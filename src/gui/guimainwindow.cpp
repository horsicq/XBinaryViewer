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
#include "guimainwindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QStyle>
#include <QUrl>

#include "ui_guimainwindow.h"

GuiMainWindow::GuiMainWindow(QWidget *pParent) : QMainWindow(pParent), ui(new Ui::GuiMainWindow)
{
    ui->setupUi(this);

#ifdef USE_XSIMD
    xsimd_init();
#endif

    // XYara::initialize();

    // g_pFile = nullptr;
    // g_pXInfo = nullptr;

    g_pActionOpen = nullptr;
    g_pActionClose = nullptr;
    g_pActionExit = nullptr;
    g_pActionCopyPath = nullptr;
    g_pMainToolBar = nullptr;
    g_bSplitterRestored = false;

    QPixmap logoPixmap(QStringLiteral(":/images/about.png"));
    ui->labelLogo->setPixmap(logoPixmap.scaled(QSize(190, 207), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->labelLogo->setAccessibleName(tr("XBinaryViewer logo"));
    ui->welcomeCard->setAccessibleName(tr("Open a binary file"));
    ui->pushButtonOpen->setIcon(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_OPEN)));
    ui->pushButtonOpen->setAccessibleDescription(tr("Choose a local file to inspect"));
    ui->pushButtonOpen->setToolTip(tr("Choose a local file to inspect"));
    connect(ui->pushButtonOpen, SIGNAL(clicked()), this, SLOT(actionOpenSlot()));

    ui->stackedWidget->setCurrentIndex(0);

    setWindowTitle(XOptions::getTitle(X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION));

    setAcceptDrops(true);

    g_xOptions.setName(X_OPTIONSFILE);

    g_xOptions.addID(XOptions::ID_VIEW_STYLE, "Fusion");
    g_xOptions.addID(XOptions::ID_VIEW_QSS, "");
    g_xOptions.addID(XOptions::ID_VIEW_LANG, "System");
    g_xOptions.addID(XOptions::ID_VIEW_FONT_CONTROLS, XOptions::getDefaultFont().toString());
    g_xOptions.addID(XOptions::ID_VIEW_FONT_TABLEVIEWS, XOptions::getMonoFont().toString());
    g_xOptions.addID(XOptions::ID_VIEW_FONT_TREEVIEWS, XOptions::getDefaultFont().toString());
    g_xOptions.addID(XOptions::ID_VIEW_FONT_TEXTEDITS, XOptions::getMonoFont().toString());
    g_xOptions.addID(XOptions::ID_VIEW_STAYONTOP, false);
    g_xOptions.addID(XOptions::ID_VIEW_SHOWLOGO, true);
    g_xOptions.addID(XOptions::ID_FILE_SAVELASTDIRECTORY, true);
    g_xOptions.addID(XOptions::ID_FILE_SAVEBACKUP, true);
    g_xOptions.addID(XOptions::ID_FILE_SAVERECENTFILES, true);
    g_xOptions.addID(XOptions::ID_VIEW_SIZES, "");

    g_xOptions.addID(XOptions::ID_FEATURE_READBUFFERSIZE, 8 * 1024);
    g_xOptions.addID(XOptions::ID_FEATURE_FILEBUFFERSIZE, 2 * 1024 * 1024);

#ifdef USE_XSIMD
#ifdef Q_PROCESSOR_X86
    g_xOptions.addID(XOptions::ID_FEATURE_SSE2, true);
    g_xOptions.addID(XOptions::ID_FEATURE_AVX2, true);
#endif
#endif

#ifdef Q_OS_WIN
    g_xOptions.addID(XOptions::ID_FILE_CONTEXT, "*");
#endif

    g_xOptions.addID(XOptions::ID_SCAN_ENGINE_DIE_ENABLED, true);
    g_xOptions.addID(XOptions::ID_SCAN_ENGINE_YARA_ENABLED, true);

    XScanEngineOptionsWidget::setDefaultValues(&g_xOptions);
    SearchSignaturesOptionsWidget::setDefaultValues(&g_xOptions);
    XHexViewOptionsWidget::setDefaultValues(&g_xOptions);
    XDisasmViewOptionsWidget::setDefaultValues(&g_xOptions);
    XOnlineToolsOptionsWidget::setDefaultValues(&g_xOptions);
    XInfoDBOptionsWidget::setDefaultValues(&g_xOptions);

    g_xOptions.load();

    g_xShortcuts.setName(X_SHORTCUTSFILE);
    g_xShortcuts.setNative(g_xOptions.isNative());

    g_xShortcuts.addGroup(XShortcuts::GROUPID_HEX);
    g_xShortcuts.addGroup(XShortcuts::GROUPID_DISASM);
    g_xShortcuts.addGroup(XShortcuts::GROUPID_TABLE);

    g_xShortcuts.addId(X_ID_FILE_OPEN);
    g_xShortcuts.addId(X_ID_FILE_CLOSE);
    g_xShortcuts.addId(X_ID_FILE_EXIT);

    g_xShortcuts.load();

    // g_pInfoMenu = new XInfoMenu(&g_xShortcuts, &g_xOptions);

    ui->widgetViewer->setGlobal(&g_xShortcuts, &g_xOptions);

    connect(&g_xOptions, SIGNAL(openFile(QString)), this, SLOT(processFile(QString)));
    connect(&g_xOptions, SIGNAL(errorMessage(QString)), this, SLOT(errorMessageSlot(QString)));
    connect(ui->widgetViewer, SIGNAL(headerSelected(XBinary::XFHEADER)), this, SLOT(onViewerHeaderSelected(XBinary::XFHEADER)));

    createMenus();
    updateShortcuts();

    g_pLabelFile = new QLabel(this);
    g_pLabelStructure = new QLabel(this);
    g_pLabelSize = new QLabel(this);
    g_pLabelType = new QLabel(this);

    g_pLabelFile->setObjectName(QStringLiteral("statusFile"));
    g_pLabelStructure->setObjectName(QStringLiteral("statusSelection"));
    g_pLabelSize->setObjectName(QStringLiteral("statusSize"));
    g_pLabelType->setObjectName(QStringLiteral("statusType"));
    g_pLabelStructure->setProperty("statusSegment", true);
    g_pLabelSize->setProperty("statusSegment", true);
    g_pLabelType->setProperty("statusSegment", true);
    g_pLabelFile->setAccessibleName(tr("Current file"));
    g_pLabelStructure->setAccessibleName(tr("Current selection"));
    g_pLabelSize->setAccessibleName(tr("File size"));
    g_pLabelType->setAccessibleName(tr("Detected file type"));
    g_pLabelFile->setTextInteractionFlags(Qt::TextSelectableByMouse);
    g_pLabelStructure->setTextInteractionFlags(Qt::TextSelectableByMouse);
    g_pLabelFile->setText(tr("Ready — open or drop a file"));
    g_pLabelFile->setToolTip(tr("No file is open"));

    ui->statusbar->addWidget(g_pLabelFile, 1);
    ui->statusbar->addPermanentWidget(g_pLabelStructure);
    ui->statusbar->addPermanentWidget(g_pLabelSize);
    ui->statusbar->addPermanentWidget(g_pLabelType);

    adjustView();

    {
        QByteArray baGeometry = g_xOptions.getSizeRecord("MainWindow");

        if (!baGeometry.isEmpty()) {
            restoreGeometry(baGeometry);
        }
    }

    if (QCoreApplication::arguments().count() > 1) {
        QString sFileName = QCoreApplication::arguments().at(1);

        processFile(sFileName);
    }
}

