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

#ifndef SV_EFFECT_WRAPPER_H
#define SV_EFFECT_WRAPPER_H

#include "bqaudioio/ApplicationPlaybackSource.h"

#include "base/BaseTypes.h"
#include "base/RingBuffer.h"

#include "plugin/RealTimePluginInstance.h"

#include <vector>
#include <mutex>
#include <memory>

/**
 * A breakfastquay::ApplicationPlaybackSource wrapper that applies a
 * real-time effect plugin.
 */
class EffectWrapper : public breakfastquay::ApplicationPlaybackSource
{
public:
    /**
     * Create a wrapper around the given ApplicationPlaybackSource,
     * implementing another ApplicationPlaybackSource interface that
     * draws from the same source data but with an effect optionally
     * applied.
     *
     * The wrapper does not take ownership of the wrapped
     * ApplicationPlaybackSource, whose lifespan must exceed that of
     * this object.    
     */
    EffectWrapper(ApplicationPlaybackSource *source);
    ~EffectWrapper();

    /**
     * Set the effect to apply. The effect instance is shared with the
     * caller: the expectation is that the caller may continue to
     * modify its parameters etc during auditioning. Replaces any
     * instance previously set.
     */
    void setEffect(std::weak_ptr<RealTimePluginInstance>);

    /**
     * Remove any applied effect without setting another one.
     */
    void clearEffect();

    /**
     * Bypass or un-bypass the effect.
     */
    void setBypassed(bool bypassed);

    /**
     * Return true if the effect is bypassed.
     */
    bool isBypassed() const;
    
    /**
     * Clear any buffered data.
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
     * ApplicationPlaybackSource, apply effect if set, and return them
     * to the target
     */
    int getSourceSamples(float *const *samples, int nchannels, int nframes)
        override;

private:
    ApplicationPlaybackSource *m_source;
    std::weak_ptr<RealTimePluginInstance> m_effect;
    bool m_bypassed;
    bool m_failed;
    int m_channelCount;
    std::vector<RingBuffer<float>> m_effectOutputBuffers;
    mutable std::mutex m_mutex;

    EffectWrapper(const EffectWrapper &)=delete;
    EffectWrapper &operator=(const EffectWrapper &)=delete;
};

#endif

    
