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
#include "data/model/AggregateWaveModel.h"
#include "data/model/RangeSummarisableTimeValueModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/AlignmentModel.h"

#include "data/fileio/CSVFileReader.h"

#include "transform/TransformFactory.h"
#include "transform/ModelTransformerFactory.h"
#include "transform/FeatureExtractionModelTransformer.h"

#include <QProcess>

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

    TransformId id = "vamp:match-vamp-plugin:match:path"; //!!! configure
    
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

    rm->setAlignment(alignmentModel);

    return true;
}

bool
Align::alignModelViaProgram(Model *ref, Model *other)
{
    WaveFileModel *reference = qobject_cast<WaveFileModel *>(ref);
    WaveFileModel *rm = qobject_cast<WaveFileModel *>(other);

    if (!rm) return false; // but this should have been tested already

    // Run an external program, passing to it paths to the main
    // model's audio file and the new model's audio file. It returns
    // the path in CSV form through stdout.

    QString refPath = reference->getLocalFilename();
    QString otherPath = rm->getLocalFilename();

    if (refPath == "" || otherPath == "") {
	m_error = "Failed to find local filepath for wave-file model";
	return false;
    }

    QProcess process;
    QString program = "/home/cannam/code/tido-audio/aligner/vect-align.sh";
    QStringList args;
    args << refPath << otherPath;
    process.start(program, args);

    process.waitForFinished(60000); //!!! nb timeout, but we can do better than blocking anyway

    if (process.exitStatus() == 0) {

	CSVFormat format;
	format.setModelType(CSVFormat::TwoDimensionalModel);
	format.setTimingType(CSVFormat::ExplicitTiming);
	format.setTimeUnits(CSVFormat::TimeSeconds);
	format.setColumnCount(2);
	format.setColumnPurpose(0, CSVFormat::ColumnStartTime);
	format.setColumnPurpose(1, CSVFormat::ColumnValue);
	format.setAllowQuoting(false);
	format.setSeparator(',');

	CSVFileReader reader(&process, format, reference->getSampleRate());
	if (!reader.isOK()) {
	    m_error = QString("Failed to parse output of program: %1")
		.arg(reader.getError());
	    return false;
	}

	Model *csvOutput = reader.load();

	SparseTimeValueModel *path = qobject_cast<SparseTimeValueModel *>(csvOutput);
	if (!path) {
	    m_error = QString("Output of program did not produce sparse time-value model");
	    return false;
	}

	if (path->getPoints().empty()) {
	    m_error = QString("Output of alignment program contained no mappings");
	    return false;
	}
	
	AlignmentModel *alignmentModel = new AlignmentModel
	    (reference, other, 0, path);

	rm->setAlignment(alignmentModel);

    } else {
	m_error = "Aligner process returned non-zero exit status";
	return false;
    }

    cerr << "Align: success" << endl;
    
    return true;
}