GuiMainWindow::~GuiMainWindow()
{
    closeCurrentFile();
    g_xOptions.save();
    g_xShortcuts.save();

    // delete g_pInfoMenu;
    delete ui;

    // XYara::finalize();

#ifdef USE_XSIMD
    xsimd_cleanup();
#endif
}

void GuiMainWindow::createMenus()
{
    QMenu *pMenuFile = new QMenu(tr("&File"), ui->menubar);
    QMenu *pMenuTools = new QMenu(tr("&Tools"), ui->menubar);
    QMenu *pMenuHelp = new QMenu(tr("&Help"), ui->menubar);

    ui->menubar->addAction(pMenuFile->menuAction());
    ui->menubar->addAction(pMenuTools->menuAction());
    ui->menubar->addAction(pMenuHelp->menuAction());

    g_pActionOpen = new QAction(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_OPEN)), tr("&Open File..."), this);
    g_pActionClose = new QAction(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_REMOVE)), tr("&Close File"), this);
    g_pActionExit = new QAction(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_EXIT)), tr("E&xit"), this);
    g_pActionCopyPath = new QAction(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_COPY)), tr("Copy File &Path"), this);
    g_pActionOpen->setStatusTip(tr("Open a local file for binary analysis"));
    g_pActionClose->setStatusTip(tr("Close the current file"));
    g_pActionCopyPath->setStatusTip(tr("Copy the full path of the current file"));
    g_pActionExit->setStatusTip(tr("Exit XBinaryViewer"));
    g_pActionClose->setEnabled(false);
    g_pActionCopyPath->setEnabled(false);
    g_pActionExit->setMenuRole(QAction::QuitRole);

    QAction *pActionOptions = new QAction(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_OPTION)), tr("&Options..."), this);
    QAction *pActionAbout = new QAction(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_INFO)), tr("&About XBinaryViewer"), this);
    QAction *pActionShortcuts = new QAction(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_SHORTCUT)), tr("&Keyboard Shortcuts..."), this);
    QAction *pActionDemangle = new QAction(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_DEMANGLE)), tr("&Demangle Symbol..."), this);
    pActionOptions->setStatusTip(tr("Configure appearance, analysis, and file handling"));
    pActionShortcuts->setStatusTip(tr("Review and customize keyboard shortcuts"));
    pActionDemangle->setStatusTip(tr("Convert a mangled symbol to a readable name"));
    pActionAbout->setStatusTip(tr("Show version, project, and contributor information"));
    pActionOptions->setMenuRole(QAction::PreferencesRole);
    pActionAbout->setMenuRole(QAction::AboutRole);

    pMenuFile->addAction(g_pActionOpen);
    QMenu *pRecentFilesMenu = g_xOptions.createRecentFilesMenu(this);
    pRecentFilesMenu->setTitle(tr("Open &Recent"));
    pRecentFilesMenu->setIcon(QIcon(XOptions::getIconPath(XOptions::ICONTYPE_FILE)));
    pMenuFile->addMenu(pRecentFilesMenu);
    pMenuFile->addSeparator();
    // pMenuFile->addMenu(g_pInfoMenu->createMenu(this));
    pMenuFile->addAction(g_pActionCopyPath);
    pMenuFile->addAction(g_pActionClose);
    pMenuFile->addSeparator();
    pMenuFile->addAction(g_pActionExit);
    pMenuTools->addAction(pActionDemangle);
    pMenuTools->addAction(pActionShortcuts);
    pMenuTools->addSeparator();
    pMenuTools->addAction(pActionOptions);
    pMenuHelp->addAction(pActionAbout);

    g_pMainToolBar = addToolBar(tr("Main toolbar"));
    g_pMainToolBar->setObjectName(QStringLiteral("mainToolBar"));
    g_pMainToolBar->setWindowTitle(tr("Main toolbar"));
    g_pMainToolBar->setMovable(false);
    g_pMainToolBar->setFloatable(false);
    g_pMainToolBar->setIconSize(QSize(16, 16));
    g_pMainToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    g_pMainToolBar->addAction(g_pActionOpen);
    g_pMainToolBar->addAction(g_pActionClose);
    g_pMainToolBar->addAction(g_pActionCopyPath);
    g_pMainToolBar->addSeparator();
    g_pMainToolBar->addAction(pActionOptions);

    connect(g_pActionOpen, SIGNAL(triggered()), this, SLOT(actionOpenSlot()));
    connect(g_pActionClose, SIGNAL(triggered()), this, SLOT(actionCloseSlot()));
    connect(g_pActionExit, SIGNAL(triggered()), this, SLOT(actionExitSlot()));
    connect(g_pActionCopyPath, SIGNAL(triggered()), this, SLOT(actionCopyPathSlot()));
    connect(pActionOptions, SIGNAL(triggered()), this, SLOT(actionOptionsSlot()));
    connect(pActionAbout, SIGNAL(triggered()), this, SLOT(actionAboutSlot()));
    connect(pActionShortcuts, SIGNAL(triggered()), this, SLOT(actionShortcutsSlot()));
    connect(pActionDemangle, SIGNAL(triggered()), this, SLOT(actionDemangleSlot()));
}

