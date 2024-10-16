/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "AudioCallbackPlaySource.h"

#include "AudioGenerator.h"
#include "TimeStretchWrapper.h"
#include "EffectWrapper.h"

#include "data/model/Model.h"
#include "base/ViewManagerBase.h"
#include "base/PlayParameterRepository.h"
#include "base/Preferences.h"
#include "data/model/DenseTimeValueModel.h"
#include "data/model/WaveFileModel.h"
#include "data/model/ReadOnlyWaveFileModel.h"
#include "data/model/SparseOneDimensionalModel.h"
#include "plugin/RealTimePluginInstance.h"

#include "bqaudioio/SystemPlaybackTarget.h"
#include "bqaudioio/ResamplerWrapper.h"

#include "bqvec/VectorOps.h"

#include <iostream>
#include <cassert>

using breakfastquay::v_zero_channels;

using std::cout;
using std::endl;

namespace sv {

//#define DEBUG_AUDIO_PLAY_SOURCE 1
//#define DEBUG_AUDIO_PLAY_SOURCE_PLAYING 1

static const int DEFAULT_RING_BUFFER_SIZE = 131071;

AudioCallbackPlaySource::AudioCallbackPlaySource(ViewManagerBase *manager,
                                                 QString clientName) :
    m_viewManager(manager),
    m_audioGenerator(new AudioGenerator()),
    m_clientName(clientName.toUtf8().data()),
    m_readBuffers(nullptr),
    m_writeBuffers(nullptr),
    m_readBufferFill(0),
    m_writeBufferFill(0),
    m_bufferScavenger(1),
    m_sourceChannelCount(0),
    m_blockSize(1024),
    m_sourceSampleRate(0),
    m_deviceSampleRate(0),
    m_deviceChannelCount(0),
    m_playLatency(0),
    m_target(nullptr),
    m_lastRetrievalTimestamp(0.0),
    m_lastRetrievedBlockSize(0),
    m_trustworthyTimestamps(true),
    m_lastCurrentFrame(0),
    m_playing(false),
    m_exiting(false),
    m_lastModelEndFrame(0),
    m_ringBufferSize(DEFAULT_RING_BUFFER_SIZE),
    m_tmpMixbuf(nullptr),
    m_tmpMixbufSize(0),
    m_chunkBufferPtrs(nullptr),
    m_chunkBufferPtrCount(0),
    m_outputLeft(0.0),
    m_outputRight(0.0),
    m_levelsSet(false),
    m_playStartFrame(0),
    m_playStartFramePassed(false),
    m_enforceStereo(true),
    m_fillThread(nullptr),
    m_resamplerWrapper(nullptr),
    m_timeStretchWrapper(nullptr),
    m_auditioningEffectWrapper(nullptr)
{
    m_viewManager->setAudioPlaySource(this);

    connect(m_viewManager, SIGNAL(selectionChanged()),
            this, SLOT(selectionChanged()));
    connect(m_viewManager, SIGNAL(playLoopModeChanged()),
            this, SLOT(playLoopModeChanged()));
    connect(m_viewManager, SIGNAL(playSelectionModeChanged()),
            this, SLOT(playSelectionModeChanged()));

    connect(this, SIGNAL(playStatusChanged(bool)),
            m_viewManager, SLOT(playStatusChanged(bool)));

    connect(PlayParameterRepository::getInstance(),
            SIGNAL(playParametersChanged(int)),
            this, SLOT(playParametersChanged(int)));

    connect(Preferences::getInstance(),
            SIGNAL(propertyChanged(PropertyContainer::PropertyName)),
            this, SLOT(preferenceChanged(PropertyContainer::PropertyName)));
}

AudioCallbackPlaySource::~AudioCallbackPlaySource()
{
#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::~AudioCallbackPlaySource entering" << endl;
#endif
    m_exiting = true;

    if (m_fillThread) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource dtor: awakening thread" << endl;
#endif
        m_condition.wakeAll();
        m_fillThread->wait();
        delete m_fillThread;
    }

    clearModels();
    
    if (m_readBuffers != m_writeBuffers) {
        delete m_readBuffers;
    }

    delete m_writeBuffers;
    delete[] m_tmpMixbuf;
    delete[] m_chunkBufferPtrs;

    delete m_audioGenerator;

    delete m_timeStretchWrapper;
    delete m_auditioningEffectWrapper;
    delete m_resamplerWrapper;

    m_bufferScavenger.scavenge(true);
    m_pluginScavenger.scavenge(true);
#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::~AudioCallbackPlaySource finishing" << endl;
#endif
}

breakfastquay::ApplicationPlaybackSource *
AudioCallbackPlaySource::getApplicationPlaybackSource()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_timeStretchWrapper) {
        return m_timeStretchWrapper;
    }

    checkWrappers();
    return m_timeStretchWrapper;
}

void
AudioCallbackPlaySource::checkWrappers()
{
    // to be called only with m_mutex held

    if (!m_resamplerWrapper) {
        m_resamplerWrapper = new breakfastquay::ResamplerWrapper(this);
    }
    if (!m_auditioningEffectWrapper) {
        m_auditioningEffectWrapper = new EffectWrapper(m_resamplerWrapper);
    }
    if (!m_timeStretchWrapper) {
        m_timeStretchWrapper = new TimeStretchWrapper(m_auditioningEffectWrapper);
        m_timeStretchWrapper->setQuality
            (Preferences::getInstance()->getFinerTimeStretch() ?
             TimeStretchWrapper::Quality::Finer :
             TimeStretchWrapper::Quality::Faster);
    }
}

