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

class Model;

class Align
{
public:
    Align() : m_error("") { }

    bool alignModel(Model *reference, Model *other); // via user preference
    
    bool alignModelViaTransform(Model *reference, Model *other);
    bool alignModelViaProgram(Model *reference, Model *other, QString program);

    QString getError() const { return m_error; }

private:
    QString m_error;
};

#endif

