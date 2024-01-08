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
#include "MATCHAligner.h"
#include "TransformDTWAligner.h"
#include "ExternalProgramAligner.h"

#include "framework/Document.h"

#include "transform/Transform.h"
#include "transform/TransformFactory.h"

#include "base/Pitch.h"

#include <QSettings>
#include <QTimer>

namespace sv {

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
    case SungNoteContourAlignment:
        return "sung-note-alignment";
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
    AlignmentType type = getAlignmentPreference();
    
    std::shared_ptr<Aligner> aligner;

    if (m_aligners.find(toAlign) != m_aligners.end()) {
        // We don't want a callback on removeAligner to happen during
        // our own call to addAligner! Disconnect and delete the old
        // aligner first
        disconnect(m_aligners[toAlign].get(), nullptr, this, nullptr);
        m_aligners.erase(toAlign);
    }
    
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
            
            aligner = make_shared<MATCHAligner>(doc,
                                                reference,
                                                toAlign,
                                                getUseSubsequenceAlignment(),
                                                withTuningDifference);
            break;
        }

        case SungNoteContourAlignment:
        {
            auto refModel = ModelById::get(reference);
            if (!refModel) return false;

            Transform transform = TransformFactory::getInstance()->
                getDefaultTransformFor("vamp:pyin:pyin:notes",
                                       refModel->getSampleRate());

            aligner = make_shared<TransformDTWAligner>
                (doc,
                 reference,
                 toAlign,
                 getUseSubsequenceAlignment(),
                 transform,
                 [](double prev, double curr) {
                     RiseFallDTW::Value v;
                     if (curr <= 0.0) {
                         v = { RiseFallDTW::Direction::None, 0.0 };
                     } else if (prev <= 0.0) {
                         v = { RiseFallDTW::Direction::Up, 0.0 };
                     } else {
                         double prevP = Pitch::getPitchForFrequency(prev);
                         double currP = Pitch::getPitchForFrequency(curr);
                         if (currP >= prevP) {
                             v = { RiseFallDTW::Direction::Up, currP - prevP };
                         } else {
                             v = { RiseFallDTW::Direction::Down, prevP - currP };
                         }
                     }
                     return v;
                 });
            break;
        }
        
        case TransformDrivenDTWAlignment:
            throw std::logic_error("Not yet implemented"); //!!!

        case ExternalProgramAlignment: {
            aligner = make_shared<ExternalProgramAligner>
                (doc,
                 reference,
                 toAlign,
                 getPreferredAlignmentProgram());
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
Align::getAlignmentPreference()
{
    QSettings settings;
    settings.beginGroup("Alignment");
    QString tag = settings.value
        ("alignment-type", getAlignmentTypeTag(MATCHAlignment)).toString();
    return getAlignmentTypeForTag(tag);
}

QString
Align::getPreferredAlignmentProgram()
{
    QSettings settings;
    settings.beginGroup("Alignment");
    return settings.value("alignment-program", "").toString();
}

Transform
Align::getPreferredAlignmentTransform()
{
    QSettings settings;
    settings.beginGroup("Alignment");
    QString xml = settings.value("alignment-transform", "").toString();
    return Transform(xml);
}

bool
Align::getUseSubsequenceAlignment()
{
    QSettings settings;
    settings.beginGroup("Alignment");
    return settings.value("alignment-subsequence", false).toBool();
}

void
Align::setAlignmentPreference(AlignmentType type)
{
    QSettings settings;
    settings.beginGroup("Alignment");
    QString tag = getAlignmentTypeTag(type);
    settings.setValue("alignment-type", tag);
    settings.endGroup();
}

void
Align::setDefaultAlignmentPreference(AlignmentType type)
{
    QSettings settings;
    settings.beginGroup("Alignment");
    if (!settings.contains("alignment-type")) {
        QString tag = getAlignmentTypeTag(type);
        settings.setValue("alignment-type", tag);
    }
    settings.endGroup();
}

void
Align::setPreferredAlignmentProgram(QString program)
{
    QSettings settings;
    settings.beginGroup("Alignment");
    settings.setValue("alignment-program", program);
    settings.endGroup();
}

void
Align::setPreferredAlignmentTransform(Transform transform)
{
    QSettings settings;
    settings.beginGroup("Alignment");
    settings.setValue("alignment-transform", transform.toXmlString());
    settings.endGroup();
}

void
Align::setUseSubsequenceAlignment(bool subsequence)
{
    QSettings settings;
    settings.beginGroup("Alignment");
    settings.setValue("alignment-subsequence", subsequence);
    settings.endGroup();
}

bool
Align::canAlign() 
{
    AlignmentType type = getAlignmentPreference();
    bool subsequence = getUseSubsequenceAlignment();

    if (type == ExternalProgramAlignment) {
        SVDEBUG << "Align::canAlign: type is ExternalProgramAlignment, "
                << "querying ExternalProgramAligner" << endl;
        return ExternalProgramAligner::isAvailable
            (getPreferredAlignmentProgram());
    } else {
        SVDEBUG << "Align::canAlign: type is not ExternalProgramAlignment, "
                << "querying MATCHAligner" << endl;
        return MATCHAligner::isAvailable
            (subsequence,
             type == MATCHAlignmentWithPitchCompare);
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

} // end namespace sv

