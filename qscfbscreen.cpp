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

#include "qscfbscreen.h"
#include <QtPlatformSupport/private/qfbcursor_p.h>
#include <QtPlatformSupport/private/qfbwindow_p.h>
#include <QtCore/QRegularExpression>
#include <QtGui/QPainter>

#include <private/qcore_unix_p.h> // overrides QT_OPEN
#include <qimage.h>
#include <qdebug.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>

#include <sys/consio.h>
#include <sys/fbio.h>

QT_BEGIN_NAMESPACE

static int openFramebufferDevice(const QString &dev)
{
    int fd = -1;

    if (access(dev.toLatin1().constData(), R_OK|W_OK) == 0)
        fd = QT_OPEN(dev.toLatin1().constData(), O_RDWR);

    if (fd == -1) {
        if (access(dev.toLatin1().constData(), R_OK) == 0)
            fd = QT_OPEN(dev.toLatin1().constData(), O_RDONLY);
    }

    return fd;
}

static QRect determineGeometry(const struct fbtype &fb, const QRect &userGeometry)
{
    int xoff = 0;
    int yoff = 0;
    int w, h;
    if (userGeometry.isValid()) {
        w = userGeometry.width();
        h = userGeometry.height();
        if (w > fb.fb_width)
            w = fb.fb_width;
        if (h > fb.fb_height)
            h = fb.fb_height;

        int xxoff = userGeometry.x(), yyoff = userGeometry.y();
        if (xxoff != 0 || yyoff != 0) {
            if (xxoff < 0 || xxoff + w > fb.fb_width)
                xxoff = fb.fb_width - w;
            if (yyoff < 0 || yyoff + h > fb.fb_height)
                yyoff = fb.fb_height - h;
            xoff += xxoff;
            yoff += yyoff;
        } else {
            xoff += (fb.fb_width - w)/2;
            yoff += (fb.fb_height - h)/2;
        }
    } else {
        w = fb.fb_width;
        h = fb.fb_height;
    }

    if (w == 0 || h == 0) {
        qWarning("Unable to find screen geometry, using 320x240");
        w = 320;
        h = 240;
    }

    return QRect(xoff, yoff, w, h);
}

static QSizeF determinePhysicalSize(const QSize &mmSize, const QSize &res)
{
    int mmWidth = mmSize.width(), mmHeight = mmSize.height();

    if (mmWidth <= 0 && mmHeight <= 0) {
        const int dpi = 100;
        mmWidth = qRound(res.width() * 25.4 / dpi);
        mmHeight = qRound(res.height() * 25.4 / dpi);
    } else if (mmWidth > 0 && mmHeight <= 0) {
        mmHeight = res.height() * mmWidth/res.width();
    } else if (mmHeight > 0 && mmWidth <= 0) {
        mmWidth = res.width() * mmHeight/res.height();
    }

    return QSize(mmWidth, mmHeight);
}

QScFbScreen::QScFbScreen(const QStringList &args)
    : mArgs(args), mFbFd(-1), mBlitter(0)
{
}

QScFbScreen::~QScFbScreen()
{
    if (mFbFd != -1) {
        munmap(mMmap.data - mMmap.offset, mMmap.size);
        close(mFbFd);
    }

    delete mBlitter;
}

