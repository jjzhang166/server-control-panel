/*
    WPN-XM Server Control Panel

    WPN-XM SCP is a GUI tool for managing server daemons under Windows.
    It's a fork of Easy WEMP written by Yann Le Moigne and (c) 2010.
    WPN-XM SCP is written by Jens-Andre Koch and (c) 2011 - onwards.

    This file is part of WPN-XM Server Stack for Windows.

    WPN-XM SCP is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    WPN-XM SCP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with WPN-XM SCP. If not, see <http://www.gnu.org/licenses/>.
*/

#include "splashscreen.h"
#include "version.h"

#include <QApplication>
#include <QPainter>
#include <QDateTime>
#include <QPicture>

SplashScreen::SplashScreen(const QPixmap &pixmap, Qt::WindowFlags f) :
    QSplashScreen(pixmap, f)
{
    this->setAutoFillBackground(true);
    this->setCursor(Qt::BusyCursor);

    // set reference point, paddings
    int paddingRight            = 20;
    int paddingTop              = 50;
    int titleVersionVSpace      = 17;
    int titleCopyrightVSpace    = 35;

    float fontFactor            = 1.0;

    // define texts
    QString titleText       = QString(QApplication::applicationName());
    QString versionText     = QString("Version %1").arg(QString::fromStdString(APP_VERSION));
    QString copyrightText   = QChar(0xA9)+QString(" 2010-%1 ").arg(QDate::currentDate().toString("yyyy")) + QString(tr("Jens-André Koch"));
    //QString madeinText      = QString("Made in Germany.");

    QString font            = "Arial";

    // load bitmap for writing text over it
    QPixmap newPixmap;
    newPixmap = QPixmap(":/splash");

    QPainter pixPaint(&newPixmap);
    pixPaint.setPen(QColor(100,100,100));

    // check font size and drawing width
    pixPaint.setFont(QFont(font, 33*fontFactor));
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth  = fm.width(titleText);
    if(titleTextWidth > 160) {
        // strange font rendering, Arial probably not found
        fontFactor = 0.75;
    }

    // title
    pixPaint.setFont(QFont(font, 33*fontFactor));
    titleTextWidth = pixPaint.fontMetrics().width(titleText);
    pixPaint.drawText(newPixmap.width()-titleTextWidth-paddingRight,paddingTop,titleText);

    // version
    pixPaint.setFont(QFont(font, 15*fontFactor));
    int versionTextWidth = pixPaint.fontMetrics().width(versionText);
    pixPaint.drawText(newPixmap.width()-versionTextWidth-paddingRight,paddingTop+titleVersionVSpace,versionText);

    // copyright
    pixPaint.setFont(QFont(font, 10*fontFactor));
    int copyrightTextWidth = pixPaint.fontMetrics().width(copyrightText);
    pixPaint.drawText(newPixmap.width()-copyrightTextWidth-paddingRight-2,paddingTop+titleCopyrightVSpace,copyrightText);

    // made in germany
    //int madeinTextWidth = pixPaint.fontMetrics().width(madeinText) + 5;
    //pixPaint.drawText(newPixmap.width()-madeinTextWidth,newPixmap.height()-5,madeinText);

    pixPaint.end();

    this->setPixmap(newPixmap);

    this->showMessage("Loading...", Qt::AlignBottom | Qt::AlignLeft, Qt::gray);
}