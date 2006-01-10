/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/*
    A waveform viewer and audio annotation editor.
    Chris Cannam, Queen Mary University of London, 2005
    
    This is experimental software.  Not for distribution.
*/

#ifndef _AUDIO_CALLBACK_PLAY_TARGET_H_
#define _AUDIO_CALLBACK_PLAY_TARGET_H_

#include <QObject>

class AudioCallbackPlaySource;

class AudioCallbackPlayTarget : public QObject
{
    Q_OBJECT

public:
    AudioCallbackPlayTarget(AudioCallbackPlaySource *source);
    virtual ~AudioCallbackPlayTarget();

    virtual bool isOK() const = 0;

    float getOutputGain() const {
	return m_outputGain;
    }

public slots:
    /**
     * Set the playback gain (0.0 = silence, 1.0 = levels unmodified)
     */
    virtual void setOutputGain(float gain);

    virtual void sourceModelReplaced() = 0;

protected:
    AudioCallbackPlaySource *m_source;
    float m_outputGain;
};

#endif

