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

TransformDTWAligner::TransformDTWAligner(Document *doc,
                                         ModelId reference,
                                         ModelId toAlign,
                                         Transform transform,
                                         DTWType dtwType) :
    m_document(doc),
    m_reference(reference),
    m_toAlign(toAlign),
    m_transform(transform),
    m_dtwType(dtwType),
    m_incomplete(true),
    m_outputPreprocessor([](double x) { return x; })
{
}

TransformDTWAligner::TransformDTWAligner(Document *doc,
                                         ModelId reference,
                                         ModelId toAlign,
                                         Transform transform,
                                         DTWType dtwType,
                                         std::function<double(double)>
                                         outputPreprocessor) :
    m_document(doc),
    m_reference(reference),
    m_toAlign(toAlign),
    m_transform(transform),
    m_dtwType(dtwType),
    m_incomplete(true),
    m_outputPreprocessor(outputPreprocessor)
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

    SVCERR << "TransformDTWAligner[" << this << "]: begin(): transform id "
           << m_transform.getIdentifier()
           << " is running on reference model" << endl;

    message = "";

    m_toAlignOutputModel = mtf->transform(m_transform, m_toAlign, message);
    auto toAlignOutputModel = ModelById::get(m_toAlignOutputModel);
    if (!toAlignOutputModel) {
        SVCERR << "Align::alignModel: ERROR: Failed to create toAlign output model (no plugin?)" << endl;
        emit failed(m_toAlign, message);
        return;
    }

    SVCERR << "TransformDTWAligner[" << this << "]: begin(): transform id "
           << m_transform.getIdentifier()
           << " is running on toAlign model" << endl;

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
TransformDTWAligner::completionChanged(ModelId id)
{
    if (!m_incomplete) {
        return;
    }

    SVCERR << "TransformDTWAligner[" << this << "]: completionChanged: "
           << "model " << id << endl;

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
               << "ready, calling performAlignment" << endl;

        alignmentModel->setCompletion(95);
        
        if (performAlignment()) {
            emit complete(m_alignmentModel);
        } else {
            emit failed(m_toAlign, tr("Alignment of transform outputs failed"));
        }

    } else {

        SVCERR << "TransformDTWAligner[" << this << "]: completionChanged: "
               << "not ready yet: reference completion " << referenceCompletion
               << ", toAlign completion " << toAlignCompletion << endl;

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
TransformDTWAligner::performAlignmentMagnitude()
{
    auto referenceOutputSTVM = ModelById::getAs<SparseTimeValueModel>
        (m_referenceOutputModel);
    auto toAlignOutputSTVM = ModelById::getAs<SparseTimeValueModel>
        (m_toAlignOutputModel);
    auto alignmentModel = ModelById::getAs<AlignmentModel>
        (m_alignmentModel);

    if (!referenceOutputSTVM || !toAlignOutputSTVM) {
        //!!! what?
        return false;
    }

    if (!alignmentModel) {
        return false;
    }
    
    vector<double> s1, s2;

    {
        auto events = referenceOutputSTVM->getAllEvents();
        for (auto e: events) {
            s1.push_back(m_outputPreprocessor(e.getValue()));
        }
        events = toAlignOutputSTVM->getAllEvents();
        for (auto e: events) {
            s2.push_back(m_outputPreprocessor(e.getValue()));
        }
    }

    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentMagnitude: "
           << "Have " << s1.size() << " events from reference, "
           << s2.size() << " from toAlign" << endl;

    MagnitudeDTW dtw;
    vector<size_t> alignment;

    {
        SVCERR << "TransformDTWAligner[" << this
               << "]: serialising DTW to avoid over-allocation" << endl;
        static QMutex mutex;
        QMutexLocker locker(&mutex);

        alignment = dtw.alignSeries(s1, s2);
    }

    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentMagnitude: "
           << "DTW produced " << alignment.size() << " points:" << endl;
    for (int i = 0; i < alignment.size() && i < 100; ++i) {
        SVCERR << alignment[i] << " ";
    }
    SVCERR << endl;

    alignmentModel->setCompletion(100);

    sv_frame_t resolution = referenceOutputSTVM->getResolution();
    sv_frame_t sourceFrame = 0;
    
    Path path(referenceOutputSTVM->getSampleRate(), resolution);
    
    for (size_t m: alignment) {
        path.add(PathPoint(sourceFrame, sv_frame_t(m) * resolution));
        sourceFrame += resolution;
    }

    alignmentModel->setPath(path);

    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentMagnitude: Done"
           << endl;

    m_incomplete = false;
    return true;
}

bool
TransformDTWAligner::performAlignmentRiseFall()
{
    auto referenceOutputSTVM = ModelById::getAs<SparseTimeValueModel>
        (m_referenceOutputModel);
    auto toAlignOutputSTVM = ModelById::getAs<SparseTimeValueModel>
        (m_toAlignOutputModel);
    auto alignmentModel = ModelById::getAs<AlignmentModel>
        (m_alignmentModel);

    if (!referenceOutputSTVM || !toAlignOutputSTVM) {
        //!!! what?
        return false;
    }

    if (!alignmentModel) {
        return false;
    }

    auto convertEvents =
        [this](const EventVector &ee) {
            vector<RiseFallDTW::Value> s;
            double prev = 0.0;
            for (auto e: ee) {
                double v = m_outputPreprocessor(e.getValue());
                if (v == prev || s.empty()) {
                    s.push_back({ RiseFallDTW::Direction::None, 0.0 });
                } else if (v > prev) {
                    s.push_back({ RiseFallDTW::Direction::Up, v - prev });
                } else {
                    s.push_back({ RiseFallDTW::Direction::Down, prev - v });
                }
            }
            return s;
        };
    
    vector<RiseFallDTW::Value> s1 =
        convertEvents(referenceOutputSTVM->getAllEvents());

    vector<RiseFallDTW::Value> s2 =
        convertEvents(toAlignOutputSTVM->getAllEvents());

    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentRiseFall: "
           << "Have " << s1.size() << " events from reference, "
           << s2.size() << " from toAlign" << endl;

    RiseFallDTW dtw;
    
    vector<size_t> alignment;

    {
        SVCERR << "TransformDTWAligner[" << this
               << "]: serialising DTW to avoid over-allocation" << endl;
        static QMutex mutex;
        QMutexLocker locker(&mutex);

        alignment = dtw.alignSeries(s1, s2);
    }

    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentRiseFall: "
           << "DTW produced " << alignment.size() << " points:" << endl;
    for (int i = 0; i < alignment.size() && i < 100; ++i) {
        SVCERR << alignment[i] << " ";
    }
    SVCERR << endl;

    alignmentModel->setCompletion(100);

    sv_frame_t resolution = referenceOutputSTVM->getResolution();
    sv_frame_t sourceFrame = 0;
    
    Path path(referenceOutputSTVM->getSampleRate(), resolution);
    
    for (size_t m: alignment) {
        path.add(PathPoint(sourceFrame, sv_frame_t(m) * resolution));
        sourceFrame += resolution;
    }

    alignmentModel->setPath(path);

    SVCERR << "TransformDTWAligner[" << this << "]: performAlignmentRiseFall: Done"
           << endl;

    m_incomplete = false;
    return true;
}
