/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "EffectWrapper.h"

#include <rubberband/RubberBandStretcher.h>

#include "base/Debug.h"

using namespace std;

static const int DEFAULT_RING_BUFFER_SIZE = 131071;

EffectWrapper::EffectWrapper(ApplicationPlaybackSource *source) :
    m_source(source),
    m_bypassed(false),
    m_failed(false),
    m_channelCount(0)
{
}

EffectWrapper::~EffectWrapper()
{
}

void
EffectWrapper::setEffect(weak_ptr<RealTimePluginInstance> effect)
{
    lock_guard<mutex> guard(m_mutex);

    m_effect = effect;
    m_failed = false;
}

void
EffectWrapper::setBypassed(bool bypassed)
{
    lock_guard<mutex> guard(m_mutex);

    m_bypassed = bypassed;
}

bool
EffectWrapper::isBypassed() const
{
    lock_guard<mutex> guard(m_mutex);

    return m_bypassed;
}

void
EffectWrapper::reset()
{
    lock_guard<mutex> guard(m_mutex);

    for (auto &rb: m_effectOutputBuffers) {
        rb.reset();
    }
}

int
EffectWrapper::getSourceSamples(float *const *samples,
                                int nchannels, int nframes)
{
    lock_guard<mutex> guard(m_mutex);

    auto effect(m_effect.lock());
    
    if (!effect || m_bypassed || m_failed) {
        return m_source->getSourceSamples(samples, nchannels, nframes);
    }

    static int warnings = 0;
    if (nchannels != m_channelCount) {
        if (warnings >= 0) {
            SVCERR << "WARNING: getSourceSamples called for a number of channels different from that set with setSystemPlaybackChannelCount ("
                   << nchannels << " vs " << m_channelCount << ")" << endl;
            if (++warnings == 6) {
                SVCERR << "(further warnings will be suppressed)" << endl;
                warnings = -1;
            }
        }
        return 0;
    }
    
    if ((int)effect->getAudioInputCount() != m_channelCount) {
        if (!m_failed) {
            SVCERR << "EffectWrapper::getSourceSamples: "
                   << "Can't run plugin: plugin input count "
                   << effect->getAudioInputCount() 
                   << " != our channel count " << m_channelCount
                   << " (future errors for this plugin will be suppressed)"
                   << endl;
            m_failed = true;
        }
    }
    if ((int)effect->getAudioOutputCount() != m_channelCount) {
        if (!m_failed) {
            SVCERR << "EffectWrapper::getSourceSamples: "
                   << "Can't run plugin: plugin output count "
                   << effect->getAudioOutputCount() 
                   << " != our channel count " << m_channelCount
                   << " (future errors for this plugin will be suppressed)"
                   << endl;
            m_failed = true;
        }
    }

    if (m_failed) {
        return m_source->getSourceSamples(samples, nchannels, nframes);
    }
    
    float **ib = effect->getAudioInputBuffers();
    float **ob = effect->getAudioOutputBuffers();
    int blockSize = effect->getBufferSize();
    
    int got = 0;
    int offset = 0;

    while (got < nframes) {

        int read = 0;
        for (int c = 0; c < nchannels; ++c) {
            read = m_effectOutputBuffers[c].read(samples[c], nframes - got);
        }

        got += read;

        if (got < nframes) {

            int toRun = m_source->getSourceSamples(ib, nchannels, blockSize);
            if (toRun <= 0) break;

            effect->run(Vamp::RealTime::zeroTime, toRun);

            for (int c = 0; c < nchannels; ++c) {
                m_effectOutputBuffers[c].write(ob[c], toRun);
            }
        }
    }
        
    return got;
}

void
EffectWrapper::setSystemPlaybackChannelCount(int count)
{
    {
        lock_guard<mutex> guard(m_mutex);
        m_effectOutputBuffers.resize
            (count, RingBuffer<float>(DEFAULT_RING_BUFFER_SIZE));
        m_channelCount = count;
    }
    m_source->setSystemPlaybackChannelCount(count);
}

void
EffectWrapper::setSystemPlaybackSampleRate(int rate)
{
    m_source->setSystemPlaybackSampleRate(rate);
}

std::string
EffectWrapper::getClientName() const
{
    return m_source->getClientName();
}

int
EffectWrapper::getApplicationSampleRate() const
{
    return m_source->getApplicationSampleRate();
}

int
EffectWrapper::getApplicationChannelCount() const
{
    return m_source->getApplicationChannelCount();
}

void
EffectWrapper::setSystemPlaybackBlockSize(int sz)
{
    SVDEBUG << "NOTE: EffectWrapper::setSystemPlaybackBlockSize called "
            << "with size = " << sz << "; not passing to wrapped source, as "
            << "actual block size will vary" << endl;
}

void
EffectWrapper::setSystemPlaybackLatency(int latency)
{
    m_source->setSystemPlaybackLatency(latency);
}

void
EffectWrapper::setOutputLevels(float left, float right)
{
    m_source->setOutputLevels(left, right);
}

void
EffectWrapper::audioProcessingOverload()
{
    m_source->audioProcessingOverload();
}
