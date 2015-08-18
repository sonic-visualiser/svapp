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

#include "data/fileio/WavFileWriter.h"

#include <QDir>

AudioRecordTarget::AudioRecordTarget(ViewManagerBase *manager,
				     QString clientName) :
    m_viewManager(manager),
    m_clientName(clientName.toUtf8().data()),
    m_recording(false),
    m_recordSampleRate(44100),
    m_writer(0)
{
}

AudioRecordTarget::~AudioRecordTarget()
{
    QMutexLocker locker(&m_mutex);
    delete m_writer;
}

void
AudioRecordTarget::setSystemRecordBlockSize(int sz)
{
}

void
AudioRecordTarget::setSystemRecordSampleRate(int n)
{
    m_recordSampleRate = n;
}

void
AudioRecordTarget::setSystemRecordLatency(int sz)
{
}

void
AudioRecordTarget::putSamples(int nframes, float **samples)
{
    QMutexLocker locker(&m_mutex); //!!! bad here
    if (!m_recording) return;
    m_writer->writeSamples(samples, nframes);
}

void
AudioRecordTarget::setInputLevels(float peakLeft, float peakRight)
{
}

QString
AudioRecordTarget::startRecording()
{
    QMutexLocker locker(&m_mutex);
    if (m_recording) {
        cerr << "WARNING: AudioRecordTarget::startRecording: We are already recording" << endl;
        return "";
    }

    QDir parent(TempDirectory::getInstance()->getContainingPath());
    QDir recordedDir;
    QString subdirname = "recorded"; //!!! tr?
    if (!parent.mkpath(subdirname)) {
        cerr << "ERROR: AudioRecordTarget::startRecording: Failed to create recorded dir in \"" << parent.canonicalPath() << "\"" << endl;
        return "";
    } else {
        recordedDir = parent.filePath(subdirname);
    }

    //!!! todo proper temp name as in TempDirectory

    QString filename = "recorded.wav"; //!!!

    m_audioFileName = recordedDir.filePath(filename);
        
    m_writer = new WavFileWriter(m_audioFileName,
                                 m_recordSampleRate,
                                 2,
                                 WavFileWriter::WriteToTarget);

    if (!m_writer->isOK()) {
        cerr << "ERROR: AudioRecordTarget::startRecording: Recording failed: "
             << m_writer->getError() << endl;
        //!!! and throw?
        delete m_writer;
        return "";
    }

    m_recording = true;
    
    return m_audioFileName;
}

void
AudioRecordTarget::stopRecording()
{
    QMutexLocker locker(&m_mutex);
    if (!m_recording) {
        cerr << "WARNING: AudioRecordTarget::startRecording: Not recording" << endl;
        return;
    }

    m_writer->close();
    delete m_writer;
    m_recording = false;
}


