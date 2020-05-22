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

#ifndef SV_TRANSFORM_DTW_ALIGNER_H
#define SV_TRANSFORM_DTW_ALIGNER_H

#include "Aligner.h"

#include "transform/Transform.h"

#include <functional>

class AlignmentModel;
class Document;

class TransformDTWAligner : public Aligner
{
    Q_OBJECT

public:
    enum DTWType {
        Magnitude,
        RiseFall
    };
    
    TransformDTWAligner(Document *doc,
                        ModelId reference,
                        ModelId toAlign,
                        Transform transform,
                        DTWType dtwType);
    
    TransformDTWAligner(Document *doc,
                        ModelId reference,
                        ModelId toAlign,
                        Transform transform,
                        DTWType dtwType,
                        std::function<double(double)> outputPreprocessor);

    // Destroy the aligner, cleanly cancelling any ongoing alignment
    ~TransformDTWAligner();

    void begin() override;

    static bool isAvailable();

private slots:
    void completionChanged(ModelId);

private:
    bool performAlignment();
    bool performAlignmentMagnitude();
    bool performAlignmentRiseFall();
    
    Document *m_document;
    ModelId m_reference;
    ModelId m_toAlign;
    ModelId m_referenceOutputModel;
    ModelId m_toAlignOutputModel;
    ModelId m_alignmentModel;
    Transform m_transform;
    DTWType m_dtwType;
    bool m_incomplete;
    std::function<double(double)> m_outputPreprocessor;
};

#endif
