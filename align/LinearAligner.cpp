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

#include "LinearAligner.h"

#include "system/System.h"

#include "data/model/Path.h"
#include "data/model/AlignmentModel.h"

#include "framework/Document.h"

#include "svcore/data/model/DenseTimeValueModel.h"

#include <QApplication>

LinearAligner::LinearAligner(Document *doc,
                             ModelId reference,
                             ModelId toAlign,
                             bool trimmed) :
    m_document(doc),
    m_reference(reference),
    m_toAlign(toAlign),
    m_trimmed(trimmed)
{
}

LinearAligner::~LinearAligner()
{
}

void
LinearAligner::begin()
{
    bool ready = false;
    while (!ready) {
        { // scope so as to release input shared_ptr before sleeping
            auto reference = ModelById::get(m_reference);
            auto toAlign = ModelById::get(m_toAlign);
            if (!reference || !reference->isOK() ||
                !toAlign || !toAlign->isOK()) {
                return;
            }
            ready = (reference->isReady() && toAlign->isReady());
        }
        if (!ready) {
            SVDEBUG << "LinearAligner: Waiting for models..." << endl;
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
                                        QEventLoop::ExcludeSocketNotifiers,
                                        500);
        }
    }

    auto reference = ModelById::get(m_reference);
    auto toAlign = ModelById::get(m_toAlign);

    if (!reference || !reference->isOK() ||
        !toAlign || !toAlign->isOK()) {
        return;
    }

    sv_frame_t s0, e0, s1, e1;
    s0 = reference->getStartFrame();
    e0 = reference->getEndFrame();
    s1 = toAlign->getStartFrame();
    e1 = toAlign->getEndFrame();

    if (m_trimmed) {
        getTrimmedExtents(m_reference, s0, e0);
        getTrimmedExtents(m_toAlign, s1, e1);
        SVCERR << "Trimmed extents: reference: " << s0 << " to " << e0
               << ", toAlign: " << s1 << " to " << e1 << endl;
    }

    sv_frame_t d0 = e0 - s0, d1 = e1 - s1;

    if (d1 == 0) {
        return;
    }
    
    double ratio = double(d0) / double(d1);
    int resolution = 1024;
    
    Path path(reference->getSampleRate(), resolution);

    for (sv_frame_t f = s1; f < e1; f += resolution) {
        sv_frame_t target = s0 + sv_frame_t(double(f - s1) * ratio);
        path.add(PathPoint(f, target));
    }

    auto alignment = std::make_shared<AlignmentModel>(m_reference,
                                                      m_toAlign,
                                                      ModelId());

    auto alignmentModelId = ModelById::add(alignment);

    alignment->setPath(path);
    alignment->setCompletion(100);
    toAlign->setAlignment(alignmentModelId);
    m_document->addNonDerivedModel(alignmentModelId);

    emit complete(alignmentModelId);
}

bool
LinearAligner::getTrimmedExtents(ModelId modelId,
                                 sv_frame_t &start,
                                 sv_frame_t &end)
{
    auto model = ModelById::getAs<DenseTimeValueModel>(modelId);
    if (!model) return false;

    sv_frame_t chunksize = 1024;
    double threshold = 1e-2;

    auto rms = [](const floatvec_t &samples) {
                   double rms = 0.0;
                   for (auto s: samples) {
                       rms += s * s;
                   }
                   rms /= double(samples.size());
                   rms = sqrt(rms);
                   return rms;
               };
    
    while (start < end) {
        floatvec_t samples = model->getData(-1, start, chunksize);
        if (samples.empty()) {
            return false; // no non-silent content found
        }
        if (rms(samples) > threshold) {
            for (auto s: samples) {
                if (fabsf(s) > threshold) {
                    break;
                }
                ++start;
            }
            break;
        }
        start += chunksize;
    }
    
    if (start >= end) {
        return false;
    }

    while (end > start) {
        sv_frame_t probe = end - chunksize;
        if (probe < 0) probe = 0;
        floatvec_t samples = model->getData(-1, probe, chunksize);
        if (samples.empty()) {
            break;
        }
        if (rms(samples) > threshold) {
            break;
        }
        end = probe;
    }

    return (end > start);
}
