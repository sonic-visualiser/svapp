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

    /**
     * Align the "other" model to the reference, attaching an
     * AlignmentModel to it. Alignment is carried out by the method
     * configured in the user preferences (either a plugin transform
     * or an external process) and is done asynchronously. 
     *
     * The return value indicates whether the alignment procedure
     * started successfully. If it is true, then an AlignmentModel has
     * been constructed and attached to the toAlign model, and you can
     * query that model to discover the alignment progress, eventual
     * outcome, and any error message generated during alignment. (The
     * AlignmentModel is subsequently owned by the toAlign model.)
     * Conversely if alignModel returns false, no AlignmentModel has
     * been created, and the error return argument will contain an
     * error report about whatever problem prevented this from
     * happening.
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
    bool alignModel(Document *doc,
                    ModelId reference,
                    ModelId toAlign,
                    QString &error);

    /**
     * Return true if the alignment facility is available (relevant
     * plugin installed, etc).
     */
    static bool canAlign();

signals:
    /**
     * Emitted when an alignment is successfully completed. The
     * reference and other models can be queried from the alignment
     * model.
     */
    void alignmentComplete(ModelId alignmentModel); // an AlignmentModel

private slots:
    void alignerComplete(ModelId alignmentModel); // an AlignmentModel
    
private:
    QMutex m_mutex;

    // maps toAlign -> aligner for ongoing alignment - note that
    // although we can calculate alignments with different references,
    // we can only have one alignment on any given toAlign model, so
    // we don't key this on the whole (reference, toAlign) pair
    std::map<ModelId, std::shared_ptr<Aligner>> m_aligners;

    static void getAlignerPreference(bool useProgram, QString program);
};

#endif

