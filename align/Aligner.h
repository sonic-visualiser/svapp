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

#ifndef SV_ALIGNER_H
#define SV_ALIGNER_H

#include <QString>

#include "data/model/Model.h"

namespace sv {

class Aligner : public QObject
{
    Q_OBJECT

public:
    virtual ~Aligner() { }

public slots:
    virtual void begin() = 0;

signals:
    /**
     * Emitted when alignment is successfully completed. The reference
     * and toAlign models can be queried from the alignment
     * model. This should be emitted as the last thing the aligner
     * does, as the recipient may delete the aligner during the call.
     */
    void complete(ModelId alignmentModel); // an AlignmentModel

    /**
     * Emitted when alignment fails. This should be emitted as the
     * last thing the aligner does, as the recipient may delete the
     * aligner during the call.
     */
    void failed(ModelId toAlign, QString errorText); // the toAlign model
};

} // end namespace sv

#endif
