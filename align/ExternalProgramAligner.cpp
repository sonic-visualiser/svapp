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

#include "ExternalProgramAligner.h"

#include <QFileInfo>
#include <QApplication>

#include "data/model/ReadOnlyWaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/AlignmentModel.h"

#include "data/fileio/CSVFileReader.h"

#include "framework/Document.h"

namespace sv {

ExternalProgramAligner::ExternalProgramAligner(Document *doc,
                                               ModelId reference,
                                               ModelId toAlign,
                                               QString program) :
    m_document(doc),
    m_reference(reference),
    m_toAlign(toAlign),
    m_program(program),
    m_process(nullptr)
{
}

ExternalProgramAligner::~ExternalProgramAligner()
{
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
    }
    
    delete m_process;
}

bool
ExternalProgramAligner::isAvailable(QString program)
{
    QFileInfo file(program);
    return file.exists() && file.isExecutable();
}

void
ExternalProgramAligner::begin()
{
    // Run an external program, passing to it paths to the main
    // model's audio file and the new model's audio file. It returns
    // the path in CSV form through stdout.

    auto reference = ModelById::getAs<ReadOnlyWaveFileModel>(m_reference);
    auto other = ModelById::getAs<ReadOnlyWaveFileModel>(m_toAlign);
    if (!reference || !other) {
        SVCERR << "ERROR: ExternalProgramAligner: Can't align non-read-only models via program (no local filename available)" << endl;
        return;
    }

    if (m_program == "") {
        emit failed(m_toAlign, tr("No external program specified"));
        return;
    }

    while (!reference->isReady(nullptr) || !other->isReady(nullptr)) {
        qApp->processEvents();
    }
    
    QString refPath = reference->getLocalFilename();
    if (refPath == "") {
        refPath = FileSource(reference->getLocation()).getLocalFilename();
    }
    
    QString otherPath = other->getLocalFilename();
    if (otherPath == "") {
        otherPath = FileSource(other->getLocation()).getLocalFilename();
    }

    if (refPath == "" || otherPath == "") {
        emit failed(m_toAlign,
                    tr("Failed to find local filepath for wave-file model"));
        return;
    }

    auto alignmentModel =
        std::make_shared<AlignmentModel>(m_reference, m_toAlign, ModelId());

    m_alignmentModel = ModelById::add(alignmentModel);
    other->setAlignment(m_alignmentModel);

    m_process = new QProcess;
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process,
            SIGNAL(finished(int, QProcess::ExitStatus)),
            this,
            SLOT(programFinished(int, QProcess::ExitStatus)));

    connect(m_process,
            SIGNAL(readyReadStandardError()),
            this,
            SLOT(logStderrOutput()));

    QStringList args;
    args << refPath << otherPath;

    SVCERR << "ExternalProgramAligner: Starting program \""
           << m_program << "\" with args: ";
    for (auto a: args) {
        SVCERR << "\"" << a << "\" ";
    }
    SVCERR << endl;

    m_process->start(m_program, args);

    bool success = m_process->waitForStarted();

    if (!success) {
        
        SVCERR << "ERROR: ExternalProgramAligner: Program did not start" << endl;

        other->setAlignment({});
        ModelById::release(m_alignmentModel);
        delete m_process;
        m_process = nullptr;

        emit failed(m_toAlign,
                    tr("Alignment program \"%1\" did not start")
                    .arg(m_program));
        
    } else {
        alignmentModel->setCompletion(10);
        m_document->addNonDerivedModel(m_alignmentModel);
    }
}

void
ExternalProgramAligner::logStderrOutput()
{
    if (!m_process) return;

    m_process->setReadChannel(QProcess::StandardError);

    qint64 byteCount = m_process->bytesAvailable();
    if (byteCount == 0) {
        m_process->setReadChannel(QProcess::StandardOutput);
        return;
    }

    QByteArray buffer = m_process->read(byteCount);
    while (buffer.endsWith('\n') || buffer.endsWith('\r')) {
        buffer.chop(1);
    }
    
    QString str = QString::fromUtf8(buffer);

    SVCERR << str << endl;
    
#if (QT_VERSION >= 0x050300)
    QString pfx = QString("[pid%1] ").arg(m_process->processId());
#else
    QString pfx = QString("[subproc] ");
#endif    
    str.replace("\r", "\\r");
    str.replace("\n", "\n" + pfx);

    SVDEBUG << pfx << str << endl;

    m_process->setReadChannel(QProcess::StandardOutput);
}