void GuiMainWindow::updateShortcuts()
{
    g_pActionOpen->setShortcut(g_xShortcuts.getShortcut(X_ID_FILE_OPEN));
    g_pActionClose->setShortcut(g_xShortcuts.getShortcut(X_ID_FILE_CLOSE));
    g_pActionExit->setShortcut(g_xShortcuts.getShortcut(X_ID_FILE_EXIT));
}

void GuiMainWindow::actionCopyPathSlot()
{
    if (!g_sCurrentFilePath.isEmpty()) {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(g_sCurrentFilePath));
    }
}

void GuiMainWindow::onViewerHeaderSelected(const XBinary::XFHEADER &xfHeader)
{
    // Command nodes (Hex, Strings, Entropy...) are tools, not file structures - no offset to show
    if (xfHeader.xfType == XBinary::XFTYPE_COMMAND) {
        g_pLabelStructure->clear();
        return;
    }

    QString sText = tr("Selection") + QString(" — ") + tr("Offset") + QString(": 0x%1").arg(QString::number(xfHeader.xLoc.nLocation, 16));

    if (xfHeader.nSize > 0) {
        sText += QString(" ") + tr("Size") + QString(": 0x%1").arg(QString::number(xfHeader.nSize, 16));
    }

    g_pLabelStructure->setText(sText);
}

