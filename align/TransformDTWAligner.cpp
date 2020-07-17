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

#include "TransformDTWAligner.h"
#include "DTW.h"

#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"
#include "data/model/RangeSummarisableTimeValueModel.h"
#include "data/model/AlignmentModel.h"
#include "data/model/AggregateWaveModel.h"

#include "framework/Document.h"

#include "transform/ModelTransformerFactory.h"
#include "transform/FeatureExtractionModelTransformer.h"

#include <QSettings>
#include <QMutex>
#include <QMutexLocker>

using std::vector;

static
TransformDTWAligner::MagnitudePreprocessor identityMagnitudePreprocessor =
    [](double x) {
        return x;
    };

static
TransformDTWAligner::RiseFallPreprocessor identityRiseFallPreprocessor =
    [](double prev, double curr) {
        if (prev == curr) {
            return RiseFallDTW::Value({ RiseFallDTW::Direction::None, 0.0 });
        } else if (curr > prev) {
            return RiseFallDTW::Value({ RiseFallDTW::Direction::Up, curr - prev });
        } else {
            return RiseFallDTW::Value({ RiseFallDTW::Direction::Down, prev - curr });
        }
    };

QMutex
TransformDTWAligner::m_dtwMutex;

TransformDTWAligner::TransformDTWAligner(Document *doc,
                                         ModelId reference,
                                         ModelId toAlign,
                                         bool subsequence,
                                         Transform transform,
                                         DTWType dtwType) :
    m_document(doc),
    m_reference(reference),
    m_toAlign(toAlign),
    m_transform(transform),
    m_dtwType(dtwType),
    m_subsequence(subsequence),
    m_incomplete(true),
    m_magnitudePreprocessor(identityMagnitudePreprocessor),
    m_riseFallPreprocessor(identityRiseFallPreprocessor)
{
}

TransformDTWAligner::TransformDTWAligner(Document *doc,
                                         ModelId reference,
                                         ModelId toAlign,
                                         bool subsequence,
                                         Transform transform,
                                         MagnitudePreprocessor outputPreprocessor) :
    m_document(doc),
    m_reference(reference),
    m_toAlign(toAlign),
    m_transform(transform),
    m_dtwType(Magnitude),
    m_subsequence(subsequence),
    m_incomplete(true),
    m_magnitudePreprocessor(outputPreprocessor),
    m_riseFallPreprocessor(identityRiseFallPreprocessor)
{
}

TransformDTWAligner::TransformDTWAligner(Document *doc,
                                         ModelId reference,
                                         ModelId toAlign,
                                         bool subsequence,
                                         Transform transform,
                                         RiseFallPreprocessor outputPreprocessor) :
    m_document(doc),
    m_reference(reference),
    m_toAlign(toAlign),
    m_transform(transform),
    m_dtwType(RiseFall),
    m_subsequence(subsequence),
    m_incomplete(true),
    m_magnitudePreprocessor(identityMagnitudePreprocessor),
    m_riseFallPreprocessor(outputPreprocessor)
{
}

TransformDTWAligner::~TransformDTWAligner()
{
    if (m_incomplete) {
        if (auto toAlign = ModelById::get(m_toAlign)) {
            toAlign->setAlignment({});
        }
    }
    
    ModelById::release(m_referenceOutputModel);
    ModelById::release(m_toAlignOutputModel);
}

bool
TransformDTWAligner::isAvailable()
{
    //!!! needs to be isAvailable(QString transformId)?
    return true;
}

void
TransformDTWAligner::begin()
{
    auto reference =
        ModelById::getAs<RangeSummarisableTimeValueModel>(m_reference);
    auto toAlign =
        ModelById::getAs<RangeSummarisableTimeValueModel>(m_toAlign);

    if (!reference || !toAlign) return;

    SVCERR << "TransformDTWAligner[" << this << "]: begin(): aligning "
           << m_toAlign << " against reference " << m_reference << endl;
    
    ModelTransformerFactory *mtf = ModelTransformerFactory::getInstance();

    QString message;

    m_referenceOutputModel = mtf->transform(m_transform, m_reference, message);
    auto referenceOutputModel = ModelById::get(m_referenceOutputModel);
    if (!referenceOutputModel) {
        SVCERR << "Align::alignModel: ERROR: Failed to create reference output model (no plugin?)" << endl;
        emit failed(m_toAlign, message);
        return;
    }

#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
    SVCERR << "TransformDTWAligner[" << this << "]: begin(): transform id "
           << m_transform.getIdentifier()
           << " is running on reference model" << endl;
#endif

    message = "";

    m_toAlignOutputModel = mtf->transform(m_transform, m_toAlign, message);
    auto toAlignOutputModel = ModelById::get(m_toAlignOutputModel);
    if (!toAlignOutputModel) {
        SVCERR << "Align::alignModel: ERROR: Failed to create toAlign output model (no plugin?)" << endl;
        emit failed(m_toAlign, message);
        return;
    }

#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
    SVCERR << "TransformDTWAligner[" << this << "]: begin(): transform id "
           << m_transform.getIdentifier()
           << " is running on toAlign model" << endl;
#endif

    connect(referenceOutputModel.get(), SIGNAL(completionChanged(ModelId)),
            this, SLOT(completionChanged(ModelId)));
    connect(toAlignOutputModel.get(), SIGNAL(completionChanged(ModelId)),
            this, SLOT(completionChanged(ModelId)));
    
    auto alignmentModel = std::make_shared<AlignmentModel>
        (m_reference, m_toAlign, ModelId());
    m_alignmentModel = ModelById::add(alignmentModel);
    
    toAlign->setAlignment(m_alignmentModel);
    m_document->addNonDerivedModel(m_alignmentModel);

    // we wouldn't normally expect these to be true here, but...
    int completion = 0;
    if (referenceOutputModel->isReady(&completion) &&
        toAlignOutputModel->isReady(&completion)) {
        SVCERR << "TransformDTWAligner[" << this << "]: begin(): output models "
               << "are ready already! calling performAlignment" << endl;
        if (performAlignment()) {
            emit complete(m_alignmentModel);
        } else {
            emit failed(m_toAlign, tr("Failed to calculate alignment using DTW"));
        }
    }
}

