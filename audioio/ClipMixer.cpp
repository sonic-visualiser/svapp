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
    delete[] m_clipData;
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
}

void
ClipMixer::reset()
{
    //!!!
}

void
ClipMixer::mix(float **toBuffers, 
               float gain,
               std::vector<NoteStart> newNotes, 
               std::vector<NoteEnd> endingNotes)
{
    //!!! do this!
}

