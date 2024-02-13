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

#include "Aligner.h"

#include <QProcess>
#include <QString>

namespace sv {

class AlignmentModel;
class Document;

class ExternalProgramAligner : public Aligner
{
    Q_OBJECT

public:
    ExternalProgramAligner(Document *doc,
                           ModelId reference,
                           ModelId toAlign,
                           QString program);

    // Destroy the aligner, cleanly cancelling any ongoing alignment
    ~ExternalProgramAligner();

    void begin() override;

    static bool isAvailable(QString program);

private slots:
    void programFinished(int, QProcess::ExitStatus);
    void logStderrOutput();

private:
    Document *m_document;
    ModelId m_reference;
    ModelId m_toAlign;
    ModelId m_alignmentModel;
    QString m_program;
    QProcess *m_process;
};

} // end namespace sv

#endif