void
TransformDTWAligner::completionChanged(ModelId
#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
                                       id
#endif
    )
{
    if (!m_incomplete) {
        return;
    }
#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
    SVCERR << "TransformDTWAligner[" << this << "]: completionChanged: "
           << "model " << id << endl;
#endif
    
    auto referenceOutputModel = ModelById::get(m_referenceOutputModel);
    auto toAlignOutputModel = ModelById::get(m_toAlignOutputModel);
    auto alignmentModel = ModelById::getAs<AlignmentModel>(m_alignmentModel);

    if (!referenceOutputModel || !toAlignOutputModel || !alignmentModel) {
        return;
    }

    int referenceCompletion = 0, toAlignCompletion = 0;
    bool referenceReady = referenceOutputModel->isReady(&referenceCompletion);
    bool toAlignReady = toAlignOutputModel->isReady(&toAlignCompletion);

    if (referenceReady && toAlignReady) {

        SVCERR << "TransformDTWAligner[" << this << "]: completionChanged: "
               << "both models ready, calling performAlignment" << endl;

        alignmentModel->setCompletion(95);
        
        if (performAlignment()) {
            emit complete(m_alignmentModel);
        } else {
            emit failed(m_toAlign, tr("Alignment of transform outputs failed"));
        }

    } else {
#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
        SVCERR << "TransformDTWAligner[" << this << "]: completionChanged: "
               << "not ready yet: reference completion " << referenceCompletion
               << ", toAlign completion " << toAlignCompletion << endl;
#endif
        
        int completion = std::min(referenceCompletion,
                                  toAlignCompletion);
        completion = (completion * 94) / 100;
        alignmentModel->setCompletion(completion);
    }
}

bool
TransformDTWAligner::performAlignment()
{
    if (m_dtwType == Magnitude) {
        return performAlignmentMagnitude();
    } else {
        return performAlignmentRiseFall();
    }
}

bool
TransformDTWAligner::getValuesFrom(ModelId modelId,
                                   vector<sv_frame_t> &frames,
                                   vector<double> &values,
                                   sv_frame_t &resolution)
{
    EventVector events;

    if (auto model = ModelById::getAs<SparseTimeValueModel>(modelId)) {
        resolution = model->getResolution();
        events = model->getAllEvents();
    } else if (auto model = ModelById::getAs<NoteModel>(modelId)) {
        resolution = model->getResolution();
        events = model->getAllEvents();
    } else {
        SVCERR << "TransformDTWAligner::getValuesFrom: Type of model "
               << modelId << " is not supported" << endl;
        return false;
    }

    frames.clear();
    values.clear();

    for (auto e: events) {
        frames.push_back(e.getFrame());
        values.push_back(e.getValue());
    }

    return true;
}

Path
TransformDTWAligner::makePath(const vector<size_t> &alignment,
                              const vector<sv_frame_t> &refFrames,
                              const vector<sv_frame_t> &otherFrames,
                              sv_samplerate_t sampleRate,
                              sv_frame_t resolution)
{
    Path path(sampleRate, int(resolution));

    path.add(PathPoint(0, 0));
    
    for (int i = 0; in_range_for(alignment, i); ++i) {

        // DTW returns "the index into s1 for each element in s2"
        sv_frame_t alignedFrame = otherFrames[i];
        
        if (!in_range_for(refFrames, alignment[i])) {
            SVCERR << "TransformDTWAligner::makePath: Internal error: "
                   << "DTW maps index " << i << " in other frame vector "
                   << "(size " << otherFrames.size() << ") onto index "
                   << alignment[i] << " in ref frame vector "
                   << "(only size " << refFrames.size() << ")" << endl;
            continue;
        }
            
        sv_frame_t refFrame = refFrames[alignment[i]];
        path.add(PathPoint(alignedFrame, refFrame));
    }

    return path;
}

