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
#include "dialogabout.h"

#include <QPixmap>

#include "ui_dialogabout.h"

DialogAbout::DialogAbout(QWidget *pParent) : XShortcutsDialog(pParent), ui(new Ui::DialogAbout)
{
    ui->setupUi(this);

    setWindowTitle(tr("About %1").arg(X_APPLICATIONDISPLAYNAME));
    setWindowModality(Qt::WindowModal);

    QPixmap logoPixmap(QStringLiteral(":/images/about.png"));
    ui->labelLogo->setPixmap(logoPixmap.scaled(QSize(210, 228), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->labelLogo->setAccessibleName(tr("XBinaryViewer logo"));

    ui->labelVersion->setText(QString("<span style=\"font-size:18px; font-weight:600;\">%1</span>").arg(XOptions::getTitle(X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION)));
    ui->labelCopyright->setText(tr("Copyright (C) 2019-2026 hors"));

    ui->labelBugreports->setText(QString("<b>%1:</b> <a href=\"mailto:horsicq@gmail.com\">horsicq@gmail.com</a>").arg(tr("Bug reports")));
    ui->labelWebsite->setText(QString("<b>%1:</b> <a href=\"https://ntinfo.biz\">ntinfo.biz</a>").arg(tr("Website")));
    ui->labelDonate->setText(QString("<b>%1 (PayPal):</b> <a href=\"mailto:ntinfo.re@gmail.com\">ntinfo.re@gmail.com</a>").arg(tr("Donate")));
    ui->labelSourceCode->setText(
        QString("<b>%1:</b> <a href=\"https://github.com/horsicq/XBinaryViewer\">github.com/horsicq/XBinaryViewer</a>").arg(tr("Source code")));
    ui->labelThanks->setText(
        QString("<html><head/><body>"
                "<p align=\"center\"><span style=\" font-weight:600;\">%1:</span></p>"
                "<p align=\"center\">"
                "<a href=\"https://www.mentebinaria.com.br/\">Fernando Mercês</a>, "
                "<a href=\"https://sandsprite.com/\">David Zimmer</a>, "
                "<a href=\"https://github.com/miso-xyz\">misonothx</a>, "
                "</p>"
                "<p align=\"center\">"
                "<a href=\"https://twitter.com/frenchyeti\">FrenchYeti</a>, "
                "<a href=\"https://github.com/fr0zenbag\">fr0zenbag</a>, "
                "<a href=\"https://github.com/AandersonL\">Anderson Leite</a>, "
                "</p>"
                "<p align=\"center\">"
                "<a href=\"https://github.com/filipnavara\">Filip Navara</a>, "
                "<a href=\"https://www.ashemery.com/\">Ali Hadi</a>, "
                "<a href=\"https://mrexodia.re/\">Duncan Ogilvie</a>, "
                "</p>"
                "<p align=\"center\">"
                "<a href=\"https://github.com/leandrofroes\">Leandro Fróes</a>, "
                "<a href=\"https://www.leavesongs.com/\">phithon</a>, "
                "<a href=\"https://github.com/clayne/\">Christopher Layne</a>, "
                "</p>"
                "<p align=\"center\">"
                "<a href=\"https://dfirnotes.net/\">Adric Net</a>, "
                "<a href=\"https://greich.com/\">Gilad Reich</a>"
                "</p>"
                "</body></html>")
            .arg(tr("Thanks")));

    ui->pushButtonOK->setAccessibleName(tr("Close About dialog"));
    ui->pushButtonOK->setFocus();
}

DialogAbout::~DialogAbout()
{
    delete ui;
}

void DialogAbout::adjustView()
{
    // TODO
}

void DialogAbout::on_pushButtonOK_clicked()
{
    accept();
}
