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

#include <bqaudiostream/AudioReadStreamFactory.h>
#include <bqaudiostream/AudioReadStream.h>

#include <cmath>
#include <vector>

#include "base/Debug.h"

//#define DEBUG_CLIP_MIXER 1

using std::vector;

namespace sv {

ClipMixer::ClipMixer(int channels, sv_samplerate_t sampleRate, sv_frame_t blockSize) :
    m_channels(channels),
    m_sampleRate(sampleRate),
    m_blockSize(blockSize),
    m_clipData(nullptr),
    m_clipLength(0),
    m_clipF0(0),
    m_clipRate(0)
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
ClipMixer::loadClipData(QString path, double f0, double level)
{
    if (m_clipData) {
        SVCERR << "ClipMixer::loadClipData: Already have clip loaded" << endl;
        return false;
    }

    breakfastquay::AudioReadStream *stream = nullptr;
    try {
        stream = breakfastquay::AudioReadStreamFactory::createReadStream
            (path.toStdString());
    } catch (const std::exception &e) {
        SVCERR << "ClipMixer::loadClipData: ERROR: " << e.what() << endl;
        return false;
    } 

    auto channels = stream->getChannelCount();
    auto rate = stream->getSampleRate();
    auto frames = stream->getEstimatedFrameCount();
        
    if (!stream->isSeekable() || frames == 0) {
        SVCERR << "ClipMixer::loadClipData: ERROR: Audio file \""
               << path << "\" must be of a format with known length"
               << endl;
        delete stream;
        return false;
    }
        
    vector<float> interleaved(frames * channels);
    auto obtained = stream->getInterleavedFrames(frames, interleaved.data());
    delete stream;
    
    if (obtained < frames) {
        SVCERR << "ClipMixer::loadClipData: ERROR: Too few frames read from \""
               << path << "\" (expected " << frames << ", got " << obtained
               << ")" << endl;
        return false;
    }

    m_clipData = new float[frames];
    
    for (size_t i = 0; i < frames; ++i) {
        m_clipData[i] = 0.0f;
        for (int j = 0; j < channels; ++j) {
            m_clipData[i] += interleaved[i * channels + j] * float(level);
        }
    }
    
    m_clipLength = frames;
    m_clipF0 = f0;
    m_clipRate = rate;
        
    return true;
}

void
ClipMixer::reset()
{
    m_playing.clear();
}

double
ClipMixer::getResampleRatioFor(double frequency)
{
    if (!m_clipData || !m_clipRate) return 1.0;
    double pitchRatio = m_clipF0 / frequency;
    double resampleRatio = m_sampleRate / m_clipRate;
    return pitchRatio * resampleRatio;
}

sv_frame_t
ClipMixer::getResampledClipDuration(double frequency)
{
    return sv_frame_t(ceil(double(m_clipLength) * getResampleRatioFor(frequency)));
}

void
ClipMixer::mix(float **toBuffers, 
               float gain,
               vector<NoteStart> newNotes, 
               vector<NoteEnd> endingNotes)
{
    for (NoteStart note : newNotes) {
        if (note.frequency > 20 && 
            note.frequency < 5000) {
            m_playing.push_back(note);
        }
    }

    vector<NoteStart> remaining;

    float *levels = new float[m_channels];

#ifdef DEBUG_CLIP_MIXER
    SVCERR << "ClipMixer::mix: have " << m_playing.size() << " playing note(s)"
         << " and " << endingNotes.size() << " note(s) ending here"
         << endl;
#endif

    for (NoteStart note : m_playing) {

        for (int c = 0; c < m_channels; ++c) {
            levels[c] = note.level * gain;
        }
        if (note.pan != 0.0 && m_channels == 2) {
            levels[0] *= 1.0f - note.pan;
            levels[1] *= note.pan + 1.0f;
        }

        sv_frame_t start = note.frameOffset;
        sv_frame_t durationHere = m_blockSize;
        if (start > 0) durationHere = m_blockSize - start;

        bool ending = false;

        for (NoteEnd end : endingNotes) {
            if (end.frequency == note.frequency &&
                // This is > rather than >= because if we have a
                // note-off and a note-on at the same time, the
                // note-off must be switching off an earlier note-on,
                // not the current one (zero-duration notes are
                // forbidden earlier in the pipeline)
                end.frameOffset > start &&
                end.frameOffset <= m_blockSize) {
                ending = true;
                durationHere = end.frameOffset;
                if (start > 0) durationHere = end.frameOffset - start;
                break;
            }
        }

        sv_frame_t clipDuration = getResampledClipDuration(note.frequency);
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
                        durationHere,
                        ending);
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
                   sv_frame_t sourceOffset,
                   sv_frame_t targetOffset,
                   sv_frame_t sampleCount,
                   bool isEnd)
{
    if (!m_clipData) return;

    double ratio = getResampleRatioFor(frequency);
    
    double releaseTime = 0.01;
    sv_frame_t releaseSampleCount = sv_frame_t(round(releaseTime * m_sampleRate));
    if (releaseSampleCount > sampleCount) {
        releaseSampleCount = sampleCount;
    }
    double releaseFraction = 1.0/double(releaseSampleCount);

    for (sv_frame_t i = 0; i < sampleCount; ++i) {

        sv_frame_t s = sourceOffset + i;

        double os = double(s) / ratio;
        sv_frame_t osi = sv_frame_t(floor(os));

        //!!! just linear interpolation for now (same as SV's sample
        //!!! player). a small sinc kernel would be better and
        //!!! probably "good enough"
        double value = 0.0;
        if (osi < m_clipLength) {
            value += m_clipData[osi];
        }
        if (osi + 1 < m_clipLength) {
            value += (m_clipData[osi + 1] - m_clipData[osi]) * (os - double(osi));
        }
         
        if (isEnd && i + releaseSampleCount > sampleCount) {
            value *= releaseFraction * double(sampleCount - i); // linear ramp for release
        }

        for (int c = 0; c < m_channels; ++c) {
            toBuffers[c][targetOffset + i] += float(levels[c] * value);
        }
    }
}


} // end namespace sv

