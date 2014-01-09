/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam, 2006-2014 QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "ClipMixer.h"

#include <sndfile.h>
#include <cmath>

#include "base/Debug.h"

ClipMixer::ClipMixer(int channels, int sampleRate, int blockSize) :
    m_channels(channels),
    m_sampleRate(sampleRate),
    m_blockSize(blockSize),
    m_clipData(0)
{
}

ClipMixer::~ClipMixer()
{
    if (m_clipData) free(m_clipData);
}

void
ClipMixer::setChannelCount(int channels)
{
    m_channels = channels;
}

bool
ClipMixer::loadClipData(QString path, float f0)
{
    if (m_clipData) {
        cerr << "ClipMixer::loadClipData: Already have clip loaded" << endl;
        return false;
    }

    SF_INFO info;
    SNDFILE *file;
    int sampleCount = 0;
    float *tmpFrames;
    size_t i;

    info.format = 0;
    file = sf_open(path.toLocal8Bit().data(), SFM_READ, &info);
    if (!file) {
	cerr << "ClipMixer::loadClipData: Failed to open file path \""
             << path << "\": " << sf_strerror(file) << endl;
	return false;
    }

    tmpFrames = (float *)malloc(info.frames * info.channels * sizeof(float));
    if (!tmpFrames) {
        cerr << "ClipMixer::loadClipData: malloc(" << info.frames * info.channels * sizeof(float) << ") failed" << endl;
        return false;
    }

    sf_readf_float(file, tmpFrames, info.frames);
    sf_close(file);

    m_clipData = (float *)malloc(info.frames * sizeof(float));
    if (!m_clipData) {
        cerr << "ClipMixer::loadClipData: malloc(" << info.frames * sizeof(float) << ") failed" << endl;
	free(tmpFrames);
	return false;
    }

    for (i = 0; i < info.frames; ++i) {
	int j;
	m_clipData[i] = 0.0f;
	for (j = 0; j < info.channels; ++j) {
	    m_clipData[i] += tmpFrames[i * info.channels + j];
	}
    }

    free(tmpFrames);

    m_clipLength = info.frames;
    m_clipF0 = f0;
    m_clipRate = info.samplerate;

    return true;
}

void
ClipMixer::reset()
{
    m_playing.clear();
}

float
ClipMixer::getResampleRatioFor(float frequency)
{
    if (!m_clipData) return 1.0;
    float pitchRatio = m_clipF0 / frequency;
    float resampleRatio = m_sampleRate / m_clipRate;
    return pitchRatio * resampleRatio;
}

int
ClipMixer::getResampledClipDuration(float frequency)
{
    return int(ceil(m_clipLength * getResampleRatioFor(frequency)));
}

void
ClipMixer::mix(float **toBuffers, 
               float gain,
               std::vector<NoteStart> newNotes, 
               std::vector<NoteEnd> endingNotes)
{
    foreach (NoteStart note, newNotes) {
        m_playing.push_back(note);
    }

    std::vector<NoteStart> remaining;

    float *levels = new float[m_channels];

    foreach (NoteStart note, m_playing) {

        for (int c = 0; c < m_channels; ++c) {
            levels[c] = gain;
        }
        if (note.pan != 0.0 && m_channels == 2) {
            levels[0] *= 1.0 - note.pan;
            levels[1] *= note.pan + 1.0;
        }

        int start = note.frameOffset;
        int durationHere = m_blockSize;
        if (start > 0) durationHere = m_blockSize - start;

        bool ending = false;

        foreach (NoteEnd end, endingNotes) {
            if (end.frequency == note.frequency && 
                end.frameOffset >= start &&
                end.frameOffset <= m_blockSize) {
                ending = true;
                durationHere = end.frameOffset;
                if (start > 0) durationHere = end.frameOffset - start;
                break;
            }
        }

        int clipDuration = getResampledClipDuration(note.frequency);
        if (start + clipDuration > 0) {
            if (start < 0 && start + clipDuration < durationHere) {
                durationHere = start + clipDuration;
            }
            if (durationHere > 0) {
                mixNote(toBuffers,
                        levels,
                        note.frequency,
                        start < 0 ? -start : 0,
                        start > 0 ?  start : 0,
                        durationHere);
            }
        }

        if (!ending) {
            NoteStart adjusted = note;
            adjusted.frameOffset -= m_blockSize;
            remaining.push_back(adjusted);
        }
    }

    delete[] levels;

    m_playing = remaining;
}

void
ClipMixer::mixNote(float **toBuffers,
                   float *levels,
                   float frequency,
                   int sourceOffset,
                   int targetOffset,
                   int sampleCount)
{
    if (!m_clipData) return;

    float ratio = getResampleRatioFor(frequency);
    
    //!!! todo: release time

    for (int i = 0; i < sampleCount; ++i) {

        int s = sourceOffset + i;

        float os = s / ratio;
        int osi = int(floor(os));

        //!!! just linear interpolation for now (same as SV's sample
        //!!! player). a small sinc kernel would be better and
        //!!! probably "good enough"
        float value = 0.f;
        if (osi < m_clipLength) {
            value += m_clipData[osi];
        }
        if (osi + 1 < m_clipLength) {
            value += (m_clipData[osi + 1] - m_clipData[osi]) * (os - osi);
        }
        
        for (int c = 0; c < m_channels; ++c) {
            toBuffers[c][targetOffset + i] += levels[c] * value;
        }
    }
}


