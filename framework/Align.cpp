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

#include "Align.h"
#include "Document.h"

#include "data/model/WaveFileModel.h"
#include "data/model/ReadOnlyWaveFileModel.h"
#include "data/model/AggregateWaveModel.h"
#include "data/model/RangeSummarisableTimeValueModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/AlignmentModel.h"

#include "data/fileio/CSVFileReader.h"

#include "transform/TransformFactory.h"
#include "transform/ModelTransformerFactory.h"
#include "transform/FeatureExtractionModelTransformer.h"

#include <QProcess>
#include <QSettings>
#include <QApplication>

bool
Align::alignModel(Document *doc, Model *ref, Model *other, QString &error)
{
    QSettings settings;
    settings.beginGroup("Preferences");
    bool useProgram = settings.value("use-external-alignment", false).toBool();
    QString program = settings.value("external-alignment-program", "").toString();
    settings.endGroup();

    if (useProgram && (program != "")) {
        return alignModelViaProgram(doc, ref, other, program, error);
    } else {
        return alignModelViaTransform(doc, ref, other, error);
    }
}

QString
Align::getAlignmentTransformName()
{
    QSettings settings;
    settings.beginGroup("Alignment");
    TransformId id =
        settings.value("transform-id",
                       "vamp:match-vamp-plugin:match:path").toString();
    settings.endGroup();
    return id;
}

QString
Align::getTuningDifferenceTransformName()
{
    QSettings settings;
    settings.beginGroup("Alignment");
    bool performPitchCompensation =
        settings.value("align-pitch-aware", false).toBool();
    QString id = "";
//!!!    if (performPitchCompensation) {
        id = settings.value
            ("tuning-difference-transform-id",
             "vamp:tuning-difference:tuning-difference:tuningfreq")
            .toString();
//    }
    settings.endGroup();
    return id;
}

bool
Align::canAlign() 
{
    TransformFactory *factory = TransformFactory::getInstance();
    TransformId id = getAlignmentTransformName();
    TransformId tdId = getTuningDifferenceTransformName();
    return factory->haveTransform(id) &&
        (tdId == "" || factory->haveTransform(tdId));
}

bool
Align::alignModelViaTransform(Document *doc, Model *ref, Model *other,
                              QString &error)
{
    QMutexLocker locker (&m_mutex);
    
    RangeSummarisableTimeValueModel *reference = qobject_cast
        <RangeSummarisableTimeValueModel *>(ref);
    
    RangeSummarisableTimeValueModel *rm = qobject_cast
        <RangeSummarisableTimeValueModel *>(other);

    if (!reference || !rm) return false; // but this should have been tested already
   
    // This involves creating either three or four new models:

    // 1. an AggregateWaveModel to provide the mixdowns of the main
    // model and the new model in its two channels, as input to the
    // MATCH plugin

    // 2a. a SparseTimeValueModel which will be automatically created
    // by FeatureExtractionModelTransformer when running the
    // TuningDifference plugin to receive the relative tuning of the
    // second model (if pitch-aware alignment is enabled in the
    // preferences)
    
    // 2b. a SparseTimeValueModel which will be automatically created
    // by FeatureExtractionPluginTransformer when running the MATCH
    // plugin to perform alignment (so containing the alignment path)

    // 3. an AlignmentModel, which stores the path model and carries
    // out alignment lookups on it.

    // The AggregateWaveModel [1] is registered with the document,
    // which deletes it when it is invalidated (when one of its
    // components is deleted). The SparseTimeValueModel [2a] is reused
    // by us when starting the alignment process proper, and is then
    // deleted by us. The SparseTimeValueModel [2b] is passed to the
    // AlignmentModel, which takes ownership of it. The AlignmentModel
    // is attached to the new model we are aligning, which also takes
    // ownership of it. The only one of these models that we need to
    // delete here is the SparseTimeValueModel [2a].

    AggregateWaveModel::ChannelSpecList components;

    components.push_back(AggregateWaveModel::ModelChannelSpec
                         (reference, -1));

    components.push_back(AggregateWaveModel::ModelChannelSpec
                         (rm, -1));

    AggregateWaveModel *aggregateModel = new AggregateWaveModel(components);
    doc->addAggregateModel(aggregateModel);

    AlignmentModel *alignmentModel =
        new AlignmentModel(reference, other, nullptr);

    connect(alignmentModel, SIGNAL(completionChanged()),
            this, SLOT(alignmentCompletionChanged()));

    TransformId tdId = getTuningDifferenceTransformName();

    if (tdId == "") {
        
        if (beginTransformDrivenAlignment(aggregateModel, alignmentModel)) {
            rm->setAlignment(alignmentModel);
        } else {
            error = alignmentModel->getError();
            delete alignmentModel;
            return false;
        }

    } else {

        // Have a tuning-difference transform id, so run it
        // asynchronously first
        
        TransformFactory *tf = TransformFactory::getInstance();

        Transform transform = tf->getDefaultTransformFor
            (tdId, aggregateModel->getSampleRate());

        SVDEBUG << "Align::alignModel: Tuning difference transform step size " << transform.getStepSize() << ", block size " << transform.getBlockSize() << endl;

        ModelTransformerFactory *mtf = ModelTransformerFactory::getInstance();

        QString message;
        Model *transformOutput = mtf->transform(transform, aggregateModel, message);

        SparseTimeValueModel *tdout = dynamic_cast<SparseTimeValueModel *>
            (transformOutput);
        
        if (!tdout) {
            SVCERR << "Align::alignModel: ERROR: Failed to create tuning-difference output model (no Tuning Difference plugin?)" << endl;
            delete tdout;
            error = message;
            return false;
        }

        rm->setAlignment(alignmentModel);
    
        connect(tdout, SIGNAL(completionChanged()),
                this, SLOT(tuningDifferenceCompletionChanged()));

        m_pendingTuningDiffs[tdout] =
            std::pair<AggregateWaveModel *, AlignmentModel *>
            (aggregateModel, alignmentModel);
    }

    return true;
}