void GuiMainWindow::errorMessageSlot(const QString &sText)
{
    QMessageBox::critical(this, tr("Error"), sText);
}

void GuiMainWindow::actionOpenSlot()
{
    QString sDirectory = g_xOptions.getLastDirectory();

    QString sFileName = QFileDialog::getOpenFileName(this, tr("Open file") + QString("..."), sDirectory, tr("All files") + QString(" (*)"));

    if (!sFileName.isEmpty()) {
        processFile(sFileName);
    }
}

void GuiMainWindow::actionCloseSlot()
{
    closeCurrentFile();
}

void GuiMainWindow::actionExitSlot()
{
    this->close();
}

void GuiMainWindow::actionOptionsSlot()
{
    DialogOptions dialogOptions(this, &g_xOptions, XOptions::GROUPID_FILE);
    dialogOptions.setGlobal(&g_xShortcuts, &g_xOptions);
    dialogOptions.exec();

    adjustView();
}

void GuiMainWindow::actionAboutSlot()
{
    DialogAbout dialogAbout(this);
    dialogAbout.setGlobal(&g_xShortcuts, &g_xOptions);
    dialogAbout.exec();
}

void GuiMainWindow::adjustView()
{
    ui->widgetViewer->adjustView();

    g_xOptions.adjustStayOnTop(this);
    g_xOptions.adjustWidget(this, XOptions::ID_VIEW_FONT_CONTROLS);

    if (g_xOptions.isShowLogo()) {
        ui->labelLogo->show();
    } else {
        ui->labelLogo->hide();
    }
}

void GuiMainWindow::processFile(const QString &sFileName)
{
    if ((sFileName != "") && (QFileInfo(sFileName).isFile())) {
        g_xOptions.setLastFileName(sFileName);

        closeCurrentFile();

        XFormats::INDATA inData = XFormats::createINDATA(XFormats::getPrefFileType(sFileName, XBinary::FT_FLAG_FORMATS), sFileName);

        // g_pFile = new QFile;
        // // g_pXInfo = new XInfoDB;

        // g_pFile->setFileName(sFileName);

        // if (!g_pFile->open(QIODevice::ReadWrite)) {
        //     if (!g_pFile->open(QIODevice::ReadOnly)) {
        //         closeCurrentFile();
        //     }
        // }

        // if (!g_pFile->open(QIODevice::ReadOnly)) {
        //     closeCurrentFile();
        // }

        // if (g_pFile) {
        //     XFWidgetAdvanced::OPTIONS formatOptions = {};

        //     ui->widgetViewer->setData(g_pFile, formatOptions);

        //     adjustView();

        //     setWindowTitle(sFileName);
        //     ui->stackedWidget->setCurrentIndex(1);
        //     // XBinary xbinary(g_pFile);
        //     // if (xbinary.isValid()) {
        //     //     // g_pInfoMenu->setData(g_pXInfo, g_pFile, sFileName + ".db");
        //     //     // g_pInfoMenu->tryToLoad();

        //     //     // XFW_DEF::OPTIONS formatOptions = {};

        //     //     // formatOptions.bIsImage = false;
        //     //     // formatOptions.nImageBase = -1;
        //     //     // formatOptions.vmode = XFW_DEF::VMODE_FILETYPE;
        //     //     // // formatOptions.nStartType = SBINARY::TYPE_INFO;

        //     //     // XMainWidget::OPTIONS formatOptions = {};
        //     //     // formatOptions.bIsImage = false;
        //     //     // formatOptions.nImageBase = -1;
        //     //     // formatOptions.bGlobalHexEnable = true;

        //     //     // ui->widgetViewer->setData(g_pFile, g_pXInfo, formatOptions);

        //     //     // ui->widgetViewer->reload();

                
        //     // } else {
        //     //     QMessageBox::critical(this, tr("Error"), tr("It is not a valid file"));
        //     // }
        // } else {
        //     QMessageBox::critical(this, tr("Error"), tr("Cannot open file"));
        // }

        XFWidgetAdvanced::OPTIONS formatOptions = {};

        ui->widgetViewer->setData(inData, formatOptions);

        adjustView();

        g_sCurrentFilePath = sFileName;
        g_pActionClose->setEnabled(true);
        g_pActionCopyPath->setEnabled(true);

        setWindowTitle(XOptions::getTitle(X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION) + QString(" - ") + QDir::toNativeSeparators(sFileName));
        setWindowFilePath(sFileName);

        QString sNativePath = QDir::toNativeSeparators(sFileName);
        g_pLabelFile->setText(tr("File") + QString(": ") + sNativePath);
        g_pLabelFile->setToolTip(sNativePath);
        g_pLabelSize->setText(tr("Size") + QString(": ") + XBinary::bytesCountToString(QFileInfo(sFileName).size()));
        g_pLabelType->setText(tr("Type") + QString(": ") + XBinary::fileTypeIdToString(inData.fileType));

        ui->stackedWidget->setCurrentIndex(1);

        if (!g_bSplitterRestored) {
            ui->widgetViewer->restoreSplitterState(g_xOptions.getSizeRecord("Splitter"));
            g_bSplitterRestored = true;
        }
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Cannot open file") + QString(": %1").arg(QDir::toNativeSeparators(sFileName)));
    }
}

