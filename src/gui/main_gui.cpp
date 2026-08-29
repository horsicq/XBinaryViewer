/* Copyright (c) 2019-2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

#include "guimainwindow.h"

static void applyDefaultTheme()
{
    QPalette palette;

    palette.setColor(QPalette::Window, QColor("#f4f7fb"));
    palette.setColor(QPalette::WindowText, QColor("#1f2937"));
    palette.setColor(QPalette::Base, QColor("#ffffff"));
    palette.setColor(QPalette::AlternateBase, QColor("#f8fafc"));
    palette.setColor(QPalette::ToolTipBase, QColor("#172033"));
    palette.setColor(QPalette::ToolTipText, QColor("#ffffff"));
    palette.setColor(QPalette::Text, QColor("#1f2937"));
    palette.setColor(QPalette::Button, QColor("#ffffff"));
    palette.setColor(QPalette::ButtonText, QColor("#1f2937"));
    palette.setColor(QPalette::BrightText, QColor("#b42318"));
    palette.setColor(QPalette::Link, QColor("#245fca"));
    palette.setColor(QPalette::LinkVisited, QColor("#6941c6"));
    palette.setColor(QPalette::Highlight, QColor("#2f6feb"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Light, QColor("#ffffff"));
    palette.setColor(QPalette::Midlight, QColor("#edf1f7"));
    palette.setColor(QPalette::Mid, QColor("#cbd5e1"));
    palette.setColor(QPalette::Dark, QColor("#64748b"));
    palette.setColor(QPalette::Shadow, QColor("#0f172a"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    palette.setColor(QPalette::PlaceholderText, QColor("#718096"));
#endif

    qApp->setPalette(palette);

    QFile styleSheetFile(QStringLiteral(":/styles/modern.qss"));

    if (styleSheetFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(styleSheetFile.readAll()));
    }
}

int main(int argc, char *argv[])
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#endif
#ifdef Q_OS_MAC
#ifndef QT_DEBUG
    QCoreApplication::setLibraryPaths(QStringList(QString(argv[0]).remove("MacOS/XBinaryViewer") + "PlugIns"));
#endif
#endif
    QCoreApplication::setOrganizationName(X_ORGANIZATIONNAME);
    QCoreApplication::setOrganizationDomain(X_ORGANIZATIONDOMAIN);
    QCoreApplication::setApplicationName(X_APPLICATIONNAME);
    QCoreApplication::setApplicationVersion(X_APPLICATIONVERSION);

    if ((argc == 2) && ((QString(argv[1]) == "--version") || (QString(argv[1]) == "-v"))) {
        QString sInfo = QString("%1 v%2").arg(X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
        printf("%s\n", sInfo.toUtf8().data());

        return 0;
    }

#ifndef QT_DEBUG
    qputenv("QT_LOGGING_RULES", "qt.*=false");
#endif

    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(QStringLiteral(":/images/app-icon.png")));

    XOptions xOptions;

    xOptions.setName(X_OPTIONSFILE);

    xOptions.addID(XOptions::ID_VIEW_LANG, "System");
    xOptions.addID(XOptions::ID_VIEW_QSS);
    xOptions.addID(XOptions::ID_VIEW_STYLE, "Fusion");

    xOptions.load();

    XOptions::adjustApplicationView(X_APPLICATIONNAME, &xOptions);

    // Keep explicit user themes untouched. The bundled default provides a
    // polished, complete baseline even in a portable/debug build where the
    // optional external QSS database is not installed.
    if (xOptions.getValue(XOptions::ID_VIEW_QSS).toString().isEmpty()) {
        applyDefaultTheme();
    }

    GuiMainWindow w;
    w.show();

    return a.exec();
}
