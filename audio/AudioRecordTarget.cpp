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

#include "AudioRecordTarget.h"

#include "base/ViewManagerBase.h"
#include "base/TempDirectory.h"

#include "data/model/WritableWaveFileModel.h"

#include <QDir>

AudioRecordTarget::AudioRecordTarget(ViewManagerBase *manager,
				     QString clientName) :
    m_viewManager(manager),
    m_clientName(clientName.toUtf8().data()),
    m_recording(false),
    m_recordSampleRate(44100),
    m_recordChannelCount(2),
    m_frameCount(0),
    m_model(0)
{
}

AudioRecordTarget::~AudioRecordTarget()
{
    QMutexLocker locker(&m_mutex);
}

int
AudioRecordTarget::getApplicationSampleRate() const
{
    return 0; // don't care
}

int
AudioRecordTarget::getApplicationChannelCount() const
{
    return m_recordChannelCount;
}

void
AudioRecordTarget::setSystemRecordBlockSize(int)
{
}

void
AudioRecordTarget::setSystemRecordSampleRate(int n)
{
    m_recordSampleRate = n;
}

void
AudioRecordTarget::setSystemRecordLatency(int)
{
}

void
AudioRecordTarget::setSystemRecordChannelCount(int c)
{
    m_recordChannelCount = c;
}

void
AudioRecordTarget::putSamples(const float *const *samples, int, int nframes)
{
    bool secChanged = false;
    sv_frame_t frameToEmit = 0;

    {
        QMutexLocker locker(&m_mutex); //!!! bad here
        if (!m_recording) return;

        m_model->addSamples(samples, nframes);

        sv_frame_t priorFrameCount = m_frameCount;
        m_frameCount += nframes;

        RealTime priorRT = RealTime::frame2RealTime
            (priorFrameCount, m_recordSampleRate);
        RealTime postRT = RealTime::frame2RealTime
            (m_frameCount, m_recordSampleRate);

        secChanged = (postRT.sec > priorRT.sec);
        if (secChanged) frameToEmit = m_frameCount;
    }

    if (secChanged) {
        emit recordDurationChanged(frameToEmit, m_recordSampleRate);
    }
}

void
AudioRecordTarget::setInputLevels(float left, float right)
{
    cerr << "AudioRecordTarget::setInputLevels(" << left << "," << right << ")"
         << endl;
}

void
AudioRecordTarget::modelAboutToBeDeleted()
{
    QMutexLocker locker(&m_mutex);
    if (sender() == m_model) {
        m_model = 0;
        m_recording = false;
    }
}

QString
AudioRecordTarget::getRecordContainerFolder()
{
    QDir parent(TempDirectory::getInstance()->getContainingPath());
    QString subdirname("recorded");

    if (!parent.mkpath(subdirname)) {
        SVCERR << "ERROR: AudioRecordTarget::getRecordContainerFolder: Failed to create recorded dir in \"" << parent.canonicalPath() << "\"" << endl;
        return "";
    } else {
        return parent.filePath(subdirname);
    }
}

QString
AudioRecordTarget::getRecordFolder()
{
    QDir parent(getRecordContainerFolder());
    QDateTime now = QDateTime::currentDateTime();
    QString subdirname = QString("%1").arg(now.toString("yyyyMMdd"));

    if (!parent.mkpath(subdirname)) {
        SVCERR << "ERROR: AudioRecordTarget::getRecordFolder: Failed to create recorded dir in \"" << parent.canonicalPath() << "\"" << endl;
        return "";
    } else {
        return parent.filePath(subdirname);
    }
}

WritableWaveFileModel *
AudioRecordTarget::startRecording()
{
    {
        QMutexLocker locker(&m_mutex);
    
        if (m_recording) {
            SVCERR << "WARNING: AudioRecordTarget::startRecording: We are already recording" << endl;
            return 0;
        }

        m_model = 0;
        m_frameCount = 0;

        QString folder = getRecordFolder();
        if (folder == "") return 0;
        QDir recordedDir(folder);

        QDateTime now = QDateTime::currentDateTime();

        // Don't use QDateTime::toString(Qt::ISODate) as the ":" character
        // isn't permitted in filenames on Windows
        QString nowString = now.toString("yyyyMMdd-HHmmss-zzz");
    
        QString filename = tr("recorded-%1.wav").arg(nowString);
        QString label = tr("Recorded %1").arg(nowString);

        m_audioFileName = recordedDir.filePath(filename);

        m_model = new WritableWaveFileModel(m_recordSampleRate,
                                            m_recordChannelCount,
                                            m_audioFileName);

        if (!m_model->isOK()) {
            SVCERR << "ERROR: AudioRecordTarget::startRecording: Recording failed"
                   << endl;
            //!!! and throw?
            delete m_model;
            m_model = 0;
            return 0;
        }

        m_model->setObjectName(label);
        m_recording = true;
    }

    emit recordStatusChanged(true);
    return m_model;
}

void
AudioRecordTarget::stopRecording()
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_recording) {
            SVCERR << "WARNING: AudioRecordTarget::startRecording: Not recording" << endl;
            return;
        }

        m_model->writeComplete();
        m_model = 0;
        m_recording = false;
    }

    emit recordStatusChanged(false);
    emit recordCompleted();
}


