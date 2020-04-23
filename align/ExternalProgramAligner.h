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

#ifndef SV_EXTERNAL_PROGRAM_ALIGNER_H
#define SV_EXTERNAL_PROGRAM_ALIGNER_H

#include <QProcess>
#include <QString>

#include "data/model/Model.h"

class AlignmentModel;
class Document;

class ExternalProgramAligner : public QObject
{
    Q_OBJECT

public:
    ExternalProgramAligner(Document *doc,
                           ModelId reference,
                           ModelId toAlign,
                           QString program);

    // Destroy the aligner, cleanly cancelling any ongoing alignment
    ~ExternalProgramAligner();

    bool begin(QString &error);

    static bool isAvailable(QString program);
    
signals:
    /**
     * Emitted when alignment is successfully completed. The reference
     * and toAlign models can be queried from the alignment model.
     */
    void complete(ModelId alignmentModel); // an AlignmentModel

private slots:
    void programFinished(int, QProcess::ExitStatus);

private:
    Document *m_document;
    ModelId m_reference;
    ModelId m_toAlign;
    ModelId m_alignmentModel;
    QString m_program;
    QProcess *m_process;
};

#endif
