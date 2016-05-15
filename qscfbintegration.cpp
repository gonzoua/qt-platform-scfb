/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Copyright (C) 2015-2016 Oleksandr Tymoshenko <gonzo@bluezbox.com>
** Contact: http://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qscfbintegration.h"
#include "qscfbscreen.h"

#include <QtPlatformSupport/private/qgenericunixfontdatabase_p.h>
#include <QtPlatformSupport/private/qgenericunixservices_p.h>
#include <QtPlatformSupport/private/qgenericunixeventdispatcher_p.h>

#include <QtPlatformSupport/private/qfbvthandler_p.h>
#include <QtPlatformSupport/private/qfbbackingstore_p.h>
#include <QtPlatformSupport/private/qfbwindow_p.h>
#include <QtPlatformSupport/private/qfbcursor_p.h>

#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatforminputcontextfactory_p.h>

#if !defined(QT_NO_TSLIB)
#include <QtPlatformSupport/private/qtslib_p.h>
#endif

QT_BEGIN_NAMESPACE

QScFbIntegration::QScFbIntegration(const QStringList &paramList)
    : m_fontDb(new QGenericUnixFontDatabase),
      m_services(new QGenericUnixServices)
{
    m_primaryScreen = new QScFbScreen(paramList);
}

QScFbIntegration::~QScFbIntegration()
{
    destroyScreen(m_primaryScreen);
}

void QScFbIntegration::initialize()
{
    if (m_primaryScreen->initialize())
        screenAdded(m_primaryScreen);
    else
        qWarning("scfb: Failed to initialize screen");

    m_inputContext = QPlatformInputContextFactory::create();

    m_nativeInterface.reset(new QPlatformNativeInterface);

    m_vtHandler.reset(new QFbVtHandler);

    if (!qEnvironmentVariableIntValue("QT_QPA_FB_DISABLE_INPUT"))
        createInputHandlers();
}

bool QScFbIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps: return true;
    case WindowManagement: return false;
    default: return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformBackingStore *QScFbIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new QFbBackingStore(window);
}

QPlatformWindow *QScFbIntegration::createPlatformWindow(QWindow *window) const
{
    return new QFbWindow(window);
}

QAbstractEventDispatcher *QScFbIntegration::createEventDispatcher() const
{
    return createUnixEventDispatcher();
}

QList<QPlatformScreen *> QScFbIntegration::screens() const
{
    QList<QPlatformScreen *> list;
    list.append(m_primaryScreen);
    return list;
}

QPlatformFontDatabase *QScFbIntegration::fontDatabase() const
{
    return m_fontDb.data();
}

QPlatformServices *QScFbIntegration::services() const
{
    return m_services.data();
}

void QScFbIntegration::createInputHandlers()
{
#ifndef QT_NO_TSLIB
    const bool useTslib = qEnvironmentVariableIntValue("QT_QPA_FB_TSLIB");
    if (useTslib)
        new QTsLibMouseHandler(QLatin1String("TsLib"), QString());
#endif
}

QPlatformNativeInterface *QScFbIntegration::nativeInterface() const
{
    return m_nativeInterface.data();
}

QT_END_NAMESPACE
