/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    A waveform viewer and audio annotation editor.
    Chris Cannam, Queen Mary University of London, 2005-2006
    
    This is experimental software.  Not for distribution.
*/

#ifndef _AUDIO_CORE_AUDIO_TARGET_H_
#define _AUDIO_CORE_AUDIO_TARGET_H_

#ifdef HAVE_COREAUDIO

#include <jack/jack.h>
#include <vector>

#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <AudioUnit/AUComponent.h>
#include <AudioUnit/AudioUnitProperties.h>
#include <AudioUnit/AudioUnitParameters.h>
#include <AudioUnit/AudioOutputUnit.h>

#include "AudioCallbackPlayTarget.h"

class AudioCallbackPlaySource;

class AudioCoreAudioTarget : public AudioCallbackPlayTarget
{
    Q_OBJECT

public:
    AudioCoreAudioTarget(AudioCallbackPlaySource *source);
    ~AudioCoreAudioTarget();

    virtual bool isOK() const;

public slots:
    virtual void sourceModelReplaced();

protected:
    OSStatus process(void *data,
		     AudioUnitRenderActionFlags *flags,
		     const AudioTimeStamp *timestamp,
		     unsigned int inbus,
		     unsigned int inframes,
		     AudioBufferList *ioData);

    int m_bufferSize;
    int m_sampleRate;
    int m_latency;
};

#endif /* HAVE_COREAUDIO */

#endif

