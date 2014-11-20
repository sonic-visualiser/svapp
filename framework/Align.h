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
#include <set>

class Model;
class AlignmentModel;

class Align : public QObject
{
    Q_OBJECT
    
public:
    Align() : m_error("") { }

    /**
     * Align the "other" model to the reference, attaching an
     * AlignmentModel to it. Alignment is carried out by the method
     * configured in the user preferences (either a plugin transform
     * or an external process) and is done asynchronously. 
     *
     * A single Align object may carry out many simultanous alignment
     * calls -- you do not need to create a new Align object each
     * time, nor to wait for an alignment to be complete before
     * starting a new one.
     * 
     * The Align object must survive after this call, for at least as
     * long as the alignment takes. There is currently no way in this
     * API to discover when an alignment is complete -- the
     * expectation is that the Align object will simply share the
     * process or document lifespan.
     */
    bool alignModel(Model *reference, Model *other); // via user preference
    
    bool alignModelViaTransform(Model *reference, Model *other);
    bool alignModelViaProgram(Model *reference, Model *other, QString program);

    QString getError() const { return m_error; }
        
private slots:
    void alignmentProgramFinished(int, QProcess::ExitStatus);
    
private:
    QString m_error;
    std::map<QProcess *, AlignmentModel *> m_processModels;
};

#endif

