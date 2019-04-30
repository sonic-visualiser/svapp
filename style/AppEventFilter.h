/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*- vi:set ts=8 sts=4 sw=4: */

/*
    Vect
    An experimental audio player for plural recordings of a work
    Centre for Digital Music, Queen Mary, University of London.

    This file is taken from Rosegarden, a MIDI and audio sequencer and
    musical notation editor. Copyright 2000-2018 the Rosegarden
    development team. Thorn style developed in stylesheet form by
    D. Michael McIntyre and reimplemented as a class by David Faure.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef SV_THORN_STYLE_APPEVENT_FILTER_H
#define SV_THORN_STYLE_APPEVENT_FILTER_H

#include "ThornStyle.h"

/**
 * The AppEventFilter class is notified when a new widget is created
 * and can decide whether to apply the Thorn Style to it or not.
 */
class AppEventFilter : public QObject
{
    Q_OBJECT
    
public:
    AppEventFilter();
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool shouldIgnoreThornStyle(QWidget *widget) const;
    void polishWidget(QWidget *widget);

private:
    ThornStyle m_style;
    QPalette m_systemPalette;
    QStyle *m_systemStyle;
};

#endif
