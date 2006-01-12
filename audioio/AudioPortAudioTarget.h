/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/*
    A waveform viewer and audio annotation editor.
    Chris Cannam, Queen Mary University of London, 2005-2006
    
    This is experimental software.  Not for distribution.
*/

#ifndef _AUDIO_PORT_AUDIO_TARGET_H_
#define _AUDIO_PORT_AUDIO_TARGET_H_

#ifdef HAVE_PORTAUDIO

#include <portaudio.h>
#include <vector>

#include "AudioCallbackPlayTarget.h"

class AudioCallbackPlaySource;

class AudioPortAudioTarget : public AudioCallbackPlayTarget
{
    Q_OBJECT

public:
    AudioPortAudioTarget(AudioCallbackPlaySource *source);
    virtual ~AudioPortAudioTarget();

    virtual bool isOK() const;

public slots:
    virtual void sourceModelReplaced();

protected:
    int process(void *input, void *output, unsigned long frames,
		PaTimestamp outTime);

    static int processStatic(void *, void *, unsigned long,
			     PaTimestamp, void *);

    PortAudioStream *m_stream;

    int m_bufferSize;
    int m_sampleRate;
    int m_latency;
};

#endif /* HAVE_PORTAUDIO */

#endif

