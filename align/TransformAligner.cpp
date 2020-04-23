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

#include "TransformAligner.h"

#include "data/model/SparseTimeValueModel.h"
#include "data/model/RangeSummarisableTimeValueModel.h"
#include "data/model/AlignmentModel.h"
#include "data/model/AggregateWaveModel.h"

#include "framework/Document.h"

#include "transform/TransformFactory.h"
#include "transform/ModelTransformerFactory.h"
#include "transform/FeatureExtractionModelTransformer.h"

#include <QSettings>

TransformAligner::TransformAligner(Document *doc,
                                   ModelId reference,
                                   ModelId toAlign) :
    m_document(doc),
    m_reference(reference),
    m_toAlign(toAlign),
    m_tuningFrequency(440.f),
    m_incomplete(true)
{
}

TransformAligner::~TransformAligner()
{
    if (m_incomplete) {
        auto other =
            ModelById::getAs<RangeSummarisableTimeValueModel>(m_toAlign);
        if (other) {
            other->setAlignment({});
        }
    }

    ModelById::release(m_tuningDiffProgressModel);
    ModelById::release(m_tuningDiffOutputModel);
    ModelById::release(m_pathOutputModel);
}

QString
TransformAligner::getAlignmentTransformName()
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
TransformAligner::getTuningDifferenceTransformName()
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
TransformAligner::isAvailable()
{
    TransformFactory *factory = TransformFactory::getInstance();
    TransformId id = getAlignmentTransformName();
    TransformId tdId = getTuningDifferenceTransformName();
    return factory->haveTransform(id) &&
        (tdId == "" || factory->haveTransform(tdId));
}

