/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef ALIGN_H
#define ALIGN_H

#include <QString>
#include <QObject>
#include <QProcess>
#include <QMutex>
#include <set>

class Model;
class AlignmentModel;
class SparseTimeValueModel;
class AggregateWaveModel;
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
                    Model *reference,
                    Model *toAlign,
                    QString &error);
    
    bool alignModelViaTransform(Document *doc,
                                Model *reference,
                                Model *toAlign,
                                QString &error);

    bool alignModelViaProgram(Document *doc,
                              Model *reference,
                              Model *toAlign,
                              QString program,
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
    void alignmentComplete(AlignmentModel *alignment);

private slots:
    void alignmentCompletionChanged();
    void tuningDifferenceCompletionChanged();
    void alignmentProgramFinished(int, QProcess::ExitStatus);
    
private:
    static QString getAlignmentTransformName();
    static QString getTuningDifferenceTransformName();

    bool beginTransformDrivenAlignment(AggregateWaveModel *,
                                       AlignmentModel *,
                                       float tuningFrequency = 0.f);

    QMutex m_mutex;
    std::map<QProcess *, AlignmentModel *> m_pendingProcesses;
    std::map<SparseTimeValueModel *,
             std::pair<AggregateWaveModel *,
                       AlignmentModel *>> m_pendingTuningDiffs;
};

#endif