void
AudioCallbackPlaySource::addModel(ModelId modelId)
{
    if (m_models.find(modelId) != m_models.end()) return;

    Profiler profiler("AudioCallbackPlaySource::addModel");
    
    bool willPlay = m_audioGenerator->addModel(modelId);

    auto model = ModelById::get(modelId);
    if (!model) return;

    m_mutex.lock();

    m_models.insert(modelId);

    if (model->getEndFrame() > m_lastModelEndFrame) {
        m_lastModelEndFrame = model->getEndFrame();
    }

    bool buffersIncreased = false, srChanged = false;

    int modelChannels = 1;
    auto rowfm = std::dynamic_pointer_cast<ReadOnlyWaveFileModel>(model);
    if (rowfm) modelChannels = rowfm->getChannelCount();
    if (modelChannels > m_sourceChannelCount) {
        m_sourceChannelCount = modelChannels;
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource: Adding model with " << modelChannels << " channels at rate " << model->getSampleRate() << endl;
#endif

    if (m_sourceSampleRate == 0) {

        SVDEBUG << "AudioCallbackPlaySource::addModel: Source rate changing from 0 to "
                << model->getSampleRate() << endl;

        m_sourceSampleRate = model->getSampleRate();
        srChanged = true;

    } else if (model->getSampleRate() != m_sourceSampleRate) {

        // If this is a read-only wave file model and we have no
        // other, we can just switch to this model's sample rate

        if (rowfm) {

            bool conflicting = false;

            for (ModelId otherId: m_models) {
                // Only read-only wave file models should be
                // considered conflicting -- writable wave file models
                // are derived and we shouldn't take their rates into
                // account.  Also, don't give any particular weight to
                // a file that's already playing at the wrong rate
                // anyway
                if (otherId == modelId) continue;
                auto other = ModelById::getAs<ReadOnlyWaveFileModel>(otherId);
                if (other &&
                    other->getSampleRate() != model->getSampleRate() &&
                    other->getSampleRate() == m_sourceSampleRate) {
                    SVDEBUG << "AudioCallbackPlaySource::addModel: Conflicting wave file model " << otherId << " found" << endl;
                    conflicting = true;
                    break;
                }
            }

            if (conflicting) {

                SVCERR << "AudioCallbackPlaySource::addModel: ERROR: "
                          << "New model sample rate does not match" << endl
                          << "existing model(s) (new " << model->getSampleRate()
                          << " vs " << m_sourceSampleRate
                          << "), playback will be wrong"
                          << endl;
                
                emit sampleRateMismatch(model->getSampleRate(),
                                        m_sourceSampleRate,
                                        false);
            } else {
                SVDEBUG << "AudioCallbackPlaySource::addModel: Source rate changing from "
                        << m_sourceSampleRate << " to " << model->getSampleRate() << endl;
                
                m_sourceSampleRate = model->getSampleRate();
                srChanged = true;
            }
        }
    }

    if (!m_writeBuffers || (int)m_writeBuffers->size() < getTargetChannelCount()) {
        SVCERR << "m_writeBuffers size = " << (m_writeBuffers ? m_writeBuffers->size() : 0) << endl;
        SVCERR << "target channel count = " << (getTargetChannelCount()) << endl;
        clearRingBuffers(true, getTargetChannelCount());
        buffersIncreased = true;
    } else {
        if (willPlay) clearRingBuffers(true);
    }

    if (srChanged) {

        checkWrappers();

        SVCERR << "AudioCallbackPlaySource: Source sample rate changed to "
               << m_sourceSampleRate << ", updating resampler wrapper"
               << endl;
        m_resamplerWrapper->changeApplicationSampleRate
            (int(round(m_sourceSampleRate)));
        m_resamplerWrapper->reset();
    }

    rebuildRangeLists();

    m_mutex.unlock();

    m_audioGenerator->setTargetChannelCount(getTargetChannelCount());

    if (buffersIncreased) {
        SVDEBUG << "AudioCallbackPlaySource::addModel: Number of buffers increased to " << getTargetChannelCount() << endl;
        if (getTargetChannelCount() > getDeviceChannelCount()) {
            SVDEBUG << "AudioCallbackPlaySource::addModel: This is more than the device channel count, signalling channelCountIncreased" << endl;
            emit channelCountIncreased(getTargetChannelCount());
        } else {
            SVDEBUG << "AudioCallbackPlaySource::addModel: This is no more than the device channel count (" << getDeviceChannelCount() << "), so taking no action" << endl;
        }
    }
    
    if (!m_fillThread) {
        m_fillThread = new FillThread(*this);
        m_fillThread->start();
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::addModel: now have " << m_models.size() << " model(s)" << endl;
#endif

    connect(model.get(), SIGNAL(modelChangedWithin(ModelId, sv_frame_t, sv_frame_t)),
            this, SLOT(modelChangedWithin(ModelId, sv_frame_t, sv_frame_t)));

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::addModel: awakening thread" << endl;
#endif
    
    m_condition.wakeAll();
}

void
AudioCallbackPlaySource::modelChangedWithin(ModelId, sv_frame_t 
#ifdef DEBUG_AUDIO_PLAY_SOURCE
                                            startFrame
#endif
                                            , sv_frame_t endFrame)
{
#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::modelChangedWithin(" << startFrame << "," << endFrame << ")" << endl;
#endif
    if (endFrame > m_lastModelEndFrame) {
        m_lastModelEndFrame = endFrame;
        rebuildRangeLists();
    }
}

void
AudioCallbackPlaySource::removeModel(ModelId modelId)
{
    auto model = ModelById::get(modelId);
    if (!model) return;
    
    Profiler profiler("AudioCallbackPlaySource::removeModel");

    m_mutex.lock();

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::removeModel(" << modelId << ")" << endl;
#endif

    disconnect(model.get(), SIGNAL(modelChangedWithin(ModelId, sv_frame_t, sv_frame_t)),
               this, SLOT(modelChangedWithin(ModelId, sv_frame_t, sv_frame_t)));

    m_models.erase(modelId);

    sv_frame_t lastEnd = 0;
    for (ModelId otherId: m_models) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE
        SVDEBUG << "AudioCallbackPlaySource::removeModel(" << modelId << "): checking end frame on model " << otherId << endl;
#endif
        if (auto other = ModelById::get(otherId)) {
            if (other->getEndFrame() > lastEnd) {
                lastEnd = other->getEndFrame();
            }
        }
#ifdef DEBUG_AUDIO_PLAY_SOURCE
        SVDEBUG << "(done, lastEnd now " << lastEnd << ")" << endl;
#endif
    }
    m_lastModelEndFrame = lastEnd;

    m_audioGenerator->removeModel(modelId);

    if (m_models.empty()) {
        m_sourceSampleRate = 0;
    }
    
    m_mutex.unlock();

    clearRingBuffers();
}

void
AudioCallbackPlaySource::clearModels()
{
    m_mutex.lock();

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::clearModels()" << endl;
#endif

    m_models.clear();

    m_lastModelEndFrame = 0;

    m_sourceSampleRate = 0;

    m_mutex.unlock();

    m_audioGenerator->clearModels();

    clearRingBuffers();
}    

void
AudioCallbackPlaySource::clearRingBuffers(bool haveLock, int count)
{
    if (!haveLock) m_mutex.lock();

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "clearRingBuffers" << endl;
#endif

    rebuildRangeLists();

    if (count == 0) {
        if (m_writeBuffers) count = int(m_writeBuffers->size());
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "current playing frame = " << getCurrentPlayingFrame() << endl;

    SVDEBUG << "write buffer fill (before) = " << m_writeBufferFill << endl;
#endif
    
    m_writeBufferFill = getCurrentBufferedFrame();

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "current buffered frame = " << m_writeBufferFill << endl;
#endif

    if (m_readBuffers != m_writeBuffers) {
        delete m_writeBuffers;
    }

    m_writeBuffers = new RingBufferVector;

    for (int i = 0; i < count; ++i) {
        m_writeBuffers->push_back(new RingBuffer<float>(m_ringBufferSize));
    }

    m_audioGenerator->reset();
    
//    SVDEBUG << "AudioCallbackPlaySource::clearRingBuffers: Created "
//              << count << " write buffers" << endl;

    if (!haveLock) {
        m_mutex.unlock();
    }
}

void
AudioCallbackPlaySource::play(sv_frame_t startFrame)
{
    if (!m_target) return;
    
    if (!m_sourceSampleRate) {
        SVCERR << "AudioCallbackPlaySource::play: No source sample rate available, not playing" << endl;
        return;
    }
    
    if (m_viewManager->getPlaySelectionMode() &&
        !m_viewManager->getSelections().empty()) {

#ifdef DEBUG_AUDIO_PLAY_SOURCE
        SVDEBUG << "AudioCallbackPlaySource::play: constraining frame " << startFrame << " to selection = ";
#endif

        startFrame = m_viewManager->constrainFrameToSelection(startFrame);

#ifdef DEBUG_AUDIO_PLAY_SOURCE
        SVDEBUG << startFrame << endl;
#endif

    } else {
        if (startFrame < 0) {
            startFrame = 0;
        }
        if (startFrame >= m_lastModelEndFrame) {
            startFrame = 0;
        }
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "play(" << startFrame << ") -> aligned playback model ";
#endif

    startFrame = m_viewManager->alignReferenceToPlaybackFrame(startFrame);

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << startFrame << endl;
#endif

    // The fill thread will automatically empty its buffers before
    // starting again if we have not so far been playing, but not if
    // we're just re-seeking.
    // NO -- we can end up playing some first -- always reset here

    m_mutex.lock();

    if (m_timeStretchWrapper) {
        m_timeStretchWrapper->reset();
    }

    m_readBufferFill = m_writeBufferFill = startFrame;
    if (m_readBuffers) {
        for (int c = 0; c < getTargetChannelCount(); ++c) {
            RingBuffer<float> *rb = getReadRingBuffer(c);
#ifdef DEBUG_AUDIO_PLAY_SOURCE
            SVDEBUG << "reset ring buffer for channel " << c << endl;
#endif
            if (rb) rb->reset();
        }
    }

    m_mutex.unlock();

    m_audioGenerator->reset();

    m_playStartFrame = startFrame;
    m_playStartFramePassed = false;
    m_playStartedAt = RealTime::zeroTime;
    if (m_target) {
        m_playStartedAt = RealTime::fromSeconds(m_target->getCurrentTime());
    }

    bool changed = !m_playing;
    m_lastRetrievalTimestamp = 0;
    m_lastCurrentFrame = 0;
    m_playing = true;

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::play: awakening thread" << endl;
#endif

    m_condition.wakeAll();
    if (changed) {
        emit playStatusChanged(m_playing);
        emit activity(tr("Play from %1").arg
                      (RealTime::frame2RealTime
                       (m_playStartFrame, m_sourceSampleRate).toText().c_str()));
    }
}

void
AudioCallbackPlaySource::stop()
{
#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::stop()" << endl;
#endif
    bool changed = m_playing;
    m_playing = false;

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::stop: awakening thread" << endl;
#endif

    m_condition.wakeAll();
    m_lastRetrievalTimestamp = 0;
    if (changed) {
        emit playStatusChanged(m_playing);
        if (m_sourceSampleRate) {
            emit activity(tr("Stop at %1").arg
                          (RealTime::frame2RealTime
                           (m_lastCurrentFrame, m_sourceSampleRate)
                           .toText().c_str()));
        } else {
            emit activity(tr("Stop"));
        }            
    }
    m_lastCurrentFrame = 0;
}

void
AudioCallbackPlaySource::selectionChanged()
{
    if (m_viewManager->getPlaySelectionMode()) {
        clearRingBuffers();
    }
}

void
AudioCallbackPlaySource::playLoopModeChanged()
{
    clearRingBuffers();
}

void
AudioCallbackPlaySource::playSelectionModeChanged()
{
    if (!m_viewManager->getSelections().empty()) {
        clearRingBuffers();
    }
}

void
AudioCallbackPlaySource::playParametersChanged(int)
{
    clearRingBuffers();
}

void
AudioCallbackPlaySource::preferenceChanged(PropertyContainer::PropertyName name)
{
    if (name == "Use Finer Time Stretch") {
        if (m_timeStretchWrapper) {
            m_timeStretchWrapper->setQuality
                (Preferences::getInstance()->getFinerTimeStretch() ?
                 TimeStretchWrapper::Quality::Finer :
                 TimeStretchWrapper::Quality::Faster);
        }
    }        
}

void
AudioCallbackPlaySource::audioProcessingOverload()
{
    SVCERR << "Audio processing overload!" << endl;

    if (!m_playing) return;

    if (m_auditioningEffectWrapper &&
        m_auditioningEffectWrapper->haveEffect() &&
        !m_auditioningEffectWrapper->isBypassed()) {
        m_auditioningEffectWrapper->setBypassed(true);
        emit audioOverloadPluginDisabled();
        return;
    }
}

void
AudioCallbackPlaySource::setSystemPlaybackTarget(breakfastquay::SystemPlaybackTarget *target)
{
    if (target == nullptr) {
        // reset target-related facts and figures
        m_deviceSampleRate = 0;
        m_deviceChannelCount = 0;
    }
    m_target = target;
}

void
AudioCallbackPlaySource::setSystemPlaybackBlockSize(int size)
{
    SVDEBUG << "AudioCallbackPlaySource::setTarget: Block size -> " << size << endl;
    if (size != 0) {
        m_blockSize = size;
    }
    if (size * 4 > m_ringBufferSize) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE
        SVCERR << "AudioCallbackPlaySource::setTarget: Buffer size "
               << size << " > a quarter of ring buffer size "
               << m_ringBufferSize << ", calling for more ring buffer"
               << endl;
#endif
        m_ringBufferSize = size * 4;
        if (m_writeBuffers && !m_writeBuffers->empty()) {
            clearRingBuffers();
        }
    }
}

int
AudioCallbackPlaySource::getTargetBlockSize() const
{
//    SVDEBUG << "AudioCallbackPlaySource::getTargetBlockSize() -> " << m_blockSize << endl;
    return int(m_blockSize);
}

void
AudioCallbackPlaySource::setSystemPlaybackLatency(int latency)
{
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << "AudioCallbackPlaySource::setSystemPlaybackLatency: latency = "
         << latency << endl;
#endif
    m_playLatency = latency;
}

sv_frame_t
AudioCallbackPlaySource::getTargetPlayLatency() const
{
    return m_playLatency;
}

sv_frame_t
AudioCallbackPlaySource::getCurrentPlayingFrame()
{
    // This method attempts to estimate which audio sample frame is
    // "currently coming through the speakers".

    sv_samplerate_t deviceRate = getDeviceSampleRate();
    sv_frame_t latency = m_playLatency; // at target rate
    RealTime latency_t = RealTime::zeroTime;

    if (deviceRate != 0) {
        latency_t = RealTime::frame2RealTime(latency, deviceRate);
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
        SVDEBUG << "getCurrentPlayingFrame: Converting latency of "
                << latency << " frames to " << latency_t
                << " at device sample rate of " << deviceRate << endl;
#endif
    }

    return getCurrentFrame(latency_t);
}

sv_frame_t
AudioCallbackPlaySource::getCurrentBufferedFrame()
{
    return getCurrentFrame(RealTime::zeroTime);
}

sv_frame_t
AudioCallbackPlaySource::getCurrentFrame(RealTime latency_t)
{
    // The ring buffers contain data at the source sample rate and all
    // processing here happens at this rate. Resampling only happens
    // after the audio data leaves this class.
    
    // (But because historically more than one sample rate could have
    // been involved here, we do latency calculations using RealTime
    // values instead of samples.)

    sv_samplerate_t rate = getSourceSampleRate();

    if (rate == 0) return 0;

    int inbuffer = 0; // at target rate

    for (int c = 0; c < getTargetChannelCount(); ++c) {
        RingBuffer<float> *rb = getReadRingBuffer(c);
        if (rb) {
            int here = rb->getReadSpace();
            if (c == 0 || here < inbuffer) inbuffer = here;
        }
    }

    sv_frame_t readBufferFill = m_readBufferFill;
    sv_frame_t lastRetrievedBlockSize = m_lastRetrievedBlockSize;
    double lastRetrievalTimestamp = m_lastRetrievalTimestamp;
    double currentTime = 0.0;
    if (m_target) currentTime = m_target->getCurrentTime();

#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << "\nrate = " << rate << ", readBufferFill = " << readBufferFill
         << ", lastRetrievedBlockSize = " << lastRetrievedBlockSize
         << ", lastRetrievalTimestamp = " << lastRetrievalTimestamp
         << ", currentTime = " << currentTime << endl;
#endif
    
    bool looping = m_viewManager->getPlayLoopMode();

    RealTime inbuffer_t = RealTime::frame2RealTime(inbuffer, rate);

    // When the target has just requested a block from us, the last
    // sample it obtained was our buffer fill frame count minus the
    // amount of read space (converted back to source sample rate)
    // remaining now.  That sample is not expected to be played until
    // the target's play latency has elapsed.  By the time the
    // following block is requested, that sample will be at the
    // target's play latency minus the last requested block size away
    // from being played.

    RealTime sincerequest_t = RealTime::zeroTime;
    RealTime lastretrieved_t = RealTime::zeroTime;

    if (m_target &&
        m_trustworthyTimestamps &&
        lastRetrievalTimestamp != 0.0) {

        lastretrieved_t = RealTime::frame2RealTime(lastRetrievedBlockSize, rate);

        // calculate number of frames at target rate that have elapsed
        // since the end of the last call to getSourceSamples

        if (m_trustworthyTimestamps && !looping) {

            // this adjustment seems to cause more problems when looping
            double elapsed = currentTime - lastRetrievalTimestamp;

            if (elapsed > 0.0) {
                sincerequest_t = RealTime::fromSeconds
                    (elapsed / m_timeStretchWrapper->getTimeStretchRatio());
            }
        }

    } else {

        lastretrieved_t = RealTime::frame2RealTime(getTargetBlockSize(), rate);
    }

    RealTime bufferedto_t = RealTime::frame2RealTime(readBufferFill, rate);

#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << "\nbuffered to: " << bufferedto_t << ", in buffer: " << inbuffer_t << ", device latency: " << latency_t << "\n  since request: " << sincerequest_t << ", last retrieved quantity: " << lastretrieved_t << endl;
#endif

    // Normally the range lists should contain at least one item each
    // -- if playback is unconstrained, that item should report the
    // entire source audio duration.

    if (m_rangeStarts.empty()) {
        rebuildRangeLists();
    }

    if (m_rangeStarts.empty()) {
        // this code is only used in case of error in rebuildRangeLists
        RealTime playing_t = bufferedto_t
            - latency_t - lastretrieved_t - inbuffer_t
            + sincerequest_t;
        if (playing_t < RealTime::zeroTime) playing_t = RealTime::zeroTime;
        sv_frame_t frame = RealTime::realTime2Frame(playing_t, rate);
        return m_viewManager->alignPlaybackFrameToReference(frame);
    }

    int inRange = 0;
    int index = 0;

    for (int i = 0; i < (int)m_rangeStarts.size(); ++i) {
        if (bufferedto_t >= m_rangeStarts[i]) {
            inRange = index;
        } else {
            break;
        }
        ++index;
    }

    if (inRange >= int(m_rangeStarts.size())) {
        inRange = int(m_rangeStarts.size())-1;
    }

    RealTime playing_t = bufferedto_t;

    playing_t = playing_t
        - latency_t - lastretrieved_t - inbuffer_t
        + sincerequest_t;

    // This rather gross little hack is used to ensure that latency
    // compensation doesn't result in the playback pointer appearing
    // to start earlier than the actual playback does.  It doesn't
    // work properly (hence the bail-out in the middle) because if we
    // are playing a relatively short looped region, the playing time
    // estimated from the buffer fill frame may have wrapped around
    // the region boundary and end up being much smaller than the
    // theoretical play start frame, perhaps even for the entire
    // duration of playback!

    if (!m_playStartFramePassed) {
        RealTime playstart_t = RealTime::frame2RealTime(m_playStartFrame, rate);
        if (playing_t < playstart_t) {
//            SVDEBUG << "playing_t " << playing_t << " < playstart_t " 
//                      << playstart_t << endl;
            if (m_playStartedAt + latency_t <
                RealTime::fromSeconds(currentTime)) {
//                SVDEBUG << "but we've been playing for long enough that I think we should disregard it (it probably results from loop wrapping)" << endl;
                m_playStartFramePassed = true;
            } else {
                playing_t = playstart_t;
            }
        } else {
            m_playStartFramePassed = true;
        }
    }
 
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << "playing_t " << playing_t;
#endif

    playing_t = playing_t - m_rangeStarts[inRange];
 
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << " as offset into range " << inRange << " (start =" << m_rangeStarts[inRange] << " duration =" << m_rangeDurations[inRange] << ") = " << playing_t << endl;
#endif

    while (playing_t < RealTime::zeroTime) {

        if (inRange == 0) {
            if (looping) {
                inRange = int(m_rangeStarts.size()) - 1;
            } else {
                break;
            }
        } else {
            --inRange;
        }

        playing_t = playing_t + m_rangeDurations[inRange];
    }

    playing_t = playing_t + m_rangeStarts[inRange];

#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << "  playing time: " << playing_t << endl;
#endif

    if (!looping) {
        if (inRange == (int)m_rangeStarts.size()-1 &&
            playing_t >= m_rangeStarts[inRange] + m_rangeDurations[inRange]) {
SVDEBUG << "Not looping, inRange " << inRange << " == rangeStarts.size()-1, playing_t " << playing_t << " >= m_rangeStarts[inRange] " << m_rangeStarts[inRange] << " + m_rangeDurations[inRange] " << m_rangeDurations[inRange] << " -- stopping" << endl;
            stop();
        }
    }

    if (playing_t < RealTime::zeroTime) playing_t = RealTime::zeroTime;

    sv_frame_t frame = RealTime::realTime2Frame(playing_t, rate);

    if (m_lastCurrentFrame > 0 && !looping) {
        if (frame < m_lastCurrentFrame) {
            frame = m_lastCurrentFrame;
        }
    }

    m_lastCurrentFrame = frame;

    return m_viewManager->alignPlaybackFrameToReference(frame);
}

void
AudioCallbackPlaySource::rebuildRangeLists()
{
    Profiler profiler("AudioCallbackPlaySource::rebuildRangeLists");
    
    bool constrained = (m_viewManager->getPlaySelectionMode());

    m_rangeStarts.clear();
    m_rangeDurations.clear();

    sv_samplerate_t sourceRate = getSourceSampleRate();
    if (sourceRate == 0) return;

    RealTime end = RealTime::frame2RealTime(m_lastModelEndFrame, sourceRate);
    if (end == RealTime::zeroTime) return;

    if (!constrained) {
        m_rangeStarts.push_back(RealTime::zeroTime);
        m_rangeDurations.push_back(end);
        return;
    }

    MultiSelection::SelectionList selections = m_viewManager->getSelections();
    MultiSelection::SelectionList::const_iterator i;

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::rebuildRangeLists" << endl;
#endif

    if (!selections.empty()) {

        for (i = selections.begin(); i != selections.end(); ++i) {
            
            RealTime start =
                (RealTime::frame2RealTime
                 (m_viewManager->alignReferenceToPlaybackFrame(i->getStartFrame()),
                  sourceRate));
            RealTime duration = 
                (RealTime::frame2RealTime
                 (m_viewManager->alignReferenceToPlaybackFrame(i->getEndFrame()) -
                  m_viewManager->alignReferenceToPlaybackFrame(i->getStartFrame()),
                  sourceRate));
            
            m_rangeStarts.push_back(start);
            m_rangeDurations.push_back(duration);
        }
    } else {
        m_rangeStarts.push_back(RealTime::zeroTime);
        m_rangeDurations.push_back(end);
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "Now have " << m_rangeStarts.size() << " play ranges" << endl;
#endif
}

void
AudioCallbackPlaySource::setOutputLevels(float left, float right)
{
    if (left > m_outputLeft) m_outputLeft = left;
    if (right > m_outputRight) m_outputRight = right;
    m_levelsSet = true;
}

bool
AudioCallbackPlaySource::getOutputLevels(float &left, float &right)
{
    left = m_outputLeft;
    right = m_outputRight;
    bool valid = m_levelsSet;
    m_outputLeft = 0.f;
    m_outputRight = 0.f;
    m_levelsSet = false;
    return valid;
}

void
AudioCallbackPlaySource::setSystemPlaybackSampleRate(int sr)
{
    m_deviceSampleRate = sr;
}

void
AudioCallbackPlaySource::setSystemPlaybackChannelCount(int count)
{
    m_deviceChannelCount = count;
}

void
AudioCallbackPlaySource::setAuditioningEffect(std::shared_ptr<Auditionable> a)
{
    SVDEBUG << "AudioCallbackPlaySource::setAuditioningEffect(" << a << ")"
            << endl;
    
    auto plugin = std::dynamic_pointer_cast<RealTimePluginInstance>(a);
    if (a && !plugin) {
        SVCERR << "WARNING: AudioCallbackPlaySource::setAuditioningEffect: auditionable object " << a << " is not a real-time plugin instance" << endl;
    }

    m_mutex.lock();
    m_auditioningEffectWrapper->setEffect(plugin);
    m_auditioningEffectWrapper->setBypassed(false);
    m_mutex.unlock();

    SVDEBUG << "AudioCallbackPlaySource::setAuditioningEffect: set plugin to "
            << plugin << endl;
}

void
AudioCallbackPlaySource::setSoloModelSet(std::set<ModelId> s)
{
    m_audioGenerator->setSoloModelSet(s);
    clearRingBuffers();
}

void
AudioCallbackPlaySource::clearSoloModelSet()
{
    m_audioGenerator->clearSoloModelSet();
    clearRingBuffers();
}

sv_samplerate_t
AudioCallbackPlaySource::getDeviceSampleRate() const
{
    return m_deviceSampleRate;
}

int
AudioCallbackPlaySource::getSourceChannelCount() const
{
    return m_sourceChannelCount;
}

int
AudioCallbackPlaySource::getTargetChannelCount() const
{
    if (m_enforceStereo && m_sourceChannelCount < 2) return 2;
    return m_sourceChannelCount;
}

int
AudioCallbackPlaySource::getDeviceChannelCount() const
{
    return m_deviceChannelCount;
}

sv_samplerate_t
AudioCallbackPlaySource::getSourceSampleRate() const
{
    return m_sourceSampleRate;
}

void
AudioCallbackPlaySource::setTimeStretch(double factor)
{
    checkWrappers();

    m_timeStretchWrapper->setTimeStretchRatio(factor);
    
    emit activity(tr("Change time-stretch factor to %1").arg(factor));
}

int
AudioCallbackPlaySource::getSourceSamples(float *const *buffer,
                                          int requestedChannels,
                                          int count)
{
    // In principle, the target will handle channel mapping in cases
    // where our channel count differs from the device's. But that
    // only holds if our channel count doesn't change -- i.e. if
    // getApplicationChannelCount() always returns the same value as
    // it did when the target was created, and if this function always
    // returns that number of channels.
    //
    // Unfortunately that can't hold for us -- we always have at least
    // 2 channels but if the user opens a new main model with more
    // channels than that (and more than the last main model) then our
    // target channel count necessarily gets increased.
    //
    // We have:
    // 
    // getSourceChannelCount() -> number of channels available to
    // provide from real model data
    //
    // getTargetChannelCount() -> number we will actually provide;
    // same as getSourceChannelCount() except that it is always at
    // least 2
    //
    // getDeviceChannelCount() -> number the device will emit, usually
    // equal to the value of getTargetChannelCount() at the time the
    // device was initialised, unless the device could not provide
    // that number
    //
    // requestedChannels -> number the device is expecting from us,
    // always equal to the value of getTargetChannelCount() at the
    // time the device was initialised
    //
    // If the requested channel count is at least the target channel
    // count, then we go ahead and provide the target channels as
    // expected. We just zero any spare channels.
    //
    // If the requested channel count is smaller than the target
    // channel count, then we don't know what to do and we provide
    // nothing. This shouldn't happen as long as management is on the
    // ball -- we emit channelCountIncreased() when the target channel
    // count increases, and whatever code "owns" the driver should
    // have reopened the audio device when it got that signal. But
    // there's a race condition there, which we accommodate with this
    // check.

    int channels = getTargetChannelCount();

    if (!m_playing) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
        SVDEBUG << "AudioCallbackPlaySource::getSourceSamples: Not playing" << endl;
#endif
        v_zero_channels(buffer, requestedChannels, count);
        return count;
    }
    if (requestedChannels < channels) {
        SVDEBUG << "AudioCallbackPlaySource::getSourceSamples: Not enough device channels (" << requestedChannels << ", need " << channels << "); hoping device is about to be reopened" << endl;
        v_zero_channels(buffer, requestedChannels, count);
        return count;
    }
    if (requestedChannels > channels) {
        v_zero_channels(buffer + channels, requestedChannels - channels, count);
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << "AudioCallbackPlaySource::getSourceSamples: Playing" << endl;
#endif

    // Ensure that all buffers have at least the amount of data we
    // need -- else reduce the size of our requests correspondingly

    for (int ch = 0; ch < channels; ++ch) {

        RingBuffer<float> *rb = getReadRingBuffer(ch);
        
        if (!rb) {
            SVCERR << "WARNING: AudioCallbackPlaySource::getSourceSamples: "
                      << "No ring buffer available for channel " << ch
                      << ", returning no data here" << endl;
            count = 0;
            break;
        }

        int rs = rb->getReadSpace();
        if (rs < count) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE
            SVCERR << "WARNING: AudioCallbackPlaySource::getSourceSamples: "
                      << "Ring buffer for channel " << ch << " has only "
                      << rs << " (of " << count << ") samples available ("
                      << "ring buffer size is " << rb->getSize() << ", write "
                      << "space " << rb->getWriteSpace() << "), "
                      << "reducing request size" << endl;
#endif
            count = rs;
        }
    }

    if (count == 0) return 0;

    if (m_target) {
        m_lastRetrievedBlockSize = count;
        m_lastRetrievalTimestamp = m_target->getCurrentTime();
    }

    int got = 0;

#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << "channels == " << channels << endl;
#endif
        
    for (int ch = 0; ch < channels; ++ch) {

        RingBuffer<float> *rb = getReadRingBuffer(ch);

        if (rb) {

            // this is marginally more likely to leave our channels in
            // sync after a processing failure than just passing "count":
            sv_frame_t request = count;
            if (ch > 0) request = got;

            got = rb->read(buffer[ch], int(request));
            
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
            SVDEBUG << "AudioCallbackPlaySource::getSamples: got " << got << " (of " << count << ") samples on channel " << ch << ", signalling for more (possibly)" << endl;
#endif
        }

        for (int ch = 0; ch < channels; ++ch) {
            for (int i = got; i < count; ++i) {
                buffer[ch][i] = 0.0;
            }
        }
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySource::getSamples: awakening thread" << endl;
#endif

    m_condition.wakeAll();

    return got;
}

// Called from fill thread, m_playing true, mutex held
bool
AudioCallbackPlaySource::fillBuffers()
{
    sv_frame_t space = 0;
    for (int c = 0; c < getTargetChannelCount(); ++c) {
        RingBuffer<float> *wb = getWriteRingBuffer(c);
        if (wb) {
            sv_frame_t spaceHere = wb->getWriteSpace();
            if (c == 0 || spaceHere < space) space = spaceHere;
        }
    }
    
    if (space == 0) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE
        SVDEBUG << "AudioCallbackPlaySourceFillThread: no space to fill" << endl;
#endif
        return false;
    }

    // space is now the number of samples that can be written on each
    // channel's write ringbuffer
    
    sv_frame_t f = m_writeBufferFill;
        
    bool readWriteEqual = (m_readBuffers == m_writeBuffers);

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    if (!readWriteEqual) {
        SVDEBUG << "AudioCallbackPlaySourceFillThread: note read buffers != write buffers" << endl;
    }
    SVDEBUG << "AudioCallbackPlaySourceFillThread: filling " << space << " frames" << endl;
#endif

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "buffered to " << f << " already" << endl;
#endif

    int channels = getTargetChannelCount();

    static float **bufferPtrs = nullptr;
    static int bufferPtrCount = 0;

    if (bufferPtrCount < channels) {
        if (bufferPtrs) delete[] bufferPtrs;
        bufferPtrs = new float *[channels];
        bufferPtrCount = channels;
    }

    sv_frame_t generatorBlockSize = m_audioGenerator->getBlockSize();

    // space must be a multiple of generatorBlockSize
    sv_frame_t reqSpace = space;
    space = (reqSpace / generatorBlockSize) * generatorBlockSize;
    if (space == 0) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE
        SVDEBUG << "requested fill of " << reqSpace
             << " is less than generator block size of "
             << generatorBlockSize << ", leaving it" << endl;
#endif
        return false;
    }

    if (m_tmpMixbufSize < channels * space) {
        delete[] m_tmpMixbuf;
        m_tmpMixbufSize = channels * space;
        m_tmpMixbuf = new float[m_tmpMixbufSize];
    }

    for (int c = 0; c < channels; ++c) {

        bufferPtrs[c] = m_tmpMixbuf + c * space;
            
        for (int i = 0; i < space; ++i) {
            m_tmpMixbuf[c * space + i] = 0.0f;
        }
    }

    sv_frame_t got = mixModels(f, space, bufferPtrs); // also modifies f

    for (int c = 0; c < channels; ++c) {

        RingBuffer<float> *wb = getWriteRingBuffer(c);
        if (wb) {
            int actual = wb->write(bufferPtrs[c], int(got));
#ifdef DEBUG_AUDIO_PLAY_SOURCE
            SVDEBUG << "Wrote " << actual << " samples for ch " << c << ", now "
                 << wb->getReadSpace() << " to read" 
                 << endl;
#endif
            if (actual < got) {
                SVCERR << "WARNING: Buffer overrun in channel " << c
                       << ": wrote " << actual << " of " << got
                       << " samples" << endl;
            }
        }
    }

    m_writeBufferFill = f;
    if (readWriteEqual) m_readBufferFill = f;

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "Read buffer fill is now " << m_readBufferFill << ", write buffer fill "
         << m_writeBufferFill << endl;
#endif

    //!!! how do we know when ended? need to mark up a fully-buffered flag and check this if we find the buffers empty in getSourceSamples

    return true;
}    

sv_frame_t
AudioCallbackPlaySource::mixModels(sv_frame_t &frame, sv_frame_t count, float **buffers)
{
    sv_frame_t processed = 0;
    sv_frame_t chunkStart = frame;
    sv_frame_t chunkSize = count;
    sv_frame_t selectionSize = 0;
    sv_frame_t nextChunkStart = chunkStart + chunkSize;
    
    bool looping = m_viewManager->getPlayLoopMode();
    bool constrained = (m_viewManager->getPlaySelectionMode() &&
                        !m_viewManager->getSelections().empty());

    int channels = getTargetChannelCount();

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "mixModels: start " << frame << ", size " << count << ", channels " << channels << endl;
#endif
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    if (constrained) {
        SVDEBUG << "Manager has " << m_viewManager->getSelections().size() << " selection(s):" << endl;
        for (auto sel: m_viewManager->getSelections()) {
            SVDEBUG << sel.getStartFrame() << " -> " << sel.getEndFrame()
                 << " (" << (sel.getEndFrame() - sel.getStartFrame()) << " frames)"
                 << endl;
        }
    }
#endif
    
    if (m_chunkBufferPtrCount < channels) {
        if (m_chunkBufferPtrs) delete[] m_chunkBufferPtrs;
        m_chunkBufferPtrCount = channels;
        m_chunkBufferPtrs = new float *[m_chunkBufferPtrCount];
    }

    for (int c = 0; c < channels; ++c) {
        m_chunkBufferPtrs[c] = buffers[c];
    }

    while (processed < count) {
        
        chunkSize = count - processed;
        nextChunkStart = chunkStart + chunkSize;
        selectionSize = 0;

        sv_frame_t fadeIn = 0, fadeOut = 0;

        if (constrained) {

            sv_frame_t rChunkStart =
                m_viewManager->alignPlaybackFrameToReference(chunkStart);
            
            Selection selection =
                m_viewManager->getContainingSelection(rChunkStart, true);
            
            if (selection.isEmpty()) {
                if (looping) {
                    selection = *m_viewManager->getSelections().begin();
                    chunkStart = m_viewManager->alignReferenceToPlaybackFrame
                        (selection.getStartFrame());
                    fadeIn = 50;
                }
            }

            if (selection.isEmpty()) {

                chunkSize = 0;
                nextChunkStart = chunkStart;

            } else {

                sv_frame_t sf = m_viewManager->alignReferenceToPlaybackFrame
                    (selection.getStartFrame());
                sv_frame_t ef = m_viewManager->alignReferenceToPlaybackFrame
                    (selection.getEndFrame());

                selectionSize = ef - sf;

                if (chunkStart < sf) {
                    chunkStart = sf;
                    fadeIn = 50;
                }

                nextChunkStart = chunkStart + chunkSize;

                if (nextChunkStart >= ef) {
                    nextChunkStart = ef;
                    fadeOut = 50;
                }

                chunkSize = nextChunkStart - chunkStart;
            }
        
        } else if (looping && m_lastModelEndFrame > 0) {

            if (chunkStart >= m_lastModelEndFrame) {
                chunkStart = 0;
            }
            if (chunkSize > m_lastModelEndFrame - chunkStart) {
                chunkSize = m_lastModelEndFrame - chunkStart;
            }
            nextChunkStart = chunkStart + chunkSize;
        }

#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
        SVDEBUG << "chunkStart " << chunkStart << ", chunkSize " << chunkSize << ", nextChunkStart " << nextChunkStart << ", frame " << frame << ", count " << count << ", processed " << processed << endl;
#endif
        
        if (!chunkSize) {
            // We need to maintain full buffers so that the other
            // thread can tell where it's got to in the playback -- so
            // return the full amount here
            frame = frame + count;
            if (frame < nextChunkStart) {
                frame = nextChunkStart;
            }
#ifdef DEBUG_AUDIO_PLAY_SOURCE
            SVDEBUG << "mixModels: ending at " << nextChunkStart << ", returning frame as "
                 << frame << endl;
#endif
            return count;
        }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
        SVDEBUG << "mixModels: chunk at " << chunkStart << " -> " << nextChunkStart << " (size " << chunkSize << ")" << endl;
#endif

        if (selectionSize < 100) {
            fadeIn = 0;
            fadeOut = 0;
        } else if (selectionSize < 300) {
            if (fadeIn > 0) fadeIn = 10;
            if (fadeOut > 0) fadeOut = 10;
        }

        if (fadeIn > 0) {
            if (processed * 2 < fadeIn) {
                fadeIn = processed * 2;
            }
        }

        if (fadeOut > 0) {
            if ((count - processed - chunkSize) * 2 < fadeOut) {
                fadeOut = (count - processed - chunkSize) * 2;
            }
        }

        for (std::set<ModelId>::iterator mi = m_models.begin();
             mi != m_models.end(); ++mi) {
            
            (void) m_audioGenerator->mixModel(*mi, chunkStart, 
                                              chunkSize, m_chunkBufferPtrs,
                                              fadeIn, fadeOut);
        }

        for (int c = 0; c < channels; ++c) {
            m_chunkBufferPtrs[c] += chunkSize;
        }

        processed += chunkSize;
        chunkStart = nextChunkStart;
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "mixModels returning " << processed << " frames to " << nextChunkStart << endl;
#endif

    frame = nextChunkStart;
    return processed;
}

void
AudioCallbackPlaySource::unifyRingBuffers()
{
    if (m_readBuffers == m_writeBuffers) return;

    // only unify if there will be something to read
    for (int c = 0; c < getTargetChannelCount(); ++c) {
        RingBuffer<float> *wb = getWriteRingBuffer(c);
        if (wb) {
            if (wb->getReadSpace() < m_blockSize * 2) {
                if ((m_writeBufferFill + m_blockSize * 2) < 
                    m_lastModelEndFrame) {
                    // OK, we don't have enough and there's more to
                    // read -- don't unify until we can do better
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
                    SVDEBUG << "AudioCallbackPlaySource::unifyRingBuffers: Not unifying: write buffer has less (" << wb->getReadSpace() << ") than " << m_blockSize*2 << " to read and write buffer fill (" << m_writeBufferFill << ") is not close to end frame (" << m_lastModelEndFrame << ")" << endl;
#endif
                    return;
                }
            }
            break;
        }
    }

    sv_frame_t rf = m_readBufferFill;
    RingBuffer<float> *rb = getReadRingBuffer(0);
    if (rb) {
        int rs = rb->getReadSpace();
        //!!! incorrect when in non-contiguous selection, see comments elsewhere
//        SVDEBUG << "rs = " << rs << endl;
        if (rs < rf) rf -= rs;
        else rf = 0;
    }
    
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << "AudioCallbackPlaySource::unifyRingBuffers: m_readBufferFill = " << m_readBufferFill << ", rf = " << rf << ", m_writeBufferFill = " << m_writeBufferFill << endl;
#endif

    sv_frame_t wf = m_writeBufferFill;
    sv_frame_t skip = 0;
    for (int c = 0; c < getTargetChannelCount(); ++c) {
        RingBuffer<float> *wb = getWriteRingBuffer(c);
        if (wb) {
            if (c == 0) {
                
                int wrs = wb->getReadSpace();
//                SVDEBUG << "wrs = " << wrs << endl;

                if (wrs < wf) wf -= wrs;
                else wf = 0;
//                SVDEBUG << "wf = " << wf << endl;
                
                if (wf < rf) skip = rf - wf;
                if (skip == 0) break;
            }

//            SVDEBUG << "skipping " << skip << endl;
            wb->skip(int(skip));
        }
    }
                    
    m_bufferScavenger.claim(m_readBuffers);
    m_readBuffers = m_writeBuffers;
    m_readBufferFill = m_writeBufferFill;
#ifdef DEBUG_AUDIO_PLAY_SOURCE_PLAYING
    SVDEBUG << "unified" << endl;
#endif
}

void
AudioCallbackPlaySource::FillThread::run()
{
    AudioCallbackPlaySource &s(m_source);
    
#ifdef DEBUG_AUDIO_PLAY_SOURCE
    SVDEBUG << "AudioCallbackPlaySourceFillThread starting" << endl;
#endif

    s.m_mutex.lock();

    bool previouslyPlaying = s.m_playing;
    bool work = false;

    while (!s.m_exiting) {

        s.unifyRingBuffers();
        s.m_bufferScavenger.scavenge();
        s.m_pluginScavenger.scavenge();

        if (work && s.m_playing && s.getSourceSampleRate()) {
            
#ifdef DEBUG_AUDIO_PLAY_SOURCE
            SVDEBUG << "AudioCallbackPlaySourceFillThread: not waiting" << endl;
#endif

            s.m_mutex.unlock();
            s.m_mutex.lock();

        } else {
            
            double ms = 100;
            if (s.getSourceSampleRate() > 0) {
                ms = double(s.m_ringBufferSize) / s.getSourceSampleRate() * 1000.0;
            }
            
            if (s.m_playing) ms /= 10;

#ifdef DEBUG_AUDIO_PLAY_SOURCE
            if (!s.m_playing) SVDEBUG << endl;
            SVDEBUG << "AudioCallbackPlaySourceFillThread: waiting for " << ms << "ms..." << endl;
#endif
            
            s.m_condition.wait(&s.m_mutex, int(ms));
        }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
        SVDEBUG << "AudioCallbackPlaySourceFillThread: awoken" << endl;
#endif

        work = false;

        if (!s.getSourceSampleRate()) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE
            SVDEBUG << "AudioCallbackPlaySourceFillThread: source sample rate is zero" << endl;
#endif
            continue;
        }

        bool playing = s.m_playing;

        if (playing && !previouslyPlaying) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE
            SVDEBUG << "AudioCallbackPlaySourceFillThread: playback state changed, resetting" << endl;
#endif
            for (int c = 0; c < s.getTargetChannelCount(); ++c) {
                RingBuffer<float> *rb = s.getReadRingBuffer(c);
                if (rb) rb->reset();
            }
        }
        previouslyPlaying = playing;

        work = s.fillBuffers();
    }

    s.m_mutex.unlock();
}

} // end namespace sv

