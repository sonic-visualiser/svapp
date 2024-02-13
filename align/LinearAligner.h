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

#ifndef SV_LINEAR_ALIGNER_H
#define SV_LINEAR_ALIGNER_H

#include "Aligner.h"

namespace sv {

class AlignmentModel;
class Document;

class LinearAligner : public Aligner
{
    Q_OBJECT

public:
    LinearAligner(Document *doc,
                  ModelId reference,
                  ModelId toAlign,
                  bool trimmed);

    ~LinearAligner();

    void begin() override;

    static bool isAvailable() {
        return true;
    }

private:
    Document *m_document;
    ModelId m_reference;
    ModelId m_toAlign;
    bool m_trimmed;

    bool getTrimmedExtents(ModelId model, sv_frame_t &start, sv_frame_t &end);
};

} // end namespace sv

#endif
