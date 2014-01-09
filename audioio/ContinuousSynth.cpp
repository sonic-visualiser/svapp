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

#include "ContinuousSynth.h"

#include "base/Debug.h"
#include "system/System.h"

#include <cmath>

ContinuousSynth::ContinuousSynth(int channels, int sampleRate, int blockSize) :
    m_channels(channels),
    m_sampleRate(sampleRate),
    m_blockSize(blockSize),
    m_prevF0(-1.f),
    m_phase(0.0)
{
}

ContinuousSynth::~ContinuousSynth()
{
}

void
ContinuousSynth::reset()
{
    m_phase = 0;
}

void
ContinuousSynth::mix(float **toBuffers, float gain, float pan, float f0)
{
    if (f0 == 0.f) f0 = m_prevF0;

    bool wasOn = (m_prevF0 > 0.f);
    bool nowOn = (f0 > 0.f);

    if (!nowOn && !wasOn) {
	m_phase = 0;
	return;
    }

    int fadeLength = 100; // samples

    float *levels = new float[m_channels];
    
    for (int c = 0; c < m_channels; ++c) {
	levels[c] = gain;
    }
    if (pan != 0.0 && m_channels == 2) {
	levels[0] *= 1.0 - pan;
	levels[1] *= pan + 1.0;
    }

//    cerr << "ContinuousSynth::mix: f0 = " << f0 << " (from " << m_prevF0 << "), phase = " << m_phase << endl;

    for (int i = 0; i < m_blockSize; ++i) {

        double fHere = (nowOn ? f0 : m_prevF0);

        if (wasOn && nowOn && (f0 != m_prevF0) && (i < fadeLength)) {
            // interpolate the frequency shift
            fHere = m_prevF0 + ((f0 - m_prevF0) * i) / fadeLength;
        }

        double phasor = (fHere * 2 * M_PI) / m_sampleRate;
    
	m_phase = m_phase + phasor;

        int harmonics = (m_sampleRate / 4) / fHere - 1;
        if (harmonics < 1) harmonics = 1;

        for (int h = 0; h < harmonics; ++h) {
            
            double hn = h*2 + 1;
            double hp = m_phase * hn;
            double v = sin(hp) / hn;

            if (!wasOn && i < fadeLength) {
                // fade in
                v = v * (i / double(fadeLength));
            } else if (!nowOn) {
                // fade out
                if (i > fadeLength) v = 0;
                else v = v * (1.0 - (i / double(fadeLength)));
            }

            for (int c = 0; c < m_channels; ++c) {
                toBuffers[c][i] += levels[c] * v;
            }
        }
    }	

    m_prevF0 = f0;

    delete[] levels;
}

