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
            ready = reference->isReady() && toAlign->isReady();
        }
        if (!ready) {
            SVDEBUG << "LinearAligner: Waiting for models..." << endl;
            usleep(500000);
        }
    }

    auto reference = ModelById::get(m_reference);
    auto toAlign = ModelById::get(m_toAlign);

    if (!reference || !reference->isOK() ||
        !toAlign || !toAlign->isOK()) {
        return;
    }
    
    sv_frame_t s0 = reference->getStartFrame(), s1 = toAlign->getStartFrame();
    sv_frame_t e0 = reference->getEndFrame(), e1 = toAlign->getEndFrame();
    sv_frame_t d0 = e0 - s0, d1 = e1 - s1;

    if (d1 == 0) {
        return;
    }
    
    double ratio = double(d0) / double(d1);
    sv_frame_t resolution = 1024;
    
    Path path(reference->getSampleRate(), resolution);

    for (sv_frame_t f = s1; f < e1; f += resolution) {
        sv_frame_t target = sv_frame_t(double(f - s1) * ratio);
        path.add(PathPoint(f, target));
    }

    auto alignment = std::make_shared<AlignmentModel>(m_reference,
                                                      m_toAlign,
                                                      ModelId());

    auto alignmentModelId = ModelById::add(alignment);

    alignment->setPath(path);
    toAlign->setAlignment(alignmentModelId);
    m_document->addNonDerivedModel(alignmentModelId);
}

//!!! + trimmed