void GuiMainWindow::closeCurrentFile()
{
    // if (g_pXInfo) {
    //     g_pInfoMenu->tryToSave();

    //     delete g_pXInfo;
    //     g_pXInfo = nullptr;
    //     g_pInfoMenu->reset();
    // }

    // if (g_pFile) {
    //     g_pFile->close();
    //     delete g_pFile;
    //     g_pFile = nullptr;
    // }

    ui->stackedWidget->setCurrentIndex(0);
    ui->widgetViewer->clear();

    g_sCurrentFilePath = "";

    if (g_pActionCopyPath) {
        g_pActionCopyPath->setEnabled(false);
    }

    if (g_pActionClose) {
        g_pActionClose->setEnabled(false);
    }

    if (g_pLabelFile) {
        g_pLabelFile->setText(tr("Ready — open or drop a file"));
        g_pLabelFile->setToolTip(tr("No file is open"));
        g_pLabelSize->clear();
        g_pLabelType->clear();
        g_pLabelStructure->clear();
    }

    setWindowFilePath("");
    setWindowTitle(XOptions::getTitle(X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION));
}

void GuiMainWindow::closeEvent(QCloseEvent *pEvent)
{
    // All window/widget sizes go through XOptions (serialized into ID_VIEW_SIZES),
    // so they honor the same native-vs-portable storage as every other option.
    g_xOptions.setSizeRecord("MainWindow", saveGeometry());

    // Only persist the splitter if a file was actually shown this session;
    // otherwise the never-laid-out default sizes would clobber the saved state.
    if (g_bSplitterRestored) {
        g_xOptions.setSizeRecord("Splitter", ui->widgetViewer->saveSplitterState());
    }

    QMainWindow::closeEvent(pEvent);
}

static bool _isLocalFileDrag(const QMimeData *pMimeData)
{
    bool bResult = false;

    if (pMimeData->hasUrls()) {
        QList<QUrl> urlList = pMimeData->urls();

        if (urlList.count() && urlList.at(0).isLocalFile() && QFileInfo(urlList.at(0).toLocalFile()).isFile()) {
            bResult = true;
        }
    }

    return bResult;
}

void GuiMainWindow::dragEnterEvent(QDragEnterEvent *pEvent)
{
    if (_isLocalFileDrag(pEvent->mimeData())) {
        pEvent->acceptProposedAction();
    }
}

void GuiMainWindow::dragMoveEvent(QDragMoveEvent *pEvent)
{
    if (_isLocalFileDrag(pEvent->mimeData())) {
        pEvent->acceptProposedAction();
    }
}

void GuiMainWindow::dropEvent(QDropEvent *pEvent)
{
    const QMimeData *mimeData = pEvent->mimeData();

    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();

        if (urlList.count()) {
            QString sFileName = urlList.at(0).toLocalFile();

            if (!sFileName.isEmpty()) {
                sFileName = XBinary::convertFileName(sFileName);

                pEvent->acceptProposedAction();

                processFile(sFileName);
            }
        }
    }
}

void GuiMainWindow::actionShortcutsSlot()
{
    DialogShortcuts dialogShortcuts(this);
    dialogShortcuts.setGlobal(&g_xShortcuts, &g_xOptions);
    dialogShortcuts.setData(&g_xShortcuts);

    dialogShortcuts.exec();

    adjustView();
    updateShortcuts();
}

void GuiMainWindow::actionDemangleSlot()
{
    DialogDemangle dialogDemangle(this);
    dialogDemangle.setGlobal(&g_xShortcuts, &g_xOptions);

    dialogDemangle.exec();
}