bool
Align::beginTransformDrivenAlignment(AggregateWaveModel *aggregateModel,
                                     AlignmentModel *alignmentModel,
                                     float tuningFrequency)
{
    TransformId id = getAlignmentTransformName();
    
    TransformFactory *tf = TransformFactory::getInstance();

    Transform transform = tf->getDefaultTransformFor
        (id, aggregateModel->getSampleRate());

    transform.setStepSize(transform.getBlockSize()/2);
    transform.setParameter("serialise", 1);
    transform.setParameter("smooth", 0);

    if (tuningFrequency != 0.f) {
        transform.setParameter("freq2", tuningFrequency);
    }

    SVDEBUG << "Align::alignModel: Alignment transform step size " << transform.getStepSize() << ", block size " << transform.getBlockSize() << endl;

    ModelTransformerFactory *mtf = ModelTransformerFactory::getInstance();

    QString message;
    Model *transformOutput = mtf->transform
        (transform, aggregateModel, message);

    if (!transformOutput) {
        transform.setStepSize(0);
        transformOutput = mtf->transform
            (transform, aggregateModel, message);
    }

    SparseTimeValueModel *path = dynamic_cast<SparseTimeValueModel *>
        (transformOutput);

    //!!! callers will need to be updated to get error from
    //!!! alignment model after initial call
        
    if (!path) {
        SVCERR << "Align::alignModel: ERROR: Failed to create alignment path (no MATCH plugin?)" << endl;
        delete transformOutput;
        alignmentModel->setError(message);
        return false;
    }

    path->setCompletion(0);
    alignmentModel->setPathFrom(path);

    connect(alignmentModel, SIGNAL(completionChanged()),
            this, SLOT(alignmentCompletionChanged()));

    return true;
}

void
Align::tuningDifferenceCompletionChanged()
{
    QMutexLocker locker (&m_mutex);
    
    SparseTimeValueModel *td = qobject_cast<SparseTimeValueModel *>(sender());
    if (!td) return;
    if (!td->isReady()) return;
    
    disconnect(td, SIGNAL(completionChanged()),
               this, SLOT(alignmentCompletionChanged()));

    if (m_pendingTuningDiffs.find(td) == m_pendingTuningDiffs.end()) {
        SVCERR << "ERROR: Align::tuningDifferenceCompletionChanged: Model "
               << td << " not found in pending tuning diff map!" << endl;
        return;
    }

    std::pair<AggregateWaveModel *, AlignmentModel *> models =
        m_pendingTuningDiffs[td];

    float tuningFrequency = 440.f;
    
    if (!td->isEmpty()) {
        tuningFrequency = td->getAllEvents()[0].getValue();
        SVCERR << "Align::tuningDifferenceCompletionChanged: Reported tuning frequency = " << tuningFrequency << endl;
    } else {
        SVCERR << "Align::tuningDifferenceCompletionChanged: No tuning frequency reported" << endl;
    }    

    m_pendingTuningDiffs.erase(td);
    
    beginTransformDrivenAlignment
        (models.first, models.second, tuningFrequency);
}

