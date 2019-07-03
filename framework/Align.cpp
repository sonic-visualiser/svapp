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
Align::alignModel(Document *doc, ModelId ref, ModelId other, QString &error)
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
    if (performPitchCompensation) {
        id = settings.value
            ("tuning-difference-transform-id",
             "vamp:tuning-difference:tuning-difference:tuningfreq")
            .toString();
    }
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
Align::alignModelViaTransform(Document *doc, ModelId ref, ModelId other,
                              QString &error)
{
    QMutexLocker locker (&m_mutex);

    auto reference = ModelById::getAs<RangeSummarisableTimeValueModel>(ref);
    auto rm = ModelById::getAs<RangeSummarisableTimeValueModel>(other);
    if (!reference || !rm) return false;
   
    // This involves creating either three or four new models:
    //
    // 1. an AggregateWaveModel to provide the mixdowns of the main
    // model and the new model in its two channels, as input to the
    // MATCH plugin
    //
    // 2a. a SparseTimeValueModel which will be automatically created
    // by FeatureExtractionModelTransformer when running the
    // TuningDifference plugin to receive the relative tuning of the
    // second model (if pitch-aware alignment is enabled in the
    // preferences)
    //
    // 2b. a SparseTimeValueModel which will be automatically created
    // by FeatureExtractionPluginTransformer when running the MATCH
    // plugin to perform alignment (so containing the alignment path)
    //
    // 3. an AlignmentModel, which stores the path model and carries
    // out alignment lookups on it.
    //
    // The AggregateWaveModel [1] is registered with the document,
    // which deletes it when it is invalidated (when one of its
    // components is deleted). The SparseTimeValueModel [2a] is reused
    // by us when starting the alignment process proper, and is then
    // deleted by us. The SparseTimeValueModel [2b] is passed to the
    // AlignmentModel, which takes ownership of it. The AlignmentModel
    // is attached to the new model we are aligning, which also takes
    // ownership of it. The only one of these models that we need to
    // delete here is the SparseTimeValueModel [2a].
    //!!! todo: review the above, especially management of AlignmentModel
    //
    // (We also create a sneaky additional SparseTimeValueModel
    // temporarily so we can attach completion information to it -
    // this is quite unnecessary from the perspective of simply
    // producing the results.)

    AggregateWaveModel::ChannelSpecList components;

    components.push_back(AggregateWaveModel::ModelChannelSpec
                         (reference->getId(), -1));

    components.push_back(AggregateWaveModel::ModelChannelSpec
                         (rm->getId(), -1));

    auto aggregateModel = std::make_shared<AggregateWaveModel>(components);
    ModelById::add(aggregateModel);
    doc->addAggregateModel(aggregateModel->getId());

    auto alignmentModel = std::make_shared<AlignmentModel>(ref, other,
                                                           ModelId());
    ModelById::add(alignmentModel);

    TransformId tdId = getTuningDifferenceTransformName();

    if (tdId == "") {
        
        if (beginTransformDrivenAlignment(aggregateModel->getId(),
                                          alignmentModel->getId())) {
            rm->setAlignment(alignmentModel->getId());
        } else {
            error = alignmentModel->getError();
            ModelById::release(alignmentModel);
            return false;
        }

    } else {

        // Have a tuning-difference transform id, so run it
        // asynchronously first
        
        TransformFactory *tf = TransformFactory::getInstance();

        Transform transform = tf->getDefaultTransformFor
            (tdId, aggregateModel->getSampleRate());

        transform.setParameter("maxduration", 60);
        transform.setParameter("maxrange", 6);
        transform.setParameter("finetuning", false);
    
        SVDEBUG << "Align::alignModel: Tuning difference transform step size " << transform.getStepSize() << ", block size " << transform.getBlockSize() << endl;

        ModelTransformerFactory *mtf = ModelTransformerFactory::getInstance();

        QString message;
        ModelId transformOutput = mtf->transform(transform,
                                                 aggregateModel->getId(),
                                                 message);

        auto tdout = ModelById::getAs<SparseTimeValueModel>(transformOutput);
        if (!tdout) {
            SVCERR << "Align::alignModel: ERROR: Failed to create tuning-difference output model (no Tuning Difference plugin?)" << endl;
            error = message;
            return false;
        }

        rm->setAlignment(alignmentModel->getId());
    
        connect(tdout.get(), SIGNAL(completionChanged()),
                this, SLOT(tuningDifferenceCompletionChanged()));

        TuningDiffRec rec;
        rec.input = aggregateModel->getId();
        rec.alignment = alignmentModel->getId();
        
        // This model exists only so that the AlignmentModel can get a
        // completion value from somewhere while the tuning difference
        // calculation is going on
        auto preparatoryModel = std::make_shared<SparseTimeValueModel>
            (aggregateModel->getSampleRate(), 1);
        ModelById::add(preparatoryModel);
        preparatoryModel->setCompletion(0);
        rec.preparatory = preparatoryModel->getId();
        alignmentModel->setPathFrom(rec.preparatory);
        
        m_pendingTuningDiffs[transformOutput] = rec;
    }

    return true;
}

