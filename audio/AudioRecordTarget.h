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

#ifndef AUDIO_RECORD_TARGET_H
#define AUDIO_RECORD_TARGET_H

#include <bqaudioio/ApplicationRecordTarget.h>

#include <string>

#include <QObject>
#include <QMutex>

#include "base/BaseTypes.h"

class ViewManagerBase;
class WritableWaveFileModel;

class AudioRecordTarget : public QObject,
			  public breakfastquay::ApplicationRecordTarget
{
    Q_OBJECT

public:
    AudioRecordTarget(ViewManagerBase *, QString clientName);
    virtual ~AudioRecordTarget();

    virtual std::string getClientName() const override { return m_clientName; }
    
    virtual int getApplicationSampleRate() const override;
    virtual int getApplicationChannelCount() const override;

    virtual void setSystemRecordBlockSize(int) override;
    virtual void setSystemRecordSampleRate(int) override;
    virtual void setSystemRecordLatency(int) override;
    virtual void setSystemRecordChannelCount(int) override;

    virtual void putSamples(const float *const *samples, int nchannels, int nframes) override;
    
    virtual void setInputLevels(float peakLeft, float peakRight) override;

    virtual void audioProcessingOverload() override { }

    QString getRecordContainerFolder();
    QString getRecordFolder();
    
    bool isRecording() const { return m_recording; }
    WritableWaveFileModel *startRecording(); // caller takes ownership
    void stopRecording();

signals:
    void recordStatusChanged(bool recording);
    void recordDurationChanged(sv_frame_t, sv_samplerate_t); // emitted occasionally
    void recordCompleted();

protected slots:
    void modelAboutToBeDeleted();
    
private:
    ViewManagerBase *m_viewManager;
    std::string m_clientName;
    bool m_recording;
    sv_samplerate_t m_recordSampleRate;
    int m_recordChannelCount;
    sv_frame_t m_frameCount;
    QString m_audioFileName;
    WritableWaveFileModel *m_model;
    QMutex m_mutex;
};

#endif
