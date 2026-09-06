/* Copyright (c) 2026 hors<horsicq@gmail.com>
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
#ifndef XBVSMOKE_H
#define XBVSMOKE_H

// In-process GUI smoke test. Compiled into XBinaryViewer only when the CMake
// option XBV_SMOKE_TEST is ON, and activated by the XBV_SMOKE environment
// variable (see optionsFromEnvironment()), so the shipping binary's argument
// handling is untouched.
//
// The driver walks the real GuiMainWindow exactly as a user would:
//   - opens every sample file (processFile), by drag-and-drop for the first,
//   - selects every node of the structure tree, which creates every detail
//     panel (XFWidget_* / header / table),
//   - toggles, cycles, steps and clicks every visible control in each panel,
//   - requests the context menu of every view and inventories its actions,
//   - cycles the "Interpret as" combo and presses Reload,
//   - triggers every menu action and walks the resulting dialog (Options pages,
//     tabs, About, Shortcuts, Demangle, file dialogs),
// while a sentinel timer answers any modal QMessageBox with its negative
// button, rejects file dialogs and closes stray popups, so the run never
// blocks on user input.
//
// Every step is written as one JSON line (flushed immediately, so a crash is
// attributable to the last line); widgets are screenshotted once per class
// per file; and generic layout checks (clipped text, overlapping siblings,
// children outside their parent, selection over-painting in list widgets)
// are reported as "check" records. Qt runtime warnings are captured as well.

#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>

class GuiMainWindow;
class QAction;
class QDialog;
class QListWidget;
class QMenu;
class QWidget;
class XFWidgetAdvanced;

class XBVSmoke : public QObject {
    Q_OBJECT

public:
    struct OPTIONS {
        QStringList listFiles;
        QString sReportDir;
        bool bScreenshots = true;
        bool bControls = true;
        bool bContextMenus = true;
        bool bInterpret = true;
        bool bDialogs = true;
        bool bDrop = true;
        qint32 nMaxNodes = 4000;
        qint32 nStepTimeoutMs = 90000;
        QStringList listSkipNodes;
    };

    explicit XBVSmoke(GuiMainWindow *pMainWindow, const OPTIONS &options, QObject *pParent = nullptr);
    ~XBVSmoke();

    // Reads XBV_SMOKE (report directory; presence enables the mode),
    // XBV_SMOKE_FILES (';'-separated sample list) or XBV_SMOKE_LIST (a text
    // file, one sample per line), XBV_SMOKE_FLAGS (','-separated: noscreens,
    // nocontrols, nomenus, nointerpret, nodialogs, nodrop), XBV_SMOKE_MAXNODES,
    // XBV_SMOKE_TIMEOUT (ms) and XBV_SMOKE_SKIP (';'-separated node names).
    static bool optionsFromEnvironment(OPTIONS *pOptions);

    // Runs the whole sweep synchronously (pumping the event loop) and returns
    // the process exit code: 0 when no check failed, else the failure count.
    int run();

protected:
    bool eventFilter(QObject *pObject, QEvent *pEvent) override;

private slots:
    void onSentinel();

private:
    // reporting
    void record(const QString &sKind, QJsonObject object);
    void fail(const QString &sWhat, const QString &sDetail = QString());
    void check(bool bOk, const QString &sWhat, const QString &sDetail = QString());
    void setStep(const QString &sStep);
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &sMessage);

    // event pumping
    void settle(qint32 nMinMs, qint32 nMaxMs);
    bool waitModalGone(qint32 nMaxMs);

    // walkers
    void runFile(qint32 nIndex, const QString &sFileName);
    void walkTree(const QString &sFileTag);
    void walkInterpretCombo(const QString &sFileTag);
    void walkMenus(const QString &sContext);
    void triggerWithDialogWalk(QAction *pAction, const QString &sName);
    void walkActiveDialog(const QString &sName);
    void exerciseControls(QWidget *pRoot, const QString &sContext, qint32 nBudget, bool bDialog);
    void inventoryContextMenus(QWidget *pRoot, const QString &sContext);
    void layoutChecks(QWidget *pRoot, const QString &sContext);
    void selectionLeakCheck(QListWidget *pList, const QString &sContext);
    void panelChecks(QWidget *pPanel, const QString &sContext);
    void dropFile(const QString &sFileName);
    bool openFile(const QString &sFileName, const QString &sHow);
    void closeFile();
    void fileLockCheck(const QString &sFileName, const QString &sWhen);

    // helpers
    QString screenshot(QWidget *pWidget, const QString &sName);
    static QJsonObject menuToJson(QMenu *pMenu, qint32 nDepth);
    static QString widgetPath(QWidget *pWidget);
    static QString safeName(const QString &sName);
    static QString actionText(const QString &sText);
    XFWidgetAdvanced *viewer() const;
    qint32 mainPageIndex() const;

    GuiMainWindow *m_pMainWindow;
    OPTIONS m_options;
    QTimer m_sentinel;
    QElapsedTimer m_clock;
    QString m_sStep;
    QString m_sFileTag;
    qint64 m_nSteps;
    qint64 m_nFails;
    qint64 m_nChecks;
    qint64 m_nQtWarnings;
    QSet<QString> m_setScreens;
    QSet<QString> m_setReported;
    QPointer<QWidget> m_pWalkedDialog;
    QPointer<QWidget> m_pSentinelModal;
    QElapsedTimer m_sentinelModalTimer;
    QPointer<QWidget> m_pSentinelPopup;
    QElapsedTimer m_sentinelPopupTimer;
    QSet<QMenu *> m_setMenusSeen;
    QString m_sContextMenuContext;
    bool m_bInHandler;
    bool m_bOffscreen;

    static XBVSmoke *s_pInstance;
};

#endif  // XBVSMOKE_H