void
Align::alignmentCompletionChanged()
{
    QMutexLocker locker (&m_mutex);
    
    AlignmentModel *am = qobject_cast<AlignmentModel *>(sender());
    if (!am) return;
    if (am->isReady()) {
        disconnect(am, SIGNAL(completionChanged()),
                   this, SLOT(alignmentCompletionChanged()));
        emit alignmentComplete(am);
    }
}

bool
Align::alignModelViaProgram(Document *, Model *ref, Model *other,
                            QString program, QString &error)
{
    QMutexLocker locker (&m_mutex);
    
    WaveFileModel *reference = qobject_cast<WaveFileModel *>(ref);
    WaveFileModel *rm = qobject_cast<WaveFileModel *>(other);

    if (!reference || !rm) {
        return false; // but this should have been tested already
    }

    while (!reference->isReady(nullptr) || !rm->isReady(nullptr)) {
        qApp->processEvents();
    }
    
    // Run an external program, passing to it paths to the main
    // model's audio file and the new model's audio file. It returns
    // the path in CSV form through stdout.

    ReadOnlyWaveFileModel *roref = qobject_cast<ReadOnlyWaveFileModel *>(reference);
    ReadOnlyWaveFileModel *rorm = qobject_cast<ReadOnlyWaveFileModel *>(rm);
    if (!roref || !rorm) {
        SVCERR << "ERROR: Align::alignModelViaProgram: Can't align non-read-only models via program (no local filename available)" << endl;
        return false;
    }
    
    QString refPath = roref->getLocalFilename();
    QString otherPath = rorm->getLocalFilename();

    if (refPath == "" || otherPath == "") {
        error = "Failed to find local filepath for wave-file model";
        return false;
    }

    AlignmentModel *alignmentModel =
        new AlignmentModel(reference, other, nullptr);
    rm->setAlignment(alignmentModel);

    QProcess *process = new QProcess;
    QStringList args;
    args << refPath << otherPath;
    
    connect(process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(alignmentProgramFinished(int, QProcess::ExitStatus)));

    m_pendingProcesses[process] = alignmentModel;
    process->start(program, args);

    bool success = process->waitForStarted();

    if (!success) {
        SVCERR << "ERROR: Align::alignModelViaProgram: Program did not start"
               << endl;
        error = "Alignment program could not be started";
        m_pendingProcesses.erase(process);
        rm->setAlignment(nullptr); // deletes alignmentModel as well
        delete process;
    }

    return success;
}

void
Align::alignmentProgramFinished(int exitCode, QProcess::ExitStatus status)
{
    QMutexLocker locker (&m_mutex);
    
    SVCERR << "Align::alignmentProgramFinished" << endl;
    
    QProcess *process = qobject_cast<QProcess *>(sender());

    if (m_pendingProcesses.find(process) == m_pendingProcesses.end()) {
        SVCERR << "ERROR: Align::alignmentProgramFinished: Process " << process
               << " not found in process model map!" << endl;
        return;
    }

    AlignmentModel *alignmentModel = m_pendingProcesses[process];
    
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
            SVCERR << "ERROR: Align::alignmentProgramFinished: Failed to parse output"
                   << endl;
            alignmentModel->setError
                (QString("Failed to parse output of program: %1")
                 .arg(reader.getError()));
            goto done;
        }

        Model *csvOutput = reader.load();

        SparseTimeValueModel *path = qobject_cast<SparseTimeValueModel *>(csvOutput);
        if (!path) {
            SVCERR << "ERROR: Align::alignmentProgramFinished: Output did not convert to sparse time-value model"
                   << endl;
            alignmentModel->setError
                ("Output of program did not produce sparse time-value model");
            goto done;
        }

        if (path->isEmpty()) {
            SVCERR << "ERROR: Align::alignmentProgramFinished: Output contained no mappings"
                   << endl;
            alignmentModel->setError
                ("Output of alignment program contained no mappings");
            goto done;
        }

        SVCERR << "Align::alignmentProgramFinished: Setting alignment path ("
             << path->getEventCount() << " point(s))" << endl;

        alignmentModel->setPathFrom(path);

        emit alignmentComplete(alignmentModel);
        
    } else {
        SVCERR << "ERROR: Align::alignmentProgramFinished: Aligner program "
               << "failed: exit code " << exitCode << ", status " << status
               << endl;
        alignmentModel->setError
            ("Aligner process returned non-zero exit status");
    }

done:
    m_pendingProcesses.erase(process);
    delete process;
}