bool QScFbScreen::initialize()
{
    QRegularExpression fbRx(QLatin1String("fb=(.*)"));
    QRegularExpression mmSizeRx(QLatin1String("mmsize=(\\d+)x(\\d+)"));
    QRegularExpression sizeRx(QLatin1String("size=(\\d+)x(\\d+)"));
    QRegularExpression offsetRx(QLatin1String("offset=(\\d+)x(\\d+)"));

    QString fbDevice;
    QSize userMmSize;
    QRect userGeometry;

    // Parse arguments
    foreach (const QString &arg, mArgs) {
        QRegularExpressionMatch match;
        if (arg.contains(mmSizeRx, &match))
            userMmSize = QSize(match.captured(1).toInt(), match.captured(2).toInt());
        else if (arg.contains(sizeRx, &match))
            userGeometry.setSize(QSize(match.captured(1).toInt(), match.captured(2).toInt()));
        else if (arg.contains(offsetRx, &match))
            userGeometry.setTopLeft(QPoint(match.captured(1).toInt(), match.captured(2).toInt()));
        else if (arg.contains(fbRx, &match))
            fbDevice = match.captured(1);
    }

    if (!fbDevice.isEmpty()) {
        if (!QFile::exists(fbDevice)) {
            qWarning("Unable to figure out framebuffer device. Specify it manually.");
            return false;
        }

        // Open the device
        mFbFd = openFramebufferDevice(fbDevice);
    }
    else
        mFbFd = STDIN_FILENO;

    if (mFbFd == -1) {
        qErrnoWarning(errno, "Failed to open framebuffer %s", qPrintable(fbDevice));
        return false;
    }

    struct fbtype fb;
    int line_length;

    if (ioctl(mFbFd, FBIOGTYPE, &fb) != 0) {
        qErrnoWarning(errno, "Error reading framebuffer information");
        return false;
    }

    if (ioctl(mFbFd, FBIO_GETLINEWIDTH, &line_length) != 0) {
        qErrnoWarning(errno, "Error reading line length information");
        return false;
    }

    mDepth = fb.fb_depth;

    mBytesPerLine = line_length;
    // TODO: userGeometry here?
    QRect geometry = determineGeometry(fb, userGeometry);
    mGeometry = QRect(QPoint(0, 0), geometry.size());
    switch (mDepth) {
        case 32:
            mFormat = QImage::Format_RGB32;
            break;
        case 24:
            mFormat = QImage::Format_RGB888;
            break;
        case 16:
        default:
            mFormat = QImage::Format_RGB16;
            break;
    }
    mPhysicalSize = determinePhysicalSize(userMmSize, geometry.size());

    // mmap the framebuffer
    int pagemask = getpagesize() - 1;
    mMmap.size = ((int) mBytesPerLine*fb.fb_height + pagemask) & ~pagemask;
    uchar *data = (unsigned char *)mmap(0, mMmap.size, PROT_READ | PROT_WRITE, MAP_SHARED, mFbFd, 0);
    if ((long)data == -1) {
        qErrnoWarning(errno, "Failed to mmap framebuffer");
        return false;
    }

    mMmap.offset = geometry.y() * mBytesPerLine + geometry.x() * mDepth / 8;
    mMmap.data = data + mMmap.offset;

    QFbScreen::initializeCompositor();
    mFbScreenImage = QImage(mMmap.data, geometry.width(), geometry.height(), mBytesPerLine, mFormat);

    mCursor = new QFbCursor(this);

    return true;
}

QRegion QScFbScreen::doRedraw()
{
    QRegion touched = QFbScreen::doRedraw();

    if (touched.isEmpty())
        return touched;

    if (!mBlitter)
        mBlitter = new QPainter(&mFbScreenImage);

    QVector<QRect> rects = touched.rects();
    for (int i = 0; i < rects.size(); i++)
        mBlitter->drawImage(rects[i], *mScreenImage, rects[i]);
    return touched;
}

// grabWindow() grabs "from the screen" not from the backingstores.
QPixmap QScFbScreen::grabWindow(WId wid, int x, int y, int width, int height) const
{
    if (!wid) {
        if (width < 0)
            width = mFbScreenImage.width() - x;
        if (height < 0)
            height = mFbScreenImage.height() - y;
        return QPixmap::fromImage(mFbScreenImage).copy(x, y, width, height);
    }

    QFbWindow *window = windowForId(wid);
    if (window) {
        const QRect geom = window->geometry();
        if (width < 0)
            width = geom.width() - x;
        if (height < 0)
            height = geom.height() - y;
        QRect rect(geom.topLeft() + QPoint(x, y), QSize(width, height));
        rect &= window->geometry();
        return QPixmap::fromImage(mFbScreenImage).copy(rect);
    }

    return QPixmap();
}

QT_END_NAMESPACE

