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

#include "TimeStretchWrapper.h"

#include <rubberband/RubberBandStretcher.h>

#include "base/Debug.h"

using namespace RubberBand;
using namespace std;

namespace sv {

TimeStretchWrapper::TimeStretchWrapper(ApplicationPlaybackSource *source) :
    m_source(source),
    m_stretcher(nullptr),
    m_timeRatio(1.0),
    m_quality(Quality::Finer),
    m_qualityChangePending(false),
    m_stretcherInputSize(16384),
    m_channelCount(0),
    m_lastReportedSystemLatency(0),
    m_sampleRate(0)
{
}

TimeStretchWrapper::~TimeStretchWrapper()
{
    delete m_stretcher;
}

void
TimeStretchWrapper::setTimeStretchRatio(double ratio)
{
    lock_guard<mutex> guard(m_mutex);

    SVDEBUG << "TimeStretchWrapper::setTimeStretchRatio: setting ratio to "
            << ratio << " (was " << m_timeRatio << ")" << endl;
    
    m_timeRatio = ratio;

    // Stretcher will be updated by checkStretcher() from next call to
    // reset() or getSourceSamples()
}

double
TimeStretchWrapper::getTimeStretchRatio() const
{
    return m_timeRatio;
}

void
TimeStretchWrapper::setQuality(Quality quality)
{
    lock_guard<mutex> guard(m_mutex);

    SVDEBUG << "TimeStretchWrapper::setTimeStretchRatio: setting quality to "
            << int(quality) << " (was " << int(m_quality) << ")" << endl;

    if (m_quality != quality) {
        m_qualityChangePending = true;
    }
    
    m_quality = quality;
}

TimeStretchWrapper::Quality
TimeStretchWrapper::getQuality() const
{
    return m_quality;
}

void
TimeStretchWrapper::reset()
{
    m_mutex.lock();
    
    if (m_qualityChangePending) {

        delete m_stretcher;
        m_stretcher = nullptr;
        
        m_mutex.unlock();
        checkStretcher();
        m_mutex.lock();
        
    } else if (m_stretcher) {
        m_stretcher->reset();
    }

    m_mutex.unlock();
}

int
TimeStretchWrapper::getSourceSamples(float *const *samples,
                                     int nchannels, int nframes)
{
    checkStretcher();

    lock_guard<mutex> guard(m_mutex);

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
    
    if (!m_stretcher) {
        return m_source->getSourceSamples(samples, nchannels, nframes);
    }

    vector<float *> inputPtrs(m_channelCount, nullptr);
    for (int i = 0; i < m_channelCount; ++i) {
        inputPtrs[i] = m_inputs[i].data();
    }
    
    // The input block for a given output is approx output / ratio,
    // but we can't predict it exactly, for an adaptive timestretcher.

    sv_frame_t available;

    while ((available = m_stretcher->available()) < nframes) {
        
        int reqd = int(ceil(double(nframes - available) / m_timeRatio));
        reqd = std::max(reqd, int(m_stretcher->getSamplesRequired()));
        reqd = std::min(reqd, m_stretcherInputSize);
        if (reqd == 0) reqd = 1;
        
        int got = m_source->getSourceSamples
            (inputPtrs.data(), nchannels, reqd);

        if (got <= 0) {
            // Don't print this - it happens routinely when we aren't playing!
//            SVCERR << "WARNING: Failed to obtain any source samples at all"
//                   << endl;
            return 0;
        }
            
        m_stretcher->process
            (inputPtrs.data(), size_t(got), false);
    }

    return int(m_stretcher->retrieve(samples, nframes));
}

void
TimeStretchWrapper::checkStretcher()
{
    lock_guard<mutex> guard(m_mutex);

    if (m_timeRatio == 1.0 || !m_channelCount || !m_sampleRate) {
        if (m_stretcher) {
            SVDEBUG << "TimeStretchWrapper::checkStretcher: m_timeRatio = "
                    << m_timeRatio << ", m_channelCount = " << m_channelCount
                    << ", m_sampleRate = " << m_sampleRate
                    << ", deleting existing stretcher" << endl;
            delete m_stretcher;
            m_stretcher = nullptr;
        }
        return;
    }
    
    if (m_stretcher) {
        if (m_timeRatio != m_stretcher->getTimeRatio()) {
            SVDEBUG << "TimeStretchWrapper::checkStretcher: setting stretcher ratio to " << m_timeRatio << endl;
            m_stretcher->setTimeRatio(m_timeRatio);
        }
        return;
    }

    SVDEBUG << "TimeStretchWrapper::checkStretcher: creating stretcher with ratio " << m_timeRatio << endl;

    RubberBandStretcher::Options options = 0;
    if (m_quality == Quality::Finer) {
        SVDEBUG << "TimeStretchWrapper::checkStretcher: using finer-quality stretcher" << endl;
        options = RubberBandStretcher::OptionEngineFiner;
    }
    
    m_stretcher = new RubberBandStretcher
        (size_t(round(m_sampleRate)),
         m_channelCount,
         RubberBandStretcher::OptionProcessRealTime | options,
         m_timeRatio);

    if (m_quality == Quality::Finer) {
        if (m_stretcher->getEngineVersion() != 3) {
            SVDEBUG << "TimeStretchWrapper::checkStretcher: WARNING: Unexpected engine version " << m_stretcher->getEngineVersion() << " (expected 3)" << endl;
        }
    }
    
    m_inputs.resize(m_channelCount);
    for (auto &v: m_inputs) {
        v.resize(m_stretcherInputSize);
    }

    // Notify upstream of changed latency due to stretcher
    setSystemPlaybackLatency(m_lastReportedSystemLatency);
}

void
TimeStretchWrapper::setSystemPlaybackChannelCount(int count)
{
    {
        lock_guard<mutex> guard(m_mutex);
        if (m_channelCount != count) {
            delete m_stretcher;
            m_stretcher = nullptr;
        }
        m_channelCount = count;
    }
    m_source->setSystemPlaybackChannelCount(count);
}

void
TimeStretchWrapper::setSystemPlaybackSampleRate(int rate)
{
    {
        lock_guard<mutex> guard(m_mutex);
        if (m_sampleRate != rate) {
            delete m_stretcher;
            m_stretcher = nullptr;
        }
        m_sampleRate = rate;
    }
    m_source->setSystemPlaybackSampleRate(rate);
}

std::string
TimeStretchWrapper::getClientName() const
{
    return m_source->getClientName();
}

int
TimeStretchWrapper::getApplicationSampleRate() const
{
    return m_source->getApplicationSampleRate();
}

int
TimeStretchWrapper::getApplicationChannelCount() const
{
    return m_source->getApplicationChannelCount();
}

void
TimeStretchWrapper::setSystemPlaybackBlockSize(int sz)
{
    SVDEBUG << "NOTE: TimeStretchWrapper::setSystemPlaybackBlockSize called "
            << "with size = " << sz << "; not passing to wrapped source, as "
            << "actual block size will vary" << endl;
}

void
TimeStretchWrapper::setSystemPlaybackLatency(int latency)
{
    if (m_stretcher) {
        m_source->setSystemPlaybackLatency(latency / m_timeRatio +
                                           m_stretcher->getLatency());
    } else {
        m_source->setSystemPlaybackLatency(latency);
    }

    m_lastReportedSystemLatency = latency;
}

void
TimeStretchWrapper::setOutputLevels(float left, float right)
{
    m_source->setOutputLevels(left, right);
}

void
TimeStretchWrapper::audioProcessingOverload()
{
    m_source->audioProcessingOverload();
}
} // end namespace sv

