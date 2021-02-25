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

#ifndef SV_TIME_STRETCH_WRAPPER_H
#define SV_TIME_STRETCH_WRAPPER_H

#include "bqaudioio/ApplicationPlaybackSource.h"

#include "base/BaseTypes.h"

#include <vector>
#include <mutex>

namespace RubberBand {
    class RubberBandStretcher;
}

/**
 * A breakfastquay::ApplicationPlaybackSource wrapper that implements
 * time-stretching using Rubber Band. Note that the stretcher is
 * bypassed entirely when a ratio of 1.0 is set; this means it's
 * (almost) free to use one of these wrappers normally, but it also
 * means you can't switch from 1.0 to another ratio (or back again)
 * without some audible artifacts.
 *
 * This is real-time safe while the ratio is fixed, and may perform
 * reallocations when the ratio changes.
 */
class TimeStretchWrapper : public breakfastquay::ApplicationPlaybackSource
{
public:
    /**
     * Create a wrapper around the given ApplicationPlaybackSource,
     * implementing another ApplicationPlaybackSource interface that
     * draws from the same source data but with a time-stretcher
     * optionally applied.
     *
     * The wrapper does not take ownership of the wrapped
     * ApplicationPlaybackSource, whose lifespan must exceed that of
     * this object.    
     */
    TimeStretchWrapper(ApplicationPlaybackSource *source);
    ~TimeStretchWrapper();

    /**
     * Set a time stretch factor, i.e. playback speed, where 1.0 is
     * normal speed
     */
    void setTimeStretchRatio(double ratio);

    /**
     * Obtain the stretch factor.
     */
    double getTimeStretchRatio() const;

    /**
     * Clear stretcher buffers.
     */
    void reset();

    // These functions are passed through to the wrapped
    // ApplicationPlaybackSource
    
    std::string getClientName() const override;
    int getApplicationSampleRate() const override;
    int getApplicationChannelCount() const override;

    void setSystemPlaybackBlockSize(int) override;
    void setSystemPlaybackSampleRate(int) override;
    void setSystemPlaybackChannelCount(int) override;
    void setSystemPlaybackLatency(int) override;

    void setOutputLevels(float peakLeft, float peakRight) override;
    void audioProcessingOverload() override;

    /** 
     * Request some samples from the wrapped
     * ApplicationPlaybackSource, time-stretch if appropriate, and
     * return them to the target
     */
    int getSourceSamples(float *const *samples, int nchannels, int nframes)
        override;

private:
    ApplicationPlaybackSource *m_source;
    RubberBand::RubberBandStretcher *m_stretcher;
    double m_timeRatio;
    std::vector<std::vector<float>> m_inputs;
    std::mutex m_mutex;
    int m_stretcherInputSize;
    int m_channelCount;
    int m_lastReportedSystemLatency;
    sv_samplerate_t m_sampleRate;

    void checkStretcher(); // call without m_mutex held
    
    TimeStretchWrapper(const TimeStretchWrapper &)=delete;
    TimeStretchWrapper &operator=(const TimeStretchWrapper &)=delete;
};

#endif

    
