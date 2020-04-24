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

#include "Align.h"
#include "TransformAligner.h"
#include "ExternalProgramAligner.h"
#include "framework/Document.h"

#include <QSettings>

bool
Align::alignModel(Document *doc,
                  ModelId reference,
                  ModelId toAlign,
                  QString &error)
{
    bool useProgram;
    QString program;
    getAlignerPreference(useProgram, program);
    
    std::shared_ptr<Aligner> aligner;

    {
        // Replace the aligner with a new one. This also stops any
        // previously-running alignment, when the old entry is
        // replaced and its aligner destroyed.
        
        QMutexLocker locker(&m_mutex);
    
        if (useProgram && (program != "")) {
            m_aligners[toAlign] =
                std::make_shared<ExternalProgramAligner>(doc,
                                                         reference,
                                                         toAlign,
                                                         program);
        } else {
            m_aligners[toAlign] =
                std::make_shared<TransformAligner>(doc,
                                                   reference,
                                                   toAlign);
        }

        aligner = m_aligners[toAlign];
    }

    connect(aligner.get(), SIGNAL(complete(ModelId)),
            this, SLOT(alignerComplete(ModelId)));
    
    return aligner->begin(error);
}

void
Align::getAlignerPreference(bool useProgram, QString program)
{
    QSettings settings;
    settings.beginGroup("Preferences");
    useProgram = settings.value("use-external-alignment", false).toBool();
    program = settings.value("external-alignment-program", "").toString();
    settings.endGroup();
}

bool
Align::canAlign() 
{
    bool useProgram;
    QString program;
    getAlignerPreference(useProgram, program);

    if (useProgram) {
        return ExternalProgramAligner::isAvailable(program);
    } else {
        return TransformAligner::isAvailable();
    }
}

void
Align::alignerComplete(ModelId alignmentModel)
{
    Aligner *aligner = qobject_cast<Aligner *>(sender());
    if (!aligner) {
        SVCERR << "ERROR: Align::alignerComplete: Caller is not an Aligner"
               << endl;
        return;
    }

    {
        QMutexLocker locker (&m_mutex);

        for (auto p: m_aligners) {
            if (aligner == p.second.get()) {
                m_aligners.erase(p.first);
                break;
            }
        }
    }

    emit alignmentComplete(alignmentModel);
}