bool
TransformDTWAligner::performAlignmentMagnitude()
{
    auto alignmentModel = ModelById::getAs<AlignmentModel>(m_alignmentModel);
    if (!alignmentModel) {
        return false;
    }

    vector<sv_frame_t> refFrames, otherFrames;
    vector<double> refValues, otherValues;
    sv_frame_t resolution = 0;

    if (!getValuesFrom(m_referenceOutputModel,
                       refFrames, refValues, resolution)) {
        return false;
    }

    if (!getValuesFrom(m_toAlignOutputModel,
                       otherFrames, otherValues, resolution)) {
        return false;
    }
    
    vector<double> s1, s2;
    for (double v: refValues) {
        s1.push_back(m_magnitudePreprocessor(v));
    }
    for (double v: otherValues) {
        s2.push_back(m_magnitudePreprocessor(v));
    }

#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentMagnitude: "
           << "Have " << s1.size() << " events from reference, "
           << s2.size() << " from toAlign" << endl;
#endif
    
    MagnitudeDTW dtw;
    vector<size_t> alignment;

    {
#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
        SVCERR << "TransformDTWAligner[" << this
               << "]: serialising DTW to avoid over-allocation" << endl;
#endif
        QMutexLocker locker(&m_dtwMutex);
        if (m_subsequence) {
            alignment = dtw.alignSubsequence(s1, s2);
        } else {
            alignment = dtw.alignSequences(s1, s2);
        }
    }

#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentMagnitude: "
           << "DTW produced " << alignment.size() << " points:" << endl;
    for (int i = 0; in_range_for(alignment, i) && i < 100; ++i) {
        SVCERR << alignment[i] << " ";
    }
    SVCERR << endl;
#endif

    alignmentModel->setPath(makePath(alignment,
                                     refFrames,
                                     otherFrames,
                                     alignmentModel->getSampleRate(),
                                     resolution));
    alignmentModel->setCompletion(100);

    SVCERR << "TransformDTWAligner[" << this
           << "]: performAlignmentMagnitude: Done" << endl;

    m_incomplete = false;
    return true;
}

bool
TransformDTWAligner::performAlignmentRiseFall()
{
    auto alignmentModel = ModelById::getAs<AlignmentModel>(m_alignmentModel);
    if (!alignmentModel) {
        return false;
    }

    vector<sv_frame_t> refFrames, otherFrames;
    vector<double> refValues, otherValues;
    sv_frame_t resolution = 0;

    if (!getValuesFrom(m_referenceOutputModel,
                       refFrames, refValues, resolution)) {
        return false;
    }

    if (!getValuesFrom(m_toAlignOutputModel,
                       otherFrames, otherValues, resolution)) {
        return false;
    }
    
    auto preprocess =
        [this](const std::vector<double> &vv) {
            vector<RiseFallDTW::Value> s;
            double prev = 0.0;
            for (auto curr: vv) {
                s.push_back(m_riseFallPreprocessor(prev, curr));
                prev = curr;
            }
            return s;
        }; 
    
    vector<RiseFallDTW::Value> s1 = preprocess(refValues);
    vector<RiseFallDTW::Value> s2 = preprocess(otherValues);

#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentRiseFall: "
           << "Have " << s1.size() << " events from reference, "
           << s2.size() << " from toAlign" << endl;

    SVCERR << "Reference:" << endl;
    for (int i = 0; in_range_for(s1, i) && i < 100; ++i) {
        SVCERR << s1[i] << " ";
    }
    SVCERR << endl;

    SVCERR << "toAlign:" << endl;
    for (int i = 0; in_range_for(s2, i) && i < 100; ++i) {
        SVCERR << s2[i] << " ";
    }
    SVCERR << endl;
#endif
    
    RiseFallDTW dtw;
    vector<size_t> alignment;

    {
#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
        SVCERR << "TransformDTWAligner[" << this
               << "]: serialising DTW to avoid over-allocation" << endl;
#endif
        QMutexLocker locker(&m_dtwMutex);
        if (m_subsequence) {
            alignment = dtw.alignSubsequence(s1, s2);
        } else {
            alignment = dtw.alignSequences(s1, s2);
        }
    }

#ifdef DEBUG_TRANSFORM_DTW_ALIGNER
    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentRiseFall: "
           << "DTW produced " << alignment.size() << " points:" << endl;
    for (int i = 0; i < alignment.size() && i < 100; ++i) {
        SVCERR << alignment[i] << " ";
    }
    SVCERR << endl;
#endif

    alignmentModel->setPath(makePath(alignment,
                                     refFrames,
                                     otherFrames,
                                     alignmentModel->getSampleRate(),
                                     resolution));

    alignmentModel->setCompletion(100);

    SVCERR << "TransformDTWAligner[" << this
           << "]: performAlignmentRiseFall: Done" << endl;

    m_incomplete = false;
    return true;
}