void
ExternalProgramAligner::programFinished(int exitCode,
                                        QProcess::ExitStatus status)
{
    SVCERR << "ExternalProgramAligner::programFinished" << endl;
    
    QProcess *process = qobject_cast<QProcess *>(sender());

    if (process != m_process) {
        SVCERR << "ERROR: ExternalProgramAligner: Emitting process " << process
               << " is not my process!" << endl;
        return;
    }

    logStderrOutput();
    
    auto alignmentModel = ModelById::getAs<AlignmentModel>(m_alignmentModel);
    if (!alignmentModel) {
        SVCERR << "ExternalProgramAligner: AlignmentModel no longer exists"
               << endl;
        return;
    }

    QString errorText;
    
    if (exitCode == 0 && status == 0) {

        CSVFormat format;
        format.setModelType(CSVFormat::TwoDimensionalModel);
        format.setTimingType(CSVFormat::ExplicitTiming);
        format.setTimeUnits(CSVFormat::TimeSeconds);
        format.setColumnCount(2);
        // The output format has time in the reference file first, and
        // time in the "other" file in the second column. This is a
        // more natural approach for a command-line alignment tool,
        // but it's the opposite of what we expect for native
        // alignment paths, which map from "other" file to
        // reference. These column purpose settings reflect that.
        format.setColumnPurpose(1, CSVFormat::ColumnStartTime);
        format.setColumnPurpose(0, CSVFormat::ColumnValue);
        format.setAllowQuoting(false);
        format.setSeparator(',');

        CSVFileReader reader(process, format, alignmentModel->getSampleRate());
        if (!reader.isOK()) {
            SVCERR << "ERROR: ExternalProgramAligner: Failed to parse output"
                   << endl;
            errorText = tr("Failed to parse output of program: %1")
                .arg(reader.getError());
            alignmentModel->setError(errorText);
            goto done;
        }

        //!!! to use ById?
        
        Model *csvOutput = reader.load();

        SparseTimeValueModel *path =
            qobject_cast<SparseTimeValueModel *>(csvOutput);
        if (!path) {
            SVCERR << "ERROR: ExternalProgramAligner: Output did not convert to sparse time-value model"
                   << endl;
            errorText =
                tr("Output of alignment program was not in the proper format");
            alignmentModel->setError(errorText);
            delete csvOutput;
            goto done;
        }
                       
        if (path->isEmpty()) {
            SVCERR << "ERROR: ExternalProgramAligner: Output contained no mappings"
                   << endl;
            errorText = 
                tr("Output of alignment program contained no mappings");
            alignmentModel->setError(errorText);
            delete path;
            goto done;
        }

        SVCERR << "ExternalProgramAligner: Setting alignment path ("
             << path->getEventCount() << " point(s))" << endl;

        auto pathId =
            ModelById::add(std::shared_ptr<SparseTimeValueModel>(path));
        alignmentModel->setPathFrom(pathId);
        alignmentModel->setCompletion(100);

        ModelById::release(pathId);
        
    } else {
        SVCERR << "ERROR: ExternalProgramAligner: Aligner program "
               << "failed: exit code " << exitCode << ", status " << status
               << endl;
        errorText = tr("Aligner process returned non-zero exit status");
        alignmentModel->setError(errorText);
    }

done:
    delete m_process;
    m_process = nullptr;

    // "This should be emitted as the last thing the aligner does, as
    // the recipient may delete the aligner during the call."
    if (errorText == "") {
        emit complete(m_alignmentModel);
    } else {
        emit failed(m_toAlign, errorText);
    }
}
} // end namespace sv

