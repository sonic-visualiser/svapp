/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "Align.h"

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
Align::alignModel(Model *ref, Model *other)
{
    QSettings settings;
    settings.beginGroup("Preferences");
    bool useProgram = settings.value("use-external-alignment", false).toBool();
    QString program = settings.value("external-alignment-program", "").toString();
    settings.endGroup();

    if (useProgram && (program != "")) {
        return alignModelViaProgram(ref, other, program);
    } else {
        return alignModelViaTransform(ref, other);
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

bool
Align::canAlign() 
{
    TransformId id = getAlignmentTransformName();
    TransformFactory *factory = TransformFactory::getInstance();
    return factory->haveTransform(id);
}

bool
Align::alignModelViaTransform(Model *ref, Model *other)
{
    RangeSummarisableTimeValueModel *reference = qobject_cast
        <RangeSummarisableTimeValueModel *>(ref);
    
    RangeSummarisableTimeValueModel *rm = qobject_cast
        <RangeSummarisableTimeValueModel *>(other);

    if (!reference || !rm) return false; // but this should have been tested already
   
    // This involves creating three new models:

    // 1. an AggregateWaveModel to provide the mixdowns of the main
    // model and the new model in its two channels, as input to the
    // MATCH plugin

    // 2. a SparseTimeValueModel, which is the model automatically
    // created by FeatureExtractionPluginTransformer when running the
    // MATCH plugin (thus containing the alignment path)

    // 3. an AlignmentModel, which stores the path model and carries
    // out alignment lookups on it.

    // The first two of these are provided as arguments to the
    // constructor for the third, which takes responsibility for
    // deleting them.  The AlignmentModel, meanwhile, is passed to the
    // new model we are aligning, which also takes responsibility for
    // it.  We should not have to delete any of these new models here.

    AggregateWaveModel::ChannelSpecList components;

    components.push_back(AggregateWaveModel::ModelChannelSpec
                         (reference, -1));

    components.push_back(AggregateWaveModel::ModelChannelSpec
                         (rm, -1));

    Model *aggregateModel = new AggregateWaveModel(components);
    ModelTransformer::Input aggregate(aggregateModel);

    TransformId id = getAlignmentTransformName();
    
    TransformFactory *tf = TransformFactory::getInstance();

    Transform transform = tf->getDefaultTransformFor
        (id, aggregateModel->getSampleRate());

    transform.setStepSize(transform.getBlockSize()/2);
    transform.setParameter("serialise", 1);
    transform.setParameter("smooth", 0);

    SVDEBUG << "Align::alignModel: Alignment transform step size " << transform.getStepSize() << ", block size " << transform.getBlockSize() << endl;

    ModelTransformerFactory *mtf = ModelTransformerFactory::getInstance();

    QString message;
    Model *transformOutput = mtf->transform(transform, aggregate, message);

    if (!transformOutput) {
        transform.setStepSize(0);
        transformOutput = mtf->transform(transform, aggregate, message);
    }

    SparseTimeValueModel *path = dynamic_cast<SparseTimeValueModel *>
        (transformOutput);

    if (!path) {
        cerr << "Align::alignModel: ERROR: Failed to create alignment path (no MATCH plugin?)" << endl;
        delete transformOutput;
        delete aggregateModel;
	m_error = message;
        return false;
    }

    path->setCompletion(0);

    AlignmentModel *alignmentModel = new AlignmentModel
        (reference, other, aggregateModel, path);

    connect(alignmentModel, SIGNAL(completionChanged()),
            this, SLOT(alignmentCompletionChanged()));
    
    rm->setAlignment(alignmentModel);

    return true;
}

void
Align::alignmentCompletionChanged()
{
    AlignmentModel *am = qobject_cast<AlignmentModel *>(sender());
    if (!am) return;
    if (am->isReady()) {
        disconnect(am, SIGNAL(completionChanged()),
                   this, SLOT(alignmentCompletionChanged()));
        emit alignmentComplete(am);
    }
}

bool
Align::alignModelViaProgram(Model *ref, Model *other, QString program)
{
    WaveFileModel *reference = qobject_cast<WaveFileModel *>(ref);
    WaveFileModel *rm = qobject_cast<WaveFileModel *>(other);

    if (!reference || !rm) {
        return false; // but this should have been tested already
    }

    while (!reference->isReady(0) || !rm->isReady(0)) {
        qApp->processEvents();
    }
    
    // Run an external program, passing to it paths to the main
    // model's audio file and the new model's audio file. It returns
    // the path in CSV form through stdout.

    ReadOnlyWaveFileModel *roref = qobject_cast<ReadOnlyWaveFileModel *>(reference);
    ReadOnlyWaveFileModel *rorm = qobject_cast<ReadOnlyWaveFileModel *>(rm);
    if (!roref || !rorm) {
        cerr << "ERROR: Align::alignModelViaProgram: Can't align non-read-only models via program (no local filename available)" << endl;
        return false;
    }
    
    QString refPath = roref->getLocalFilename();
    QString otherPath = rorm->getLocalFilename();

    if (refPath == "" || otherPath == "") {
	m_error = "Failed to find local filepath for wave-file model";
	return false;
    }

    m_error = "";
    
    AlignmentModel *alignmentModel = new AlignmentModel(reference, other, 0, 0);
    rm->setAlignment(alignmentModel);

    QProcess *process = new QProcess;
    QStringList args;
    args << refPath << otherPath;
    
    connect(process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(alignmentProgramFinished(int, QProcess::ExitStatus)));

    m_processModels[process] = alignmentModel;
    process->start(program, args);

    bool success = process->waitForStarted();

    if (!success) {
        cerr << "ERROR: Align::alignModelViaProgram: Program did not start"
             << endl;
        m_error = "Alignment program could not be started";
        m_processModels.erase(process);
        rm->setAlignment(0); // deletes alignmentModel as well
        delete process;
    }

    return success;
}

void
Align::alignmentProgramFinished(int exitCode, QProcess::ExitStatus status)
{
    cerr << "Align::alignmentProgramFinished" << endl;
    
    QProcess *process = qobject_cast<QProcess *>(sender());

    if (m_processModels.find(process) == m_processModels.end()) {
        cerr << "ERROR: Align::alignmentProgramFinished: Process " << process
             << " not found in process model map!" << endl;
        return;
    }

    AlignmentModel *alignmentModel = m_processModels[process];
    
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
            cerr << "ERROR: Align::alignmentProgramFinished: Failed to parse output"
                 << endl;
	    m_error = QString("Failed to parse output of program: %1")
		.arg(reader.getError());
            goto done;
	}

	Model *csvOutput = reader.load();

	SparseTimeValueModel *path = qobject_cast<SparseTimeValueModel *>(csvOutput);
	if (!path) {
            cerr << "ERROR: Align::alignmentProgramFinished: Output did not convert to sparse time-value model"
                 << endl;
	    m_error = QString("Output of program did not produce sparse time-value model");
            goto done;
	}

	if (path->getPoints().empty()) {
            cerr << "ERROR: Align::alignmentProgramFinished: Output contained no mappings"
                 << endl;
	    m_error = QString("Output of alignment program contained no mappings");
            goto done;
	}

        cerr << "Align::alignmentProgramFinished: Setting alignment path ("
             << path->getPoints().size() << " point(s))" << endl;
        
        alignmentModel->setPathFrom(path);

        emit alignmentComplete(alignmentModel);
        
    } else {
        cerr << "ERROR: Align::alignmentProgramFinished: Aligner program "
             << "failed: exit code " << exitCode << ", status " << status
             << endl;
	m_error = "Aligner process returned non-zero exit status";
    }

done:
    m_processModels.erase(process);
    delete process;
}

