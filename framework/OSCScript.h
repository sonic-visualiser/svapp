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

#ifndef SV_OSC_SCRIPT_H
#define SV_OSC_SCRIPT_H

#include <QThread>
#include <QFile>
#include <QTextStream>

#include "base/Debug.h"
#include "base/StringBits.h"
#include "data/osc/OSCQueue.h"
#include "data/osc/OSCMessage.h"

#include <stdexcept>

class OSCScript : public QThread
{
    Q_OBJECT

public:
    OSCScript(QString filename, OSCQueue *queue) :
        m_filename(filename),
        m_queue(queue),
        m_abandoning(false) {
    }

    void run() override {

        QFile f(m_filename);
        if (!f.open(QFile::ReadOnly | QFile::Text)) {
            SVCERR << "OSCScript: Failed to open script file \""
                   << m_filename << "\" for reading" << endl;
            throw std::runtime_error("OSC script file not found");
        }

        if (!m_queue) {
            SVCERR << "OSCScript: No OSC queue available" << endl;
            throw std::runtime_error("OSC queue not running");
        }
        
        int lineno = 0;
        QTextStream str(&f);
        
        while (!str.atEnd() && !m_abandoning) {

            ++lineno;

            QString line = str.readLine().trimmed();
            if (line == QString()) continue;

            if (line[0] == '#') {
                continue;

            } else if (line[0].isDigit()) {
                bool ok = false;
                float pause = line.toFloat(&ok);
                if (ok) {
                    SVCERR << "OSCScript: " << m_filename << ":" << lineno
                           << ": pausing for " << pause << " sec" << endl;
                    msleep(unsigned(round(pause * 1000.0f)));
                    continue;
                } else {
                    SVCERR << "OSCScript: " << m_filename << ":" << lineno
                           << ": error: failed to parse sleep time, giving up"
                           << endl;
                    throw std::runtime_error("OSC script parse error");
                }

            } else if (line[0] == '/' && line.size() > 1) {
                QStringList parts = StringBits::splitQuoted(line, ' ');
                if (parts.empty()) {
                    SVCERR << "OSCScript: " << m_filename << ":" << lineno
                           << ": warning: empty command spec, ignoring"
                           << endl;
                    continue;
                }
                OSCMessage message;
                message.setMethod(parts[0].mid(1));
                for (int i = 1; i < parts.size(); ++i) {
                    message.addArg(parts[i]);
                }
                SVCERR << "OSCScript: " << m_filename << ":" << lineno
                       << ": invoking: \"" << parts[0] << "\"" << endl;
                m_queue->postMessage(message);

            } else {
                SVCERR << "OSCScript: " << m_filename << ":" << lineno
                       << ": error: message expected" << endl;
                throw std::runtime_error("OSC script parse error");
            }
        }

        SVCERR << "OSCScript: " << m_filename << ": finished" << endl;
    }

    void abandon() {
        m_abandoning = true;
    }
    
private:
    QString m_filename;
    OSCQueue *m_queue; // I do not own this
    bool m_abandoning;
};

#endif