void
Align::tuningDifferenceCompletionChanged()
{
    QMutexLocker locker (&m_mutex);

    ModelId tdId;
    if (Model *modelPtr = qobject_cast<Model *>(sender())) {
        tdId = modelPtr->getId();
    } else {
        return;
    }

    if (m_pendingTuningDiffs.find(tdId) == m_pendingTuningDiffs.end()) {
        SVCERR << "ERROR: Align::tuningDifferenceCompletionChanged: Model "
               << tdId << " not found in pending tuning diff map!" << endl;
        return;
    }

    auto td = ModelById::getAs<SparseTimeValueModel>(tdId);
    if (!td) {
        SVCERR << "WARNING: Align::tuningDifferenceCompletionChanged: Model "
               << tdId << " not known as SparseTimeValueModel" << endl;
        return;
    }

    TuningDiffRec rec = m_pendingTuningDiffs[tdId];

    auto alignment = ModelById::getAs<AlignmentModel>(rec.alignment);
    if (!alignment) {
        SVCERR << "WARNING: Align::tuningDifferenceCompletionChanged:"
               << "alignment model has disappeared" << endl;
        return;
    }
    
    int completion = 0;
    bool done = td->isReady(&completion);

//    SVCERR << "Align::tuningDifferenceCompletionChanged: done = " << done << ", completion = " << completion << endl;

    if (!done) {
        // This will be the completion the alignment model reports,
        // before the alignment actually begins. It goes up from 0 to
        // 99 (not 100!) and then back to 0 again when we start
        // calculating the actual path in the following phase
        int clamped = (completion == 100 ? 99 : completion);
//        SVCERR << "Align::tuningDifferenceCompletionChanged: setting rec.preparatory completion to " << clamped << endl;
        auto preparatory = ModelById::getAs<SparseTimeValueModel>
            (rec.preparatory);
        if (preparatory) {
            preparatory->setCompletion(clamped);
        }
        return;
    }

    float tuningFrequency = 440.f;
    
    if (!td->isEmpty()) {
        tuningFrequency = td->getAllEvents()[0].getValue();
        SVCERR << "Align::tuningDifferenceCompletionChanged: Reported tuning frequency = " << tuningFrequency << endl;
    } else {
        SVCERR << "Align::tuningDifferenceCompletionChanged: No tuning frequency reported" << endl;
    }    

    m_pendingTuningDiffs.erase(tdId);
    ModelById::release(tdId);
    
    alignment->setPathFrom({});
    
    beginTransformDrivenAlignment
        (rec.input, rec.alignment, tuningFrequency);
}

