/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/*
    A waveform viewer and audio annotation editor.
    Chris Cannam, Queen Mary University of London, 2005
    
    This is experimental software.  Not for distribution.
*/

#ifndef _AUDIO_JACK_TARGET_H_
#define _AUDIO_JACK_TARGET_H_

#ifdef HAVE_JACK

#include <jack/jack.h>
#include <vector>

#include "AudioCallbackPlayTarget.h"

#include <QMutex>

class AudioCallbackPlaySource;

class AudioJACKTarget : public AudioCallbackPlayTarget
{
    Q_OBJECT

public:
    AudioJACKTarget(AudioCallbackPlaySource *source);
    virtual ~AudioJACKTarget();

    virtual bool isOK() const;

public slots:
    virtual void sourceModelReplaced();

protected:
    int process(jack_nframes_t nframes);

    static int processStatic(jack_nframes_t, void *);

    jack_client_t              *m_client;
    std::vector<jack_port_t *>  m_outputs;
    jack_nframes_t              m_bufferSize;
    jack_nframes_t              m_sampleRate;
    QMutex                      m_mutex;
};

#endif /* HAVE_JACK */

#endif

