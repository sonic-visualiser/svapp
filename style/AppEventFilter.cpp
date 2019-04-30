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

#include "ThornStyle.h"
#include "AppEventFilter.h"

#include <QApplication>
#include <QFileDialog>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QLabel>
#include <QRadioButton>
#include <QToolBar>
#include <QWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>

// Apply the style to widget and its children, recursively
// Even though every widget goes through the event filter, this is needed
// for the case where a whole widget hierarchy is suddenly reparented into the file dialog.
// Then we need to apply the app style again. Testcase: scrollbars in file dialog.
static void applyStyleRecursive(QWidget* widget, QStyle *style)
{
    if (widget->style() != style) {
        widget->setStyle(style);
    }
    foreach (QObject* obj, widget->children()) {
        if (obj->isWidgetType()) {
            QWidget *w = static_cast<QWidget *>(obj);
            applyStyleRecursive(w, style);
        }
    }
}

AppEventFilter::AppEventFilter() :
    m_systemPalette(qApp->palette()),
    m_systemStyle(qApp->style()) {
}

bool
AppEventFilter::shouldIgnoreThornStyle(QWidget *widget) const {
    return qobject_cast<QFileDialog *>(widget)
        || widget->inherits("KDEPlatformFileDialog")
        || widget->inherits("KDirSelectDialog");
}

// when we ditch Qt4, we can switch to qCDebug...
//#define DEBUG_EVENTFILTER

bool AppEventFilter::eventFilter(QObject *watched, QEvent *event)
{
    static bool s_insidePolish = false; // setStyle calls polish again, so skip doing the work twice
    if (!s_insidePolish && watched->isWidgetType() && event->type() == QEvent::Polish) {
        s_insidePolish = true;
        // This is called after every widget is created and just before being shown
        // (we use this so that it has a proper parent widget already)
        QWidget *widget = static_cast<QWidget *>(watched);
        if (shouldIgnoreThornStyle(widget)) {
            // The palette from the mainwindow propagated to the dialog, restore it.
            widget->setPalette(m_systemPalette);
#ifdef DEBUG_EVENTFILTER
            qDebug() << widget << "now using app style (recursive)";
#endif
            applyStyleRecursive(widget, qApp->style());
            s_insidePolish = false;
            return false;
        }
        QWidget *toplevel = widget->window();
#ifdef DEBUG_EVENTFILTER
        qDebug() << widget << "current widget style=" << widget->style() << "shouldignore=" << shouldIgnoreThornStyle(toplevel);
#endif
        if (shouldIgnoreThornStyle(toplevel)) {
            // Here we should apply qApp->style() recursively on widget and its children, in case one was reparented
#ifdef DEBUG_EVENTFILTER
            qDebug() << widget << widget->objectName() << "in" << toplevel << "now using app style (recursive)";
#endif
            applyStyleRecursive(widget, qApp->style());
        } else if (widget->style() != &m_style) {
#ifdef DEBUG_EVENTFILTER
            //qDebug() << "    ToolTipBase=" << widget->palette().color(QPalette::ToolTipBase).name();
#endif
            // Apply style recursively because some child widgets (e.g. QHeaderView in QTreeWidget, in DeviceManagerDialog) don't seem to get here.
            if (qobject_cast<QAbstractItemView *>(widget)) {
                applyStyleRecursive(widget, &m_style);
            } else {
                widget->setStyle(&m_style);
            }
#ifdef DEBUG_EVENTFILTER
            qDebug() << "    now using style" << widget->style();
#endif
            if (widget->windowType() != Qt::Widget) { // window, tooltip, ...
                widget->setPalette(m_style.standardPalette());
#ifdef DEBUG_EVENTFILTER
                qDebug() << "    after setPalette:     ToolTipBase=" << widget->palette().color(QPalette::ToolTipBase).name();
#endif
            } else {
#ifdef DEBUG_EVENTFILTER
                //qDebug() << "    not a toplevel. ToolTipBase=" << widget->palette().color(QPalette::ToolTipBase).name();
#endif
            }
            polishWidget(widget);
        }
        s_insidePolish = false;
    }
    return false; // don't eat the event
}

void AppEventFilter::polishWidget(QWidget *widget)
{
    if (QLabel *label = qobject_cast<QLabel *>(widget)) {
        if (qobject_cast<QToolBar *>(widget->parentWidget())) {
            /* Toolbars must be light enough for black icons, therefore black text on their
               QLabels, rather than white, is more appropriate.
               QToolBar QLabel { color: #000000; } */
            QPalette pal = label->palette();
            pal.setColor(label->foregroundRole(), Qt::black);
            label->setPalette(pal);
            //qDebug() << "made label black:" << label << label->text();
        }
        if (widget->objectName() == "SPECIAL_LABEL") {
            widget->setAutoFillBackground(true);
            // QWidget#SPECIAL_LABEL { color: #000000; background-color: #999999; }
            QPalette palette = widget->palette();
            palette.setColor(QPalette::WindowText, Qt::black);
            palette.setColor(QPalette::Window, QColor(0x99, 0x99, 0x99));
            widget->setPalette(palette);
        }
    } else if (widget->objectName() == "Rosegarden Transport") {
        // Give the non-LED parts of the dialog the groupbox "lighter black"
        // background for improved contrast.
        QPalette transportPalette = widget->palette();
        transportPalette.setColor(widget->backgroundRole(), QColor(0x40, 0x40, 0x40));
        widget->setPalette(transportPalette);
        widget->setAutoFillBackground(true);
    } else if (QCheckBox *cb = qobject_cast<QCheckBox *>(widget)) {
        cb->setAttribute(Qt::WA_Hover);
    } else if (QRadioButton *rb = qobject_cast<QRadioButton *>(widget)) {
        rb->setAttribute(Qt::WA_Hover);
    } else if (QPushButton *pb = qobject_cast<QPushButton *>(widget)) {
        pb->setAttribute(Qt::WA_Hover);
        if (qobject_cast<QDialogButtonBox *>(widget->parentWidget())) {
            // Bug in QDialogButtonBox: if the app style sets QStyle::SH_DialogButtonBox_ButtonsHaveIcons
            // a later call to setStyle() doesn't remove the button icon again.
            // Fix submitted at https://codereview.qt-project.org/183788
            pb->setIcon(QIcon());
        }
    } else if (QComboBox *cb = qobject_cast<QComboBox *>(widget)) {
        cb->setAttribute(Qt::WA_Hover);
    } else if (QAbstractSpinBox *sb = qobject_cast<QAbstractSpinBox *>(widget)) {
        sb->setAttribute(Qt::WA_Hover);
    }
}
