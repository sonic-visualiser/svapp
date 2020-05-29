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

#ifndef SV_ALIGN_H
#define SV_ALIGN_H

#include <QString>
#include <QObject>
#include <QProcess>
#include <QMutex>
#include <set>

#include "Aligner.h"

class AlignmentModel;
class Document;

class Align : public QObject
{
    Q_OBJECT
    
public:
    Align() { }

    enum AlignmentType {
        NoAlignment,
        LinearAlignment,
        TrimmedLinearAlignment,
        MATCHAlignment,
        MATCHAlignmentWithPitchCompare,
        SungNoteContourAlignment,
        TransformDrivenDTWAlignment,
        ExternalProgramAlignment,

        LastAlignmentType = ExternalProgramAlignment
    };

    static QString getAlignmentTypeTag(AlignmentType type);
    static AlignmentType getAlignmentTypeForTag(QString tag);

    static AlignmentType getAlignmentPreference(QString &additionalData);
    static void setAlignmentPreference(AlignmentType type,
                                       QString additionalData = "");
    
    /**
     * Align the "other" model to the reference, attaching an
     * AlignmentModel to it. Alignment is carried out by the method
     * configured in the user preferences (either a plugin transform
     * or an external process) and is done asynchronously. 
     *
     * Any errors are reported by firing the alignmentFailed
     * signal. Note that the signal may be fired during the call to
     * this function, if the aligner fails to start at all.
     *
     * If alignment starts successfully, then an AlignmentModel has
     * been constructed and attached to the toAlign model, and you can
     * query that model to discover the alignment progress, eventual
     * outcome, and also (separately from the alignmentFailed signal
     * here) any error message generated during alignment.
     *
     * A single Align object may carry out many simultanous alignment
     * calls -- you do not need to create a new Align object each
     * time, nor to wait for an alignment to be complete before
     * starting a new one.
     * 
     * The Align object must survive after this call, for at least as
     * long as the alignment takes. The usual expectation is that the
     * Align object will simply share the process or document
     * lifespan.
     */
    void alignModel(Document *doc,
                    ModelId reference,
                    ModelId toAlign);

    /**
     * As alignModel, except that the alignment does not begin
     * immediately, but is instead placed behind an event callback
     * with a small delay. Useful to avoid an unresponsive GUI when
     * firing off alignments while doing something else as well. Any
     * error is reported by firing the alignmentFailed signal.
     *
     * Scheduled alignments are not queued or serialised - many could
     * happen at once. They are just delayed a little for UI
     * responsiveness.
     */
    void scheduleAlignment(Document *doc,
                           ModelId reference,
                           ModelId toAlign);
    
    /**
     * Return true if the alignment facility is available (relevant
     * plugin installed, etc).
     */
    static bool canAlign();

    //!!! + check whether specific alignment types are available

signals:
    /**
     * Emitted when an alignment is successfully completed. The
     * reference and other models can be queried from the alignment
     * model.
     */
    void alignmentComplete(ModelId alignmentModel); // an AlignmentModel

    /**
     * Emitted when an alignment fails. The model is the toAlign model
     * that was passed to the call to alignModel or scheduleAlignment.
     */
    void alignmentFailed(ModelId toAlign, QString errorText);

private slots:
    void alignerComplete(ModelId alignmentModel); // an AlignmentModel
    void alignerFailed(ModelId toAlign, QString errorText);
    
private:
    QMutex m_mutex;

    // maps toAlign -> aligner for ongoing alignment - note that
    // although we can calculate alignments with different references,
    // we can only have one alignment on any given toAlign model, so
    // we don't key this on the whole (reference, toAlign) pair
    std::map<ModelId, std::shared_ptr<Aligner>> m_aligners;

    bool addAligner(Document *doc, ModelId reference, ModelId toAlign);
    void removeAligner(QObject *);
};

#endif

