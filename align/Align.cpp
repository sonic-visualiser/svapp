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

#include "LinearAligner.h"
#include "TransformAligner.h"
#include "TransformDTWAligner.h"
#include "ExternalProgramAligner.h"

#include "framework/Document.h"

#include "transform/Transform.h"
#include "transform/TransformFactory.h"

#include "base/Pitch.h"

#include <QSettings>
#include <QTimer>

using std::make_shared;

QString
Align::getAlignmentTypeTag(AlignmentType type)
{
    switch (type) {
    case NoAlignment:
    default:
        return "no-alignment";
    case LinearAlignment:
        return "linear-alignment";
    case TrimmedLinearAlignment:
        return "trimmed-linear-alignment";
    case MATCHAlignment:
        return "match-alignment";
    case MATCHAlignmentWithPitchCompare:
        return "match-alignment-with-pitch";
    case SungPitchContourAlignment:
        return "sung-pitch-alignment";
    case TransformDrivenDTWAlignment:
        return "transform-driven-alignment";
    case ExternalProgramAlignment:
        return "external-program-alignment";
    }
}

Align::AlignmentType
Align::getAlignmentTypeForTag(QString tag)
{
    for (int i = 0; i <= int(LastAlignmentType); ++i) {
        if (tag == getAlignmentTypeTag(AlignmentType(i))) {
            return AlignmentType(i);
        }
    }
    return NoAlignment;
}

void
Align::alignModel(Document *doc,
                  ModelId reference,
                  ModelId toAlign)
{
    if (addAligner(doc, reference, toAlign)) {
        m_aligners[toAlign]->begin();
    }
}

void
Align::scheduleAlignment(Document *doc,
                         ModelId reference,
                         ModelId toAlign)
{
    int delay = 700 * int(m_aligners.size());
    if (delay > 3500) {
        delay = 3500;
    }
    if (!addAligner(doc, reference, toAlign)) {
        return;
    }
    SVCERR << "Align::scheduleAlignment: delaying " << delay << "ms" << endl;
    QTimer::singleShot(delay, m_aligners[toAlign].get(), SLOT(begin()));
}

bool
Align::addAligner(Document *doc,
                  ModelId reference,
                  ModelId toAlign)
{
    QString additionalData;
    AlignmentType type = getAlignmentPreference(additionalData);
    
    std::shared_ptr<Aligner> aligner;

    {
        // Replace the aligner with a new one. This also stops any
        // previously-running alignment, when the old entry is
        // replaced and its aligner destroyed.
        
        QMutexLocker locker(&m_mutex);

        switch (type) {

        case NoAlignment:
            return false;

        case LinearAlignment:
        case TrimmedLinearAlignment: {
            bool trimmed = (type == TrimmedLinearAlignment);
            aligner = make_shared<LinearAligner>(doc,
                                                 reference,
                                                 toAlign,
                                                 trimmed);
            break;
        }

        case MATCHAlignment:
        case MATCHAlignmentWithPitchCompare: {

            bool withTuningDifference =
                (type == MATCHAlignmentWithPitchCompare);
            
            aligner = make_shared<TransformAligner>(doc,
                                                    reference,
                                                    toAlign,
                                                    withTuningDifference);
            break;
        }

        case SungPitchContourAlignment:
        {
            auto refModel = ModelById::get(reference);
            if (!refModel) return false;
            
            Transform transform = TransformFactory::getInstance()->
                getDefaultTransformFor("vamp:pyin:pyin:smoothedpitchtrack",
                                       refModel->getSampleRate());

            transform.setParameter("outputunvoiced", 2.f);
            
            aligner = make_shared<TransformDTWAligner>
                (doc,
                 reference,
                 toAlign,
                 transform,
                 TransformDTWAligner::RiseFall,
                 [](double freq) {
                     if (freq < 0.0) {
                         return 0.0;
                     } else {
                         return double(Pitch::getPitchForFrequency(freq));
                     }
                 });
            break;
        }
        
        case TransformDrivenDTWAlignment:
            throw std::logic_error("Not yet implemented"); //!!!

        case ExternalProgramAlignment: {
            aligner = make_shared<ExternalProgramAligner>(doc,
                                                          reference,
                                                          toAlign,
                                                          additionalData);
        }
        }

        m_aligners[toAlign] = aligner;
    }

    connect(aligner.get(), SIGNAL(complete(ModelId)),
            this, SLOT(alignerComplete(ModelId)));

    connect(aligner.get(), SIGNAL(failed(ModelId, QString)),
            this, SLOT(alignerFailed(ModelId, QString)));

    return true;
}

Align::AlignmentType
Align::getAlignmentPreference(QString &additionalData)
{
    QSettings settings;
    settings.beginGroup("Alignment");

    QString tag = settings.value
        ("alignment-type", getAlignmentTypeTag(MATCHAlignment)).toString();

    AlignmentType type = getAlignmentTypeForTag(tag);

    if (type == TransformDrivenDTWAlignment) {
        additionalData = settings.value("alignment-transform", "").toString();
    } else if (type == ExternalProgramAlignment) {
        additionalData = settings.value("alignment-program", "").toString();
    }

    settings.endGroup();
    return type;
}

void
Align::setAlignmentPreference(AlignmentType type, QString additionalData)
{
    QSettings settings;
    settings.beginGroup("Alignment");

    QString tag = getAlignmentTypeTag(type);
    settings.setValue("alignment-type", tag);

    if (type == TransformDrivenDTWAlignment) {
        settings.setValue("alignment-transform", additionalData);
    } else if (type == ExternalProgramAlignment) {
        settings.setValue("alignment-program", additionalData);
    }

    settings.endGroup();
}

bool
Align::canAlign() 
{
    QString additionalData;
    AlignmentType type = getAlignmentPreference(additionalData);

    if (type == ExternalProgramAlignment) {
        return ExternalProgramAligner::isAvailable(additionalData);
    } else {
        return TransformAligner::isAvailable();
    }
}

void
Align::alignerComplete(ModelId alignmentModel)
{
    removeAligner(sender());
    emit alignmentComplete(alignmentModel);
}

void
Align::alignerFailed(ModelId toAlign, QString error)
{
    removeAligner(sender());
    emit alignmentFailed(toAlign, error);
}

void
Align::removeAligner(QObject *obj)
{
    Aligner *aligner = qobject_cast<Aligner *>(obj);
    if (!aligner) {
        SVCERR << "ERROR: Align::removeAligner: Not an Aligner" << endl;
        return;
    }

    QMutexLocker locker (&m_mutex);

    for (auto p: m_aligners) {
        if (aligner == p.second.get()) {
            m_aligners.erase(p.first);
            break;
        }
    }
}

