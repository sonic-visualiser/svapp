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

#ifndef SV_MATCH_ALIGNER_H
#define SV_MATCH_ALIGNER_H

#include "Aligner.h"

class AlignmentModel;
class Document;

class MATCHAligner : public Aligner
{
    Q_OBJECT

public:
    MATCHAligner(Document *doc,
                 ModelId reference,
                 ModelId toAlign,
                 bool subsequence,
                 bool withTuningDifference);

    // Destroy the aligner, cleanly cancelling any ongoing alignment
    ~MATCHAligner();

    void begin() override;

    static bool isAvailable();

private slots:
    void alignmentCompletionChanged(ModelId);
    void tuningDifferenceCompletionChanged(ModelId);

private:
    static QString getAlignmentTransformName(bool subsequence);
    static QString getTuningDifferenceTransformName();

    bool beginAlignmentPhase();
    
    Document *m_document;
    ModelId m_reference;
    ModelId m_toAlign;
    ModelId m_aggregateModel; // an AggregateWaveModel
    ModelId m_alignmentModel; // an AlignmentModel
    ModelId m_tuningDiffOutputModel; // SparseTimeValueModel, unreg'd with doc
    ModelId m_pathOutputModel; // SparseTimeValueModel, unreg'd with doc
    bool m_subsequence;
    bool m_withTuningDifference;
    float m_tuningFrequency;
    bool m_incomplete;
};

#endif
