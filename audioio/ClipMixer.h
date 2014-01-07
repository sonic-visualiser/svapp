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

#ifndef _CLIP_MIXER_H_
#define _CLIP_MIXER_H_

#include <QString>
#include <vector>

/**
 * Mix in synthetic notes produced by resampling a prerecorded
 * clip. That is, this is a sampler.
 */

class ClipMixer
{
public:
    ClipMixer(int channels, int sampleRate, int blockSize);
    ~ClipMixer();

    void setChannelCount(int channels);
    
    bool loadClipData(QString clipFilePath, float clipF0);

    void reset(); // discarding any playing notes

    //!!! what can we find in common with the NoteData type and
    //!!! AudioGenerator's NoteOff?

    struct NoteStart {
	int frameOffset; // within current processing block
	float frequency; // Hz
	float level; // volume in range (0,1]
	float pan; // range [-1,1]
    };

    struct NoteEnd {
	int frameOffset; // in current processing block
        float frequency; // matching note start
    };

    void mix(float **toBuffers, 
             float gain,
	     std::vector<NoteStart> newNotes, 
	     std::vector<NoteEnd> endingNotes);

private:
    int m_channels;
    int m_sampleRate;
    int m_blockSize;

    QString m_clipPath;

    float *m_clipData;
    int m_clipLength;
    float m_clipF0;
    float m_clipRate;

    std::vector<NoteStart> m_playing;

    float getResampleRatioFor(float frequency);
    int getResampledClipDuration(float frequency);

    void mixNote(float **toBuffers, 
                 float *levels,
                 float frequency,
                 int sourceOffset, // within resampled note
                 int targetOffset, // within target buffer
                 int sampleCount);
};


#endif
