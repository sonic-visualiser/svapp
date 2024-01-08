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
#include "DTW.h"

#include "transform/Transform.h"
#include "svcore/data/model/Path.h"

#include <functional>

#include <QMutex>

namespace sv {

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

    /**
     * Create a TransformDTWAligner that runs the given transform on
     * both models and feeds the resulting values into the given DTW
     * type. If DTWType is Magnitude, the transform output values are
     * used unmodified; if RiseFall, the deltas between consecutive
     * values are used.
     */
    TransformDTWAligner(Document *doc,
                        ModelId reference,
                        ModelId toAlign,
                        bool subsequence,
                        Transform transform,
                        DTWType dtwType);

    typedef std::function<double(double)> MagnitudePreprocessor;

    /**
     * Create a TransformDTWAligner that runs the given transform on
     * both models, applies the supplied output preprocessor, and
     * feeds the resulting values into a Magnitude DTW type.
     */
    TransformDTWAligner(Document *doc,
                        ModelId reference,
                        ModelId toAlign,
                        bool subsequence,
                        Transform transform,
                        MagnitudePreprocessor outputPreprocessor);

    typedef std::function<RiseFallDTW::Value(double prev, double curr)>
        RiseFallPreprocessor;

    /**
     * Create a TransformDTWAligner that runs the given transform on
     * both models, applies the supplied output preprocessor, and
     * feeds the resulting values into a RiseFall DTW type.
     */
    TransformDTWAligner(Document *doc,
                        ModelId reference,
                        ModelId toAlign,
                        bool subsequence,
                        Transform transform,
                        RiseFallPreprocessor outputPreprocessor);

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

    bool getValuesFrom(ModelId modelId,
                       std::vector<sv_frame_t> &frames,
                       std::vector<double> &values,
                       sv_frame_t &resolution);

    Path makePath(const std::vector<size_t> &alignment,
                  const std::vector<sv_frame_t> &refFrames,
                  const std::vector<sv_frame_t> &otherFrames,
                  sv_samplerate_t sampleRate,
                  sv_frame_t resolution);
    
    Document *m_document;
    ModelId m_reference;
    ModelId m_toAlign;
    ModelId m_referenceOutputModel;
    ModelId m_toAlignOutputModel;
    ModelId m_alignmentModel;
    Transform m_transform;
    DTWType m_dtwType;
    bool m_subsequence;
    bool m_incomplete;
    MagnitudePreprocessor m_magnitudePreprocessor;
    RiseFallPreprocessor m_riseFallPreprocessor;

    static QMutex m_dtwMutex;
};

} // end namespace sv

#endif