bool
Align::beginTransformDrivenAlignment(ModelId aggregateModelId,
                                     ModelId alignmentModelId,
                                     float tuningFrequency)
{
    TransformId id = getAlignmentTransformName();
    
    TransformFactory *tf = TransformFactory::getInstance();

    auto aggregateModel = ModelById::getAs<AggregateWaveModel>(aggregateModelId);
    auto alignmentModel = ModelById::getAs<AlignmentModel>(alignmentModelId);

    if (!aggregateModel || !alignmentModel) {
        SVCERR << "Align::alignModel: ERROR: One or other of the aggregate & alignment models has disappeared" << endl;
        return false;
    }
    
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
    ModelId transformOutput = mtf->transform
        (transform, aggregateModelId, message);

    if (transformOutput.isNone()) {
        transform.setStepSize(0);
        transformOutput = mtf->transform
            (transform, aggregateModelId, message);
    }

    auto path = ModelById::getAs<SparseTimeValueModel>(transformOutput);

    //!!! callers will need to be updated to get error from
    //!!! alignment model after initial call
        
    if (!path) {
        SVCERR << "Align::alignModel: ERROR: Failed to create alignment path (no MATCH plugin?)" << endl;
        ModelById::release(transformOutput);
        alignmentModel->setError(message);
        return false;
    }

    path->setCompletion(0);
    alignmentModel->setPathFrom(transformOutput); //!!! who releases transformOutput?

    connect(alignmentModel.get(), SIGNAL(completionChanged()),
            this, SLOT(alignmentCompletionChanged()));

    return true;
}

void
Align::alignmentCompletionChanged()
{
    QMutexLocker locker (&m_mutex);

    if (AlignmentModel *amPtr = qobject_cast<AlignmentModel *>(sender())) {

        auto am = ModelById::getAs<AlignmentModel>(amPtr->getId());
        if (am && am->isReady()) {
            disconnect(am.get(), SIGNAL(completionChanged()),
                       this, SLOT(alignmentCompletionChanged()));
            emit alignmentComplete(am->getId());
        }
    }
}

bool
Align::alignModelViaProgram(Document *, ModelId ref, ModelId other,
                            QString program, QString &error)
{
    QMutexLocker locker (&m_mutex);

    auto reference = ModelById::getAs<RangeSummarisableTimeValueModel>(ref);
    auto rm = ModelById::getAs<RangeSummarisableTimeValueModel>(other);
    if (!reference || !rm) return false;

    while (!reference->isReady(nullptr) || !rm->isReady(nullptr)) {
        qApp->processEvents();
    }
    
    // Run an external program, passing to it paths to the main
    // model's audio file and the new model's audio file. It returns
    // the path in CSV form through stdout.

    auto roref = ModelById::getAs<ReadOnlyWaveFileModel>(ref);
    auto rorm = ModelById::getAs<ReadOnlyWaveFileModel>(other);
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

    auto alignmentModel = std::make_shared<AlignmentModel>(ref, other,
                                                           ModelId());
    ModelById::add(alignmentModel);
    rm->setAlignment(alignmentModel->getId());

    QProcess *process = new QProcess;
    QStringList args;
    args << refPath << otherPath;
    
    connect(process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(alignmentProgramFinished(int, QProcess::ExitStatus)));

    m_pendingProcesses[process] = alignmentModel->getId();
    process->start(program, args);

    bool success = process->waitForStarted();

    if (!success) {
        SVCERR << "ERROR: Align::alignModelViaProgram: Program did not start"
               << endl;
        error = "Alignment program could not be started";
        m_pendingProcesses.erase(process);
        //!!! who releases alignmentModel? does this? review
        rm->setAlignment({});
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

    ModelId alignmentModelId = m_pendingProcesses[process];
    auto alignmentModel = ModelById::getAs<AlignmentModel>(alignmentModelId);
    if (!alignmentModel) return;
    
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

        //!!! to use ById?
        
        Model *csvOutput = reader.load();

        SparseTimeValueModel *path = qobject_cast<SparseTimeValueModel *>(csvOutput);
        if (!path) {
            SVCERR << "ERROR: Align::alignmentProgramFinished: Output did not convert to sparse time-value model"
                   << endl;
            alignmentModel->setError
                ("Output of program did not produce sparse time-value model");
            delete csvOutput;
            goto done;
        }
                       
        if (path->isEmpty()) {
            SVCERR << "ERROR: Align::alignmentProgramFinished: Output contained no mappings"
                   << endl;
            alignmentModel->setError
                ("Output of alignment program contained no mappings");
            delete path;
            goto done;
        }

        SVCERR << "Align::alignmentProgramFinished: Setting alignment path ("
             << path->getEventCount() << " point(s))" << endl;

        ModelById::add(std::shared_ptr<SparseTimeValueModel>(path));
        alignmentModel->setPathFrom(path->getId());

        emit alignmentComplete(alignmentModelId);
        
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