bool
TransformAligner::begin(QString &error)
{
    auto reference =
        ModelById::getAs<RangeSummarisableTimeValueModel>(m_reference);
    auto other =
        ModelById::getAs<RangeSummarisableTimeValueModel>(m_toAlign);

    if (!reference || !other) return false;

    // This involves creating a number of new models:
    //
    // 1. an AggregateWaveModel to provide the mixdowns of the main
    // model and the new model in its two channels, as input to the
    // MATCH plugin. We just call this one aggregateModel
    //
    // 2a. a SparseTimeValueModel which will be automatically created
    // by FeatureExtractionModelTransformer when running the
    // TuningDifference plugin to receive the relative tuning of the
    // second model (if pitch-aware alignment is enabled in the
    // preferences). This is m_tuningDiffOutputModel.
    //
    // 2b. a SparseTimeValueModel which will be automatically created
    // by FeatureExtractionPluginTransformer when running the MATCH
    // plugin to perform alignment (so containing the alignment path).
    // This is m_pathOutputModel.
    //
    // 2c. a SparseTimeValueModel used solely to provide faked
    // completion information to the AlignmentModel while a
    // TuningDifference calculation is going on. We call this
    // m_tuningDiffProgressModel.
    //
    // 3. an AlignmentModel, which stores the path and carries out
    // alignment lookups on it. This one is m_alignmentModel.
    //
    // Models 1 and 3 are registered with the document, which will
    // eventually release them. We don't release them here except in
    // the case where an activity fails before the point where we
    // would otherwise have registered them with the document.
    //
    // Models 2a (m_tuningDiffOutputModel), 2b (m_pathOutputModel) and
    // 2c (m_tuningDiffProgressModel) are not registered with the
    // document, because they are not intended to persist, and also
    // Model 2c (m_tuningDiffProgressModel) is a bodge that we are
    // embarrassed about, so we try to manage it ourselves without
    // anyone else noticing. These have to be released by us when
    // finished with, but their lifespans do not extend beyond the end
    // of the alignment procedure, so this should be ok.

    AggregateWaveModel::ChannelSpecList components;
    components.push_back
        (AggregateWaveModel::ModelChannelSpec(m_reference, -1));

    components.push_back
        (AggregateWaveModel::ModelChannelSpec(m_toAlign, -1));

    auto aggregateModel = std::make_shared<AggregateWaveModel>(components);
    m_aggregateModel = ModelById::add(aggregateModel);
    m_document->addNonDerivedModel(m_aggregateModel);

    auto alignmentModel = std::make_shared<AlignmentModel>
        (m_reference, m_toAlign, ModelId());
    m_alignmentModel = ModelById::add(alignmentModel);

    TransformId tdId = getTuningDifferenceTransformName();

    if (tdId == "") {
        
        if (beginAlignmentPhase()) {
            other->setAlignment(m_alignmentModel);
            m_document->addNonDerivedModel(m_alignmentModel);
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
    
        SVDEBUG << "TransformAligner: Tuning difference transform step size " << transform.getStepSize() << ", block size " << transform.getBlockSize() << endl;

        ModelTransformerFactory *mtf = ModelTransformerFactory::getInstance();

        QString message;
        ModelId tuningDiffOutputModelId = mtf->transform(transform,
                                                         m_aggregateModel,
                                                         message);

        auto tuningDiffOutputModel =
            ModelById::getAs<SparseTimeValueModel>(tuningDiffOutputModelId);
        if (!tuningDiffOutputModel) {
            SVCERR << "Align::alignModel: ERROR: Failed to create tuning-difference output model (no Tuning Difference plugin?)" << endl;
            error = message;
            ModelById::release(alignmentModel);
            return false;
        }

        other->setAlignment(m_alignmentModel);
        m_document->addNonDerivedModel(m_alignmentModel);
    
        connect(tuningDiffOutputModel.get(),
                SIGNAL(completionChanged(ModelId)),
                this, SLOT(tuningDifferenceCompletionChanged(ModelId)));

        // This model exists only so that the AlignmentModel can get a
        // completion value from somewhere while the tuning difference
        // calculation is going on
        auto progressModel = std::make_shared<SparseTimeValueModel>
            (aggregateModel->getSampleRate(), 1);
        m_tuningDiffProgressModel = ModelById::add(progressModel);
        progressModel->setCompletion(0);
        alignmentModel->setPathFrom(m_tuningDiffProgressModel);
    }

    return true;
}

void
TransformAligner::tuningDifferenceCompletionChanged(ModelId tuningDiffOutputModelId)
{
    if (tuningDiffOutputModelId != m_tuningDiffOutputModel) {
        SVCERR << "WARNING: TransformAligner::tuningDifferenceCompletionChanged: Model "
                << tuningDiffOutputModelId
                << " is not ours!" << endl;
        return;
    }

    auto tuningDiffOutputModel =
        ModelById::getAs<SparseTimeValueModel>(m_tuningDiffOutputModel);
    if (!tuningDiffOutputModel) {
        SVCERR << "WARNING: TransformAligner::tuningDifferenceCompletionChanged: Model "
               << tuningDiffOutputModelId
               << " not known as SparseTimeValueModel" << endl;
        return;
    }

    auto alignmentModel = ModelById::getAs<AlignmentModel>(m_alignmentModel);
    if (!alignmentModel) {
        SVCERR << "WARNING: TransformAligner::tuningDifferenceCompletionChanged:"
               << "alignment model has disappeared" << endl;
        return;
    }
    
    int completion = 0;
    bool done = tuningDiffOutputModel->isReady(&completion);

    if (!done) {
        // This will be the completion the alignment model reports,
        // before the alignment actually begins. It goes up from 0 to
        // 99 (not 100!) and then back to 0 again when we start
        // calculating the actual path in the following phase
        int clamped = (completion == 100 ? 99 : completion);
        auto progressModel =
            ModelById::getAs<SparseTimeValueModel>(m_tuningDiffProgressModel);
        if (progressModel) {
            progressModel->setCompletion(clamped);
        }
        return;
    }

    m_tuningFrequency = 440.f;
    
    if (!tuningDiffOutputModel->isEmpty()) {
        m_tuningFrequency = tuningDiffOutputModel->getAllEvents()[0].getValue();
        SVCERR << "TransformAligner::tuningDifferenceCompletionChanged: Reported tuning frequency = " << m_tuningFrequency << endl;
    } else {
        SVCERR << "TransformAligner::tuningDifferenceCompletionChanged: No tuning frequency reported" << endl;
    }    
    
    ModelById::release(tuningDiffOutputModel);
    m_tuningDiffOutputModel = {};
    
    alignmentModel->setPathFrom({}); // replace m_tuningDiffProgressModel
    ModelById::release(m_tuningDiffProgressModel);
    m_tuningDiffProgressModel = {};

    beginAlignmentPhase();
}

bool
TransformAligner::beginAlignmentPhase()
{
    TransformId id = getAlignmentTransformName();
    
    TransformFactory *tf = TransformFactory::getInstance();

    auto aggregateModel =
        ModelById::getAs<AggregateWaveModel>(m_aggregateModel);
    auto alignmentModel =
        ModelById::getAs<AlignmentModel>(m_alignmentModel);

    if (!aggregateModel || !alignmentModel) {
        SVCERR << "TransformAligner::alignModel: ERROR: One or other of the aggregate & alignment models has disappeared" << endl;
        return false;
    }
    
    Transform transform = tf->getDefaultTransformFor
        (id, aggregateModel->getSampleRate());

    transform.setStepSize(transform.getBlockSize()/2);
    transform.setParameter("serialise", 1);
    transform.setParameter("smooth", 0);
    transform.setParameter("zonewidth", 40);
    transform.setParameter("noise", true);
    transform.setParameter("minfreq", 500);

    int cents = 0;
    
    if (m_tuningFrequency != 0.f) {
        transform.setParameter("freq2", m_tuningFrequency);

        double centsOffset = 0.f;
        int pitch = Pitch::getPitchForFrequency(m_tuningFrequency,
                                                &centsOffset);
        cents = int(round((pitch - 69) * 100 + centsOffset));
        SVCERR << "TransformAligner: frequency " << m_tuningFrequency
               << " yields cents offset " << centsOffset
               << " and pitch " << pitch << " -> cents " << cents << endl;
    }

    alignmentModel->setRelativePitch(cents);
    
    SVDEBUG << "Align::alignModel: Alignment transform step size "
            << transform.getStepSize() << ", block size "
            << transform.getBlockSize() << endl;

    ModelTransformerFactory *mtf = ModelTransformerFactory::getInstance();

    QString message;
    ModelId pathOutputModelId = mtf->transform
        (transform, m_aggregateModel, message);

    if (pathOutputModelId.isNone()) {
        transform.setStepSize(0);
        pathOutputModelId = mtf->transform
            (transform, m_aggregateModel, message);
    }

    auto pathOutputModel =
        ModelById::getAs<SparseTimeValueModel>(pathOutputModelId);

    //!!! callers will need to be updated to get error from
    //!!! alignment model after initial call
        
    if (!pathOutputModel) {
        SVCERR << "TransformAligner: ERROR: Failed to create alignment path (no MATCH plugin?)" << endl;
        alignmentModel->setError(message);
        return false;
    }

    pathOutputModel->setCompletion(0);
    alignmentModel->setPathFrom(m_pathOutputModel);

    connect(alignmentModel.get(), SIGNAL(completionChanged(ModelId)),
            this, SLOT(alignmentCompletionChanged(ModelId)));

    return true;
}

void
TransformAligner::alignmentCompletionChanged(ModelId alignmentModelId)
{
    if (alignmentModelId != m_alignmentModel) {
        SVCERR << "WARNING: TransformAligner::alignmentCompletionChanged: Model "
                << alignmentModelId
                << " is not ours!" << endl;
        return;
    }

    auto alignmentModel = ModelById::getAs<AlignmentModel>(m_alignmentModel);

    if (alignmentModel && alignmentModel->isReady()) {

        m_incomplete = false;
        
        ModelById::release(m_pathOutputModel);
        m_pathOutputModel = {};

        disconnect(alignmentModel.get(),
                   SIGNAL(completionChanged(ModelId)),
                   this, SLOT(alignmentCompletionChanged(ModelId)));
        emit complete(m_alignmentModel);
    }
}
