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
#include "xbvsmoke.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QThread>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <cstdio>
#include <cstring>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

#ifdef Q_OS_WIN
#include <share.h>
#include <windows.h>
#include <dbghelp.h>
#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif
#endif

#include "../guimainwindow.h"
#include "xdialogprocess.h"
#include "xfwidgetadvanced.h"
#include "xftreeview.h"
#include "xoptionswidget.h"

XBVSmoke *XBVSmoke::s_pInstance = nullptr;

namespace {
// Last step text, kept as plain C storage so the crash handler can print it
// without touching Qt. The report is written through one C stream shared by
// record() and the crash handlers: a second open of the file from inside the
// handler is not reliable while a QFile holds it.
char g_szLastStep[1024] = {0};
FILE *g_pReportFile = nullptr;

void reportWrite(const char *pLine)
{
    if (g_pReportFile) {
        fputs(pLine, g_pReportFile);
        fflush(g_pReportFile);
    }
    fputs(pLine, stdout);
    fflush(stdout);
}

#ifdef Q_OS_WIN
DWORD g_nMainThreadId = 0;
volatile LONG g_nCrashReported = 0;

// Copies pSource into pDest as a JSON-safe string body (no surrounding quotes).
void jsonEscape(char *pDest, size_t nDestSize, const char *pSource)
{
    size_t nOut = 0;
    for (const char *p = pSource; *p && (nOut + 2 < nDestSize); p++) {
        unsigned char c = (unsigned char)*p;
        if ((c == '"') || (c == '\\')) {
            pDest[nOut++] = '\\';
            pDest[nOut++] = (char)c;
        } else if (c < 0x20) {
            pDest[nOut++] = ' ';
        } else {
            pDest[nOut++] = (char)c;
        }
    }
    pDest[nOut] = 0;
}

// Writes one record ("crash" or "hang") with a symbolised stack of the given
// thread, walked from the given context. Runs inside the exception dispatch
// or on the watchdog thread, so it uses only the C runtime + dbghelp.
void smokeWriteStackRecord(const char *pKind, DWORD nCode, void *pAddress, const char *pVia, HANDLE hThread, const CONTEXT *pContextIn, bool bMainThread)
{
    static char szLine[16384];
    char szStep[2048];
    jsonEscape(szStep, sizeof(szStep), g_szLastStep);

    int nLen = _snprintf_s(szLine, sizeof(szLine), _TRUNCATE,
                           "{\"kind\":\"%s\",\"code\":\"0x%08lX\",\"address\":\"%p\",\"via\":\"%s\",\"thread\":\"%s\",\"step\":\"%s\",\"stack\":[", pKind,
                           (unsigned long)nCode, pAddress, pVia, bMainThread ? "gui" : "worker", szStep);

    HANDLE hProcess = GetCurrentProcess();
    void *pFrames[62];
    USHORT nFrames = 0;

    // Walk from the context so the faulting/blocked frame comes first
    // (a backtrace taken inside the dispatcher would start at this handler).
    if (pContextIn) {
        CONTEXT context = *pContextIn;
        STACKFRAME64 frame;
        memset(&frame, 0, sizeof(frame));
        DWORD nMachine;
#if defined(_M_X64)
        nMachine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = context.Rip;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrStack.Offset = context.Rsp;
#else
        nMachine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = context.Eip;
        frame.AddrFrame.Offset = context.Ebp;
        frame.AddrStack.Offset = context.Esp;
#endif
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;

        while ((nFrames < 62) && StackWalk64(nMachine, hProcess, hThread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            if (frame.AddrPC.Offset == 0) {
                break;
            }
            pFrames[nFrames++] = (void *)(uintptr_t)frame.AddrPC.Offset;
        }
    }

    if ((nFrames == 0) && (hThread == GetCurrentThread())) {
        nFrames = RtlCaptureStackBackTrace(0, 62, pFrames, nullptr);
    }

    char szSymbolBuffer[sizeof(SYMBOL_INFO) + 512];
    bool bFirst = true;

    for (USHORT i = 0; (i < nFrames) && (nLen > 0) && ((size_t)nLen < sizeof(szLine) - 700); i++) {
        DWORD64 nAddress = (DWORD64)(uintptr_t)pFrames[i];
        SYMBOL_INFO *pSymbol = (SYMBOL_INFO *)szSymbolBuffer;
        memset(szSymbolBuffer, 0, sizeof(szSymbolBuffer));
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = 511;
        DWORD64 nDisplacement = 0;
        char szFrame[640];
        if (SymFromAddr(hProcess, nAddress, &nDisplacement, pSymbol)) {
            IMAGEHLP_LINE64 line;
            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(line);
            DWORD nLineDisplacement = 0;
            if (SymGetLineFromAddr64(hProcess, nAddress, &nLineDisplacement, &line)) {
                _snprintf_s(szFrame, sizeof(szFrame), _TRUNCATE, "%s+0x%llx (%s:%lu)", pSymbol->Name, (unsigned long long)nDisplacement, line.FileName,
                            (unsigned long)line.LineNumber);
            } else {
                _snprintf_s(szFrame, sizeof(szFrame), _TRUNCATE, "%s+0x%llx", pSymbol->Name, (unsigned long long)nDisplacement);
            }
        } else {
            HMODULE hModule = nullptr;
            char szModule[MAX_PATH] = {0};
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pFrames[i], &hModule) && hModule) {
                GetModuleFileNameA(hModule, szModule, sizeof(szModule));
            }
            _snprintf_s(szFrame, sizeof(szFrame), _TRUNCATE, "%p %s", pFrames[i], szModule);
        }
        char szEscaped[700];
        jsonEscape(szEscaped, sizeof(szEscaped), szFrame);
        int nAdded = _snprintf_s(szLine + nLen, sizeof(szLine) - nLen, _TRUNCATE, "%s\"%s\"", bFirst ? "" : ",", szEscaped);
        if (nAdded < 0) {
            break;
        }
        nLen += nAdded;
        bFirst = false;
    }

    _snprintf_s(szLine + nLen, sizeof(szLine) - nLen, _TRUNCATE, "]}\n");

    reportWrite(szLine);
}

void smokeWriteCrash(EXCEPTION_POINTERS *pInfo, const char *pVia)
{
    DWORD nCode = (pInfo && pInfo->ExceptionRecord) ? pInfo->ExceptionRecord->ExceptionCode : 0;
    void *pAddress = (pInfo && pInfo->ExceptionRecord) ? pInfo->ExceptionRecord->ExceptionAddress : nullptr;
    smokeWriteStackRecord("crash", nCode, pAddress, pVia, GetCurrentThread(), pInfo ? pInfo->ContextRecord : nullptr, GetCurrentThreadId() == g_nMainThreadId);
}

// Watchdog: if the GUI thread makes no step progress for g_nWatchdogMs, it is
// blocked in something the sentinel cannot see (a synchronous network wait, a
// nested loop without a widget, a deadlock). Freeze it, record its stack as a
// "hang" and terminate, so the sweep continues with the next sample.
volatile LONG g_nStepSerial = 0;
HANDLE g_hMainThread = nullptr;
DWORD g_nWatchdogMs = 270000;

DWORD WINAPI smokeWatchdogThread(LPVOID)
{
    LONG nLast = g_nStepSerial;
    ULONGLONG nSince = GetTickCount64();

    for (;;) {
        Sleep(2000);
        LONG nNow = g_nStepSerial;
        if (nNow != nLast) {
            nLast = nNow;
            nSince = GetTickCount64();
            continue;
        }
        if ((GetTickCount64() - nSince) < g_nWatchdogMs) {
            continue;
        }
        if (InterlockedExchange(&g_nCrashReported, 1) != 0) {
            return 0;
        }
        if (g_hMainThread && (SuspendThread(g_hMainThread) != (DWORD)-1)) {
            CONTEXT context;
            memset(&context, 0, sizeof(context));
            context.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(g_hMainThread, &context)) {
                smokeWriteStackRecord("hang", 0, nullptr, "watchdog", g_hMainThread, &context, true);
            } else {
                smokeWriteStackRecord("hang", 0, nullptr, "watchdog-nocontext", g_hMainThread, nullptr, true);
            }
        } else {
            smokeWriteStackRecord("hang", 0, nullptr, "watchdog-nosuspend", nullptr, nullptr, true);
        }
        TerminateProcess(GetCurrentProcess(), 0xDEAD0001);
        return 0;
    }
}

bool isFatalExceptionCode(DWORD nCode)
{
    return (nCode == EXCEPTION_ACCESS_VIOLATION) || (nCode == EXCEPTION_ILLEGAL_INSTRUCTION) || (nCode == EXCEPTION_STACK_OVERFLOW) ||
           (nCode == EXCEPTION_INT_DIVIDE_BY_ZERO) || (nCode == EXCEPTION_PRIV_INSTRUCTION) || (nCode == EXCEPTION_IN_PAGE_ERROR) ||
           (nCode == EXCEPTION_ARRAY_BOUNDS_EXCEEDED) || (nCode == 0xC0000409) /* STATUS_STACK_BUFFER_OVERRUN */ || (nCode == 0xC0000374) /* heap corruption */;
}

// First-chance handler: sees the fault on the faulting thread before any
// frame-based handler (Qt, libyara, the CRT) can swallow or re-throw it.
LONG WINAPI smokeVectoredHandler(EXCEPTION_POINTERS *pInfo)
{
    if (pInfo && pInfo->ExceptionRecord && isFatalExceptionCode(pInfo->ExceptionRecord->ExceptionCode)) {
        if (InterlockedExchange(&g_nCrashReported, 1) == 0) {
            smokeWriteCrash(pInfo, "vectored");
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI smokeExceptionFilter(EXCEPTION_POINTERS *pInfo)
{
    if (InterlockedExchange(&g_nCrashReported, 1) == 0) {
        smokeWriteCrash(pInfo, "unhandled");
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

bool isViewportLike(QWidget *pWidget)
{
    if (!pWidget) {
        return false;
    }

    return pWidget->inherits("QAbstractScrollArea") || (pWidget->objectName() == QLatin1String("qt_scrollarea_viewport")) || pWidget->inherits("QHeaderView") ||
           pWidget->inherits("QStackedWidget") || pWidget->inherits("QMainWindow") || pWidget->inherits("QTabWidget") || pWidget->inherits("QSplitter") ||
           pWidget->inherits("QMdiArea") || pWidget->inherits("QMenuBar") || pWidget->inherits("QToolBar") || pWidget->inherits("QDockWidget");
}

bool isIgnorableWidget(QWidget *pWidget)
{
    return pWidget->inherits("QSizeGrip") || pWidget->inherits("QRubberBand") || pWidget->inherits("QMenu") || pWidget->inherits("QScrollBar") ||
           pWidget->inherits("QToolTip") || pWidget->inherits("QFocusFrame") || pWidget->inherits("QSplitterHandle") || pWidget->inherits("QDesktopWidget") ||
           pWidget->objectName().startsWith(QLatin1String("qt_")) || pWidget->inherits("QWidgetResizeHandler") || pWidget->inherits("QAbstractItemView") ||
           pWidget->inherits("QLineEdit") ? false : false;
}
}  // namespace

XBVSmoke::XBVSmoke(GuiMainWindow *pMainWindow, const OPTIONS &options, QObject *pParent)
    : QObject(pParent), m_pMainWindow(pMainWindow), m_options(options), m_nSteps(0), m_nFails(0), m_nChecks(0), m_nQtWarnings(0), m_bInHandler(false),
      m_bOffscreen(false)
{
    s_pInstance = this;
    m_sentinel.setInterval(120);
    connect(&m_sentinel, SIGNAL(timeout()), this, SLOT(onSentinel()));
    m_bOffscreen = (QGuiApplication::platformName() == QLatin1String("offscreen"));
    qApp->installEventFilter(this);
}

XBVSmoke::~XBVSmoke()
{
    if (s_pInstance == this) {
        qInstallMessageHandler(nullptr);
        s_pInstance = nullptr;
    }
}

bool XBVSmoke::optionsFromEnvironment(OPTIONS *pOptions)
{
    QByteArray baReport = qgetenv("XBV_SMOKE");

    if (baReport.isEmpty()) {
        return false;
    }

    pOptions->sReportDir = QString::fromLocal8Bit(baReport);

    QString sFiles = QString::fromLocal8Bit(qgetenv("XBV_SMOKE_FILES"));
    if (!sFiles.isEmpty()) {
        pOptions->listFiles = sFiles.split(';', Qt::SkipEmptyParts);
    }

    QString sList = QString::fromLocal8Bit(qgetenv("XBV_SMOKE_LIST"));
    if (!sList.isEmpty()) {
        QFile file(sList);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!file.atEnd()) {
                QString sLine = QString::fromUtf8(file.readLine()).trimmed();
                if (!sLine.isEmpty() && !sLine.startsWith('#')) {
                    pOptions->listFiles.append(sLine);
                }
            }
        }
    }

    QStringList listFlags = QString::fromLocal8Bit(qgetenv("XBV_SMOKE_FLAGS")).split(',', Qt::SkipEmptyParts);
    pOptions->bScreenshots = !listFlags.contains("noscreens");
    pOptions->bControls = !listFlags.contains("nocontrols");
    pOptions->bContextMenus = !listFlags.contains("nomenus");
    pOptions->bInterpret = !listFlags.contains("nointerpret");
    pOptions->bDialogs = !listFlags.contains("nodialogs");
    pOptions->bDrop = !listFlags.contains("nodrop");

    bool bOk = false;
    qint32 nMaxNodes = qgetenv("XBV_SMOKE_MAXNODES").toInt(&bOk);
    if (bOk && (nMaxNodes > 0)) {
        pOptions->nMaxNodes = nMaxNodes;
    }

    qint32 nTimeout = qgetenv("XBV_SMOKE_TIMEOUT").toInt(&bOk);
    if (bOk && (nTimeout > 0)) {
        pOptions->nStepTimeoutMs = nTimeout;
    }

    pOptions->listSkipNodes = QString::fromLocal8Bit(qgetenv("XBV_SMOKE_SKIP")).split(';', Qt::SkipEmptyParts);

    return true;
}

// ---------------------------------------------------------------- reporting

void XBVSmoke::record(const QString &sKind, QJsonObject object)
{
    object.insert("kind", sKind);
    object.insert("t", (double)m_clock.elapsed());
    if (!m_sFileTag.isEmpty() && !object.contains("file")) {
        object.insert("file", m_sFileTag);
    }

    QByteArray baLine = QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n";

    reportWrite(baLine.constData());
}

void XBVSmoke::fail(const QString &sWhat, const QString &sDetail)
{
    m_nFails++;
    QJsonObject object;
    object.insert("what", sWhat);
    object.insert("detail", sDetail);
    object.insert("step", m_sStep);
    object.insert("ok", false);
    record("check", object);
}

void XBVSmoke::check(bool bOk, const QString &sWhat, const QString &sDetail)
{
    m_nChecks++;
    if (!bOk) {
        fail(sWhat, sDetail);
    }
}

void XBVSmoke::setStep(const QString &sStep)
{
    m_sStep = sStep;
    m_nSteps++;
    QByteArray ba = sStep.toUtf8();
    strncpy_s(g_szLastStep, sizeof(g_szLastStep), ba.constData(), _TRUNCATE);
#ifdef Q_OS_WIN
    InterlockedIncrement(&g_nStepSerial);
#endif
}

void XBVSmoke::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &sMessage)
{
    Q_UNUSED(context)

    XBVSmoke *pThis = s_pInstance;

    if (!pThis || pThis->m_bInHandler) {
        fprintf(stderr, "%s\n", sMessage.toUtf8().constData());
        return;
    }

    pThis->m_bInHandler = true;

    const char *pType = "debug";
    if (type == QtWarningMsg) pType = "warning";
    else if (type == QtCriticalMsg) pType = "critical";
    else if (type == QtFatalMsg) pType = "fatal";
    else if (type == QtInfoMsg) pType = "info";

    // Debug chatter (XOptions::load() etc.) is not worth a line; everything from
    // warning up is a real finding (broken connects, layout misuse, ...) except
    // the offscreen platform plugin's own capability notices.
    bool bPlatformNoise = sMessage.contains(QLatin1String("This plugin does not support")) || sMessage.contains(QLatin1String("Cannot find font directory")) ||
                          sMessage.contains(QLatin1String("QFontDatabase: Cannot find font"));

    if ((type != QtDebugMsg) && (type != QtInfoMsg) && !bPlatformNoise) {
        pThis->m_nQtWarnings++;
        QJsonObject object;
        object.insert("type", pType);
        object.insert("msg", sMessage);
        object.insert("step", pThis->m_sStep);
        pThis->record("qtmsg", object);
    }

    pThis->m_bInHandler = false;

    if (type == QtFatalMsg) {
        // A Qt assertion (qFatal) is a defect in its own right: record the stack
        // of the asserting thread, then leave without the CRT abort() dialog,
        // which would only block this thread while the GUI keeps running.
#ifdef Q_OS_WIN
        CONTEXT context;
        memset(&context, 0, sizeof(context));
        RtlCaptureContext(&context);
        smokeWriteStackRecord("fatal", 0, nullptr, "qFatal", GetCurrentThread(), &context, GetCurrentThreadId() == g_nMainThreadId);
        TerminateProcess(GetCurrentProcess(), 0xDEAD0002);
#else
        abort();
#endif
    }
}

// ------------------------------------------------------------ event pumping

void XBVSmoke::settle(qint32 nMinMs, qint32 nMaxMs)
{
    QElapsedTimer timer;
    timer.start();
    qint32 nQuiet = 0;

    while (true) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCoreApplication::sendPostedEvents();

        QWidget *pModal = QApplication::activeModalWidget();
        bool bBusy = (pModal && (pModal != m_pWalkedDialog.data())) || (QApplication::activePopupWidget() != nullptr);

        if (timer.elapsed() >= nMaxMs) {
            QJsonObject object;
            object.insert("step", m_sStep);
            object.insert("modal", pModal ? QString(pModal->metaObject()->className()) : QString());
            object.insert("popup", QApplication::activePopupWidget() ? QString(QApplication::activePopupWidget()->metaObject()->className()) : QString());
            object.insert("maxMs", nMaxMs);
            record("settle_timeout", object);
            break;
        }

        if (!bBusy && (timer.elapsed() >= nMinMs)) {
            nQuiet++;
            if (nQuiet >= 3) {
                break;
            }
        } else {
            nQuiet = 0;
        }

        QThread::msleep(10);
    }
}

bool XBVSmoke::waitModalGone(qint32 nMaxMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < nMaxMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QWidget *pModal = QApplication::activeModalWidget();
        if ((!pModal || (pModal == m_pWalkedDialog.data())) && !QApplication::activePopupWidget()) {
            return true;
        }
        QThread::msleep(10);
    }

    return false;
}

bool XBVSmoke::eventFilter(QObject *pObject, QEvent *pEvent)
{
    // Qt 5.15 on Windows: QMessageBox::showEvent() calls qt_getWindowsSystemMenu(),
    // which dereferences QGuiApplication::platformNativeInterface() - null on the
    // "offscreen" platform, so every message box would crash the headless run.
    // Swallow the Show event for message boxes there; the box is still visible
    // and modal, and the sentinel answers it. (The widget's own showEvent only
    // sizes the box and greys the system-menu Close item.)
    if (m_bOffscreen && (pEvent->type() == QEvent::Show) && qobject_cast<QMessageBox *>(pObject)) {
        return true;
    }

    return QObject::eventFilter(pObject, pEvent);
}

void XBVSmoke::onSentinel()
{
    // 1. Modal dialogs not owned by the dialog walker. A dialog that was
    //    show()n before exec() (XOptions::_adjustStayOnTop used to do that) is
    //    never registered in Qt's modal list, so also look for any visible
    //    top-level QDialog.
    QWidget *pModal = QApplication::activeModalWidget();

    if (!pModal) {
        QWidgetList listTop = QApplication::topLevelWidgets();
        for (qint32 i = 0; i < listTop.count(); i++) {
            QDialog *pCandidate = qobject_cast<QDialog *>(listTop.at(i));
            if (pCandidate && pCandidate->isVisible() && (pCandidate != m_pWalkedDialog.data())) {
                pModal = pCandidate;
                break;
            }
        }
    }

    if (pModal && (pModal != m_pWalkedDialog.data())) {
        if (m_pSentinelModal.data() != pModal) {
            m_pSentinelModal = pModal;
            m_sentinelModalTimer.start();
            QJsonObject seen;
            seen.insert("class", QString(pModal->metaObject()->className()));
            seen.insert("title", pModal->windowTitle());
            seen.insert("modality", (qint32)pModal->windowModality());
            seen.insert("step", m_sStep);
            record("modal_seen", seen);
        }

        XDialogProcess *pProcess = qobject_cast<XDialogProcess *>(pModal);
        QMessageBox *pMessageBox = qobject_cast<QMessageBox *>(pModal);
        QFileDialog *pFileDialog = qobject_cast<QFileDialog *>(pModal);
        QDialog *pDialog = qobject_cast<QDialog *>(pModal);

        if (pProcess) {
            // Legitimate progress dialog: let it run, but bound it.
            if (m_sentinelModalTimer.elapsed() > m_options.nStepTimeoutMs) {
                fail("step timeout: progress dialog still running", QString("%1 ms at %2").arg(m_sentinelModalTimer.elapsed()).arg(m_sStep));
                pProcess->stop();
                m_sentinelModalTimer.restart();
            }
        } else if (pMessageBox) {
            QJsonObject object;
            object.insert("title", pMessageBox->windowTitle());
            object.insert("text", pMessageBox->text());
            object.insert("step", m_sStep);
            record("messagebox", object);

            QAbstractButton *pButton = nullptr;
            const QMessageBox::StandardButton order[] = {QMessageBox::Cancel, QMessageBox::No, QMessageBox::Close, QMessageBox::Abort, QMessageBox::Ok,
                                                         QMessageBox::Ignore, QMessageBox::Yes};
            for (QMessageBox::StandardButton button : order) {
                pButton = pMessageBox->button(button);
                if (pButton) {
                    break;
                }
            }

            if (pButton) {
                pButton->click();
            } else {
                pMessageBox->reject();
            }
        } else if (pFileDialog) {
            QJsonObject object;
            object.insert("title", pFileDialog->windowTitle());
            object.insert("mode", (qint32)pFileDialog->acceptMode());
            object.insert("step", m_sStep);
            record("filedialog", object);
            pFileDialog->reject();
        } else if (pDialog) {
            if (m_sentinelModalTimer.elapsed() > 1500) {
                QJsonObject object;
                object.insert("class", QString(pDialog->metaObject()->className()));
                object.insert("title", pDialog->windowTitle());
                object.insert("step", m_sStep);
                record("autoclosed_dialog", object);
                screenshot(pDialog, QString("dialog_auto_%1").arg(QString(pDialog->metaObject()->className())));
                layoutChecks(pDialog, QString("autodialog:%1").arg(QString(pDialog->metaObject()->className())));
                pDialog->reject();
                m_sentinelModalTimer.restart();
            }
        } else if (m_sentinelModalTimer.elapsed() > 3000) {
            QJsonObject object;
            object.insert("class", QString(pModal->metaObject()->className()));
            object.insert("step", m_sStep);
            record("autoclosed_modal", object);
            pModal->close();
            m_sentinelModalTimer.restart();
        }
    } else {
        m_pSentinelModal = nullptr;
    }

    // 2. Popups (context menus, combo dropdowns).
    QWidget *pPopup = QApplication::activePopupWidget();

    if (pPopup) {
        if (m_pSentinelPopup.data() != pPopup) {
            m_pSentinelPopup = pPopup;
            m_sentinelPopupTimer.start();

            QMenu *pMenu = qobject_cast<QMenu *>(pPopup);
            if (pMenu && !m_setMenusSeen.contains(pMenu)) {
                m_setMenusSeen.insert(pMenu);
                QJsonObject object = menuToJson(pMenu, 0);
                object.insert("context", m_sContextMenuContext);
                object.insert("step", m_sStep);
                record("contextmenu", object);
                screenshot(pMenu, QString("menu_%1").arg(m_sContextMenuContext));
            }
        }

        if (m_sentinelPopupTimer.elapsed() > 250) {
            pPopup->close();
        }
    } else {
        m_pSentinelPopup = nullptr;
    }
}

// ------------------------------------------------------------------ helpers

QString XBVSmoke::screenshot(QWidget *pWidget, const QString &sName)
{
    if (!m_options.bScreenshots || !pWidget) {
        return QString();
    }

    QString sFile = m_options.sReportDir + QDir::separator() + safeName(sName) + ".png";
    QPixmap pixmap = pWidget->grab();

    if (pixmap.isNull()) {
        return QString();
    }

    pixmap.save(sFile, "PNG");

    return sFile;
}

QJsonObject XBVSmoke::menuToJson(QMenu *pMenu, qint32 nDepth)
{
    QJsonObject result;
    result.insert("title", pMenu->title());
    QJsonArray array;

    QList<QAction *> listActions = pMenu->actions();

    for (qint32 i = 0; i < listActions.count(); i++) {
        QAction *pAction = listActions.at(i);
        if (pAction->isSeparator()) {
            continue;
        }
        QJsonObject item;
        item.insert("text", actionText(pAction->text()));
        item.insert("enabled", pAction->isEnabled());
        if (!pAction->shortcut().isEmpty()) {
            item.insert("shortcut", pAction->shortcut().toString());
        }
        if (pAction->menu() && (nDepth < 4)) {
            item.insert("submenu", menuToJson(pAction->menu(), nDepth + 1));
        }
        array.append(item);
    }

    result.insert("actions", array);

    return result;
}

QString XBVSmoke::widgetPath(QWidget *pWidget)
{
    QStringList listParts;
    QWidget *pCurrent = pWidget;
    qint32 nGuard = 0;

    while (pCurrent && (nGuard++ < 12)) {
        QString sPart = QString(pCurrent->metaObject()->className());
        if (!pCurrent->objectName().isEmpty()) {
            sPart += QString("#") + pCurrent->objectName();
        }
        listParts.prepend(sPart);
        if (pCurrent->isWindow()) {
            break;
        }
        pCurrent = pCurrent->parentWidget();
    }

    return listParts.join("/");
}

QString XBVSmoke::safeName(const QString &sName)
{
    QString sResult = sName;

    for (qint32 i = 0; i < sResult.size(); i++) {
        QChar c = sResult.at(i);
        if (!(c.isLetterOrNumber() || (c == '_') || (c == '-') || (c == '.'))) {
            sResult[i] = '_';
        }
    }

    if (sResult.size() > 120) {
        sResult = sResult.left(120);
    }

    return sResult;
}

QString XBVSmoke::actionText(const QString &sText)
{
    QString sResult = sText;
    sResult.remove('&');
    return sResult;
}

XFWidgetAdvanced *XBVSmoke::viewer() const
{
    return m_pMainWindow->findChild<XFWidgetAdvanced *>("widgetViewer");
}

qint32 XBVSmoke::mainPageIndex() const
{
    QList<QStackedWidget *> list = m_pMainWindow->findChildren<QStackedWidget *>("stackedWidget");

    for (qint32 i = 0; i < list.count(); i++) {
        if (list.at(i)->parentWidget() && (list.at(i)->parentWidget()->objectName() == QLatin1String("centralwidget"))) {
            return list.at(i)->currentIndex();
        }
    }

    return -1;
}

// ------------------------------------------------------------------- checks

void XBVSmoke::layoutChecks(QWidget *pRoot, const QString &sContext)
{
    if (!pRoot) {
        return;
    }

    QList<QWidget *> listWidgets = pRoot->findChildren<QWidget *>();
    QHash<QWidget *, QList<QWidget *>> mapChildren;
    qint32 nReported = 0;
    const qint32 nMaxReports = 40;

    for (qint32 i = 0; (i < listWidgets.count()) && (nReported < nMaxReports); i++) {
        QWidget *pWidget = listWidgets.at(i);

        if (!pWidget->isVisible() || pWidget->isWindow() || isIgnorableWidget(pWidget)) {
            continue;
        }
        if (pWidget->inherits("QScrollBar") || pWidget->inherits("QSizeGrip") || pWidget->inherits("QRubberBand") || pWidget->inherits("QMenu") ||
            pWidget->inherits("QSplitterHandle") || pWidget->objectName().startsWith(QLatin1String("qt_"))) {
            continue;
        }
        if ((pWidget->width() <= 0) || (pWidget->height() <= 0)) {
            continue;
        }

        QWidget *pParent = pWidget->parentWidget();
        QString sPath = widgetPath(pWidget);

        // 1. Leaf controls narrower/shorter than their own minimum size hint:
        //    the text is cut off on screen.
        QLabel *pLabel = qobject_cast<QLabel *>(pWidget);
        QAbstractButton *pButton = qobject_cast<QAbstractButton *>(pWidget);
        QComboBox *pCombo = qobject_cast<QComboBox *>(pWidget);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        bool bLabelHasPixmap = pLabel && !pLabel->pixmap(Qt::ReturnByValue).isNull();
#else
        bool bLabelHasPixmap = pLabel && pLabel->pixmap();
#endif
        // Icon buttons with at most a one-character label (the 24px "<" ">" glyph
        // buttons) draw their icon centred whatever the style padding says.
        bool bIconGlyphButton = pButton && !pButton->icon().isNull() && (pButton->text().size() <= 1);
        bool bLeaf = (pLabel && !pLabel->wordWrap() && !pLabel->text().isEmpty() && !bLabelHasPixmap) || (pButton && !pButton->text().isEmpty() && !bIconGlyphButton) || pCombo;

        if (bLeaf) {
            QSize sizeHint = pWidget->minimumSizeHint();
            qint32 nShortW = sizeHint.isValid() ? (sizeHint.width() - pWidget->width()) : 0;
            qint32 nShortH = sizeHint.isValid() ? (sizeHint.height() - pWidget->height()) : 0;
            bool bTooNarrow = nShortW > 1;
            bool bTooShort = nShortH > 1;

            // A label that does its own eliding or is inside a scroll area is fine.
            // A shortfall of a few pixels (typically QSS padding vs. layout rounding)
            // is recorded but not counted as a failure.
            if ((bTooNarrow || bTooShort) && !(pParent && isViewportLike(pParent))) {
                QString sKey = sContext + "|clip|" + sPath;
                if (!m_setReported.contains(sKey)) {
                    m_setReported.insert(sKey);
                    bool bMinor = (qMax(nShortW, nShortH) <= 3);
                    QJsonObject object;
                    object.insert("what", bMinor ? "control slightly smaller than minimumSizeHint" : "clipped control (smaller than minimumSizeHint)");
                    object.insert("context", sContext);
                    object.insert("widget", sPath);
                    object.insert("text", pLabel ? pLabel->text().left(80) : (pButton ? actionText(pButton->text()) : (pCombo ? pCombo->currentText() : QString())));
                    object.insert("size", QString("%1x%2").arg(pWidget->width()).arg(pWidget->height()));
                    object.insert("hint", QString("%1x%2").arg(sizeHint.width()).arg(sizeHint.height()));
                    object.insert("shortfall", QString("%1x%2").arg(qMax(0, nShortW)).arg(qMax(0, nShortH)));
                    object.insert("ok", bMinor);
                    if (!bMinor) {
                        m_nFails++;
                    }
                    record("check", object);
                    nReported++;
                }
            }
        }

        // 2. Children partially outside their parent (except scroll viewports,
        //    whose children legitimately scroll out of view).
        if (pParent && !isViewportLike(pParent) && !pParent->isWindow()) {
            QRect rectParent(QPoint(0, 0), pParent->size());
            if (!rectParent.contains(pWidget->geometry()) && rectParent.isValid()) {
                QString sKey = sContext + "|oob|" + sPath;
                if (!m_setReported.contains(sKey)) {
                    m_setReported.insert(sKey);
                    QJsonObject object;
                    object.insert("what", "widget extends outside its parent");
                    object.insert("context", sContext);
                    object.insert("widget", sPath);
                    object.insert("geometry", QString("%1,%2 %3x%4").arg(pWidget->x()).arg(pWidget->y()).arg(pWidget->width()).arg(pWidget->height()));
                    object.insert("parent", QString("%1x%2").arg(pParent->width()).arg(pParent->height()));
                    object.insert("ok", false);
                    m_nFails++;
                    record("check", object);
                    nReported++;
                }
            }
        }

        if (pParent) {
            mapChildren[pParent].append(pWidget);
        }
    }

    // 3. Overlapping siblings.
    QHash<QWidget *, QList<QWidget *>>::const_iterator it = mapChildren.constBegin();

    for (; (it != mapChildren.constEnd()) && (nReported < nMaxReports); ++it) {
        QWidget *pParent = it.key();
        if (isViewportLike(pParent) || pParent->inherits("QDialogButtonBox")) {
            continue;
        }
        const QList<QWidget *> &list = it.value();
        for (qint32 i = 0; i < list.count(); i++) {
            for (qint32 j = i + 1; j < list.count(); j++) {
                QWidget *pA = list.at(i);
                QWidget *pB = list.at(j);
                QRect rect = pA->geometry().intersected(pB->geometry());
                if ((rect.width() > 2) && (rect.height() > 2)) {
                    QString sKey = sContext + "|overlap|" + widgetPath(pA) + "|" + widgetPath(pB);
                    if (!m_setReported.contains(sKey)) {
                        m_setReported.insert(sKey);
                        QJsonObject object;
                        object.insert("what", "sibling widgets overlap");
                        object.insert("context", sContext);
                        object.insert("widgetA", widgetPath(pA));
                        object.insert("widgetB", widgetPath(pB));
                        object.insert("overlap", QString("%1x%2").arg(rect.width()).arg(rect.height()));
                        object.insert("ok", false);
                        m_nFails++;
                        record("check", object);
                        nReported++;
                    }
                }
            }
        }
    }
}

void XBVSmoke::selectionLeakCheck(QListWidget *pList, const QString &sContext)
{
    // The selected row's background must stay inside the row's rect. A QSS
    // rule that changes the font of ::item:selected (e.g. font-weight) makes
    // the selected row paint taller than its allotted rect and over the
    // neighbouring rows.
    if (!pList || (pList->currentRow() < 0)) {
        return;
    }

    QListWidgetItem *pItem = pList->currentItem();
    if (!pItem) {
        return;
    }

    QRect rect = pList->visualItemRect(pItem);
    QImage image = pList->viewport()->grab().toImage();

    if (image.isNull() || !rect.isValid() || (rect.width() < 8) || (rect.height() < 4)) {
        return;
    }

    // Sample the background just inside the left edge, mid-height, where the
    // padding keeps text away.
    QPoint pointSample(rect.left() + 3, rect.center().y());
    if (!image.rect().contains(pointSample)) {
        return;
    }
    QRgb rgbSelected = image.pixel(pointSample);

    // Only count it as "selection colour" if it differs from the widget's
    // base background sampled far away from any row (bottom-right corner).
    QRgb rgbBase = image.pixel(qMax(0, image.width() - 2), qMax(0, image.height() - 2));
    if (rgbSelected == rgbBase) {
        return;
    }

    qint32 nLeak = 0;
    for (qint32 y = 0; y < image.height(); y++) {
        for (qint32 x = 0; x < image.width(); x++) {
            if (!rect.contains(x, y) && (image.pixel(x, y) == rgbSelected)) {
                nLeak++;
            }
        }
    }

    QJsonObject object;
    object.insert("what", "list selection paints outside its row rect");
    object.insert("context", sContext);
    object.insert("row", pList->currentRow());
    object.insert("rowText", pItem->text());
    object.insert("leakPixels", nLeak);
    object.insert("rowRect", QString("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()));
    object.insert("ok", nLeak < 40);
    m_nChecks++;
    if (nLeak >= 40) {
        m_nFails++;
    }
    record("check", object);
}

void XBVSmoke::panelChecks(QWidget *pPanel, const QString &sContext)
{
    if (!pPanel) {
        return;
    }

    // Every table/tree in the panel: report its dimensions; an empty model in
    // a structure panel is suspicious (blank page for the user).
    QList<QAbstractItemView *> listViews = pPanel->findChildren<QAbstractItemView *>();
    QJsonArray arrayViews;

    for (qint32 i = 0; i < listViews.count(); i++) {
        QAbstractItemView *pView = listViews.at(i);
        if (!pView->isVisible()) {
            continue;
        }
        QJsonObject view;
        view.insert("widget", widgetPath(pView));
        qint32 nRows = pView->model() ? pView->model()->rowCount() : -1;
        qint32 nColumns = pView->model() ? pView->model()->columnCount() : -1;
        view.insert("rows", nRows);
        view.insert("columns", nColumns);
        arrayViews.append(view);
    }

    QJsonObject object;
    object.insert("context", sContext);
    object.insert("views", arrayViews);
    object.insert("panel", QString(pPanel->metaObject()->className()));
    object.insert("size", QString("%1x%2").arg(pPanel->width()).arg(pPanel->height()));
    record("panel", object);

    layoutChecks(pPanel, sContext);
}

// ------------------------------------------------------------------ actions

void XBVSmoke::exerciseControls(QWidget *pRoot, const QString &sContext, qint32 nBudget, bool bDialog)
{
    if (!pRoot || !m_options.bControls) {
        return;
    }

    qint32 nUsed = 0;

    auto recordControl = [&](const QString &sAction, QWidget *pWidget, const QString &sExtra, qint64 nMs) {
        QJsonObject object;
        object.insert("action", sAction);
        object.insert("context", sContext);
        object.insert("widget", widgetPath(pWidget));
        object.insert("extra", sExtra);
        object.insert("ms", (double)nMs);
        record("control", object);
    };

    // Every interaction below can rebuild part of the panel, so all lists are
    // held through QPointers and each entry is re-validated before use.
    QPointer<QWidget> pRootAlive(pRoot);

    // Check boxes
    QList<QPointer<QCheckBox>> listCheck;
    for (QCheckBox *p : pRoot->findChildren<QCheckBox *>()) listCheck.append(QPointer<QCheckBox>(p));
    for (qint32 i = 0; (i < listCheck.count()) && (nUsed < nBudget) && pRootAlive; i++) {
        QCheckBox *pCheck = listCheck.at(i).data();
        if (!pCheck || !pCheck->isVisible() || !pCheck->isEnabled()) {
            continue;
        }
        setStep(sContext + " | checkbox " + widgetPath(pCheck));
        QElapsedTimer timer;
        timer.start();
        QPointer<QCheckBox> pGuard(pCheck);
        QString sText = actionText(pCheck->text());
        pCheck->toggle();
        settle(30, m_options.nStepTimeoutMs);
        if (pGuard) {
            pGuard->toggle();
            settle(30, m_options.nStepTimeoutMs);
        }
        if (pGuard) recordControl("toggle", pGuard, sText, timer.elapsed());
        nUsed++;
    }

    // Radio buttons: click every one, then restore.
    QList<QPointer<QRadioButton>> listRadio;
    for (QRadioButton *p : pRoot->findChildren<QRadioButton *>()) listRadio.append(QPointer<QRadioButton>(p));
    QPointer<QRadioButton> pRadioChecked;
    for (qint32 i = 0; (i < listRadio.count()) && (nUsed < nBudget) && pRootAlive; i++) {
        QRadioButton *pRadio = listRadio.at(i).data();
        if (!pRadio || !pRadio->isVisible() || !pRadio->isEnabled()) {
            continue;
        }
        if (pRadio->isChecked() && !pRadioChecked) {
            pRadioChecked = pRadio;
        }
        setStep(sContext + " | radio " + widgetPath(pRadio));
        QElapsedTimer timer;
        timer.start();
        QString sText = actionText(pRadio->text());
        QPointer<QRadioButton> pGuard(pRadio);
        pRadio->click();
        settle(30, m_options.nStepTimeoutMs);
        if (pGuard) recordControl("click", pGuard, sText, timer.elapsed());
        nUsed++;
    }
    if (pRadioChecked) {
        pRadioChecked->click();
        settle(30, m_options.nStepTimeoutMs);
    }

    // Combo boxes: sample up to 6 entries spread across the list, then restore.
    QList<QPointer<QComboBox>> listCombo;
    for (QComboBox *p : pRoot->findChildren<QComboBox *>()) listCombo.append(QPointer<QComboBox>(p));
    for (qint32 i = 0; (i < listCombo.count()) && (nUsed < nBudget) && pRootAlive; i++) {
        QComboBox *pCombo = listCombo.at(i).data();
        if (!pCombo || !pCombo->isVisible() || !pCombo->isEnabled() || (pCombo->count() < 2)) {
            continue;
        }
        qint32 nOriginal = pCombo->currentIndex();
        qint32 nCount = pCombo->count();
        qint32 nSamples = qMin(nCount, 6);
        setStep(sContext + " | combo " + widgetPath(pCombo));
        QElapsedTimer timer;
        timer.start();
        QPointer<QComboBox> pGuard(pCombo);
        for (qint32 j = 0; (j < nSamples) && pGuard; j++) {
            qint32 nIndex = (nSamples == nCount) ? j : (qint32)(((qint64)j * (nCount - 1)) / (nSamples - 1));
            if (nIndex == nOriginal) {
                continue;
            }
            pGuard->setCurrentIndex(nIndex);
            settle(30, m_options.nStepTimeoutMs);
        }
        if (pGuard) {
            pGuard->setCurrentIndex(nOriginal);
            settle(30, m_options.nStepTimeoutMs);
            recordControl("cycle", pGuard, QString("%1 items").arg(nCount), timer.elapsed());
        }
        nUsed++;
    }

    // Spin boxes
    QList<QPointer<QSpinBox>> listSpin;
    for (QSpinBox *p : pRoot->findChildren<QSpinBox *>()) listSpin.append(QPointer<QSpinBox>(p));
    for (qint32 i = 0; (i < listSpin.count()) && (nUsed < nBudget) && pRootAlive; i++) {
        QSpinBox *pSpin = listSpin.at(i).data();
        if (!pSpin || !pSpin->isVisible() || !pSpin->isEnabled() || pSpin->isReadOnly()) {
            continue;
        }
        setStep(sContext + " | spin " + widgetPath(pSpin));
        QElapsedTimer timer;
        timer.start();
        QPointer<QSpinBox> pGuard(pSpin);
        pSpin->stepUp();
        settle(20, m_options.nStepTimeoutMs);
        if (pGuard) {
            pGuard->stepDown();
            settle(20, m_options.nStepTimeoutMs);
        }
        if (pGuard) recordControl("step", pGuard, QString::number(pGuard->value()), timer.elapsed());
        nUsed++;
    }

    // Filter/search line edits: type and clear.
    QList<QPointer<QLineEdit>> listEdit;
    for (QLineEdit *p : pRoot->findChildren<QLineEdit *>()) listEdit.append(QPointer<QLineEdit>(p));
    for (qint32 i = 0; (i < listEdit.count()) && (nUsed < nBudget) && pRootAlive; i++) {
        QLineEdit *pEdit = listEdit.at(i).data();
        if (!pEdit || !pEdit->isVisible() || !pEdit->isEnabled() || pEdit->isReadOnly()) {
            continue;
        }
        QString sName = pEdit->objectName().toLower();
        QWidget *pParent = pEdit->parentWidget();
        bool bFilter = sName.contains("filter") || sName.contains("search") || sName.contains("find") || (pParent && pParent->inherits("QHeaderView")) ||
                       (pParent && pParent->parentWidget() && pParent->parentWidget()->inherits("QHeaderView"));
        if (!bFilter) {
            continue;
        }
        setStep(sContext + " | filter " + widgetPath(pEdit));
        QElapsedTimer timer;
        timer.start();
        QPointer<QLineEdit> pGuard(pEdit);
        QString sOld = pEdit->text();
        pEdit->setText("e");
        settle(60, m_options.nStepTimeoutMs);
        if (pGuard) {
            pGuard->setText(sOld);
            settle(60, m_options.nStepTimeoutMs);
        }
        if (pGuard) recordControl("filter", pGuard, QString(), timer.elapsed());
        nUsed++;
    }

    // Tab widgets
    QList<QPointer<QTabWidget>> listTabs;
    for (QTabWidget *p : pRoot->findChildren<QTabWidget *>()) listTabs.append(QPointer<QTabWidget>(p));
    for (qint32 i = 0; (i < listTabs.count()) && (nUsed < nBudget) && pRootAlive; i++) {
        QTabWidget *pTabs = listTabs.at(i).data();
        if (!pTabs || !pTabs->isVisible() || (pTabs->count() < 2)) {
            continue;
        }
        qint32 nOriginal = pTabs->currentIndex();
        setStep(sContext + " | tabs " + widgetPath(pTabs));
        QElapsedTimer timer;
        timer.start();
        QPointer<QTabWidget> pGuard(pTabs);
        for (qint32 j = 0; pGuard && (j < pGuard->count()); j++) {
            pGuard->setCurrentIndex(j);
            settle(30, m_options.nStepTimeoutMs);
            if (pGuard && pRootAlive) {
                layoutChecks(pGuard->currentWidget(), sContext + " tab:" + pGuard->tabText(j));
                screenshot(pRoot, sContext + "_tab_" + pGuard->tabText(j));
            }
        }
        if (pGuard) {
            pGuard->setCurrentIndex(nOriginal);
            settle(30, m_options.nStepTimeoutMs);
            recordControl("tabs", pGuard, QString("%1 tabs").arg(pGuard->count()), timer.elapsed());
        }
        nUsed++;
    }

    // Views: select first row, sort first column both ways.
    QList<QPointer<QAbstractItemView>> listViews;
    for (QAbstractItemView *p : pRoot->findChildren<QAbstractItemView *>()) listViews.append(QPointer<QAbstractItemView>(p));
    for (qint32 i = 0; (i < listViews.count()) && (nUsed < nBudget) && pRootAlive; i++) {
        QAbstractItemView *pView = listViews.at(i).data();
        if (!pView || !pView->isVisible() || !pView->model() || (pView->model()->rowCount() < 1)) {
            continue;
        }
        setStep(sContext + " | view " + widgetPath(pView));
        QElapsedTimer timer;
        timer.start();
        QPointer<QAbstractItemView> pGuard(pView);
        QModelIndex index = pView->model()->index(0, 0);
        pView->setCurrentIndex(index);
        settle(30, m_options.nStepTimeoutMs);
        if (!pGuard) {
            nUsed++;
            continue;
        }
        QTableView *pTable = qobject_cast<QTableView *>(pView);
        if (pTable && pTable->isSortingEnabled()) {
            pTable->sortByColumn(0, Qt::AscendingOrder);
            settle(30, m_options.nStepTimeoutMs);
            if (pGuard) {
                pTable->sortByColumn(0, Qt::DescendingOrder);
                settle(30, m_options.nStepTimeoutMs);
            }
        }
        if (!pGuard || !pView->model()) {
            nUsed++;
            continue;
        }
        // Double-click the first cell through the real event path.
        index = pView->model()->index(0, 0);
        QRect rectCell = pView->visualRect(index);
        if (rectCell.isValid()) {
            QPoint point = rectCell.center();
            QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent dbl(QEvent::MouseButtonDblClick, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(pView->viewport(), &press);
            QApplication::sendEvent(pView->viewport(), &release);
            QApplication::sendEvent(pView->viewport(), &dbl);
            QApplication::sendEvent(pView->viewport(), &release);
            settle(60, m_options.nStepTimeoutMs);
            waitModalGone(m_options.nStepTimeoutMs);
        }
        if (pGuard && pView->model()) {
            recordControl("view", pView, QString("%1 rows").arg(pView->model()->rowCount()), timer.elapsed());
        }
        nUsed++;
    }

    // Push/tool buttons. Buttons that open dialogs are fine: the sentinel
    // rejects file dialogs and answers message boxes negatively. The dialog's
    // own OK/Cancel/Default are excluded when walking a dialog.
    // A click can rebuild (delete) part of the panel - e.g. a Scan button that
    // reloads the view - so the list is held through QPointers and re-checked.
    QList<QPointer<QAbstractButton>> listButtons;
    {
        QList<QAbstractButton *> listRaw = pRoot->findChildren<QAbstractButton *>();
        for (qint32 i = 0; i < listRaw.count(); i++) {
            listButtons.append(QPointer<QAbstractButton>(listRaw.at(i)));
        }
    }
    QPointer<QWidget> pRootGuard(pRoot);
    for (qint32 i = 0; (i < listButtons.count()) && (nUsed < nBudget); i++) {
        if (!pRootGuard) {
            QJsonObject object;
            object.insert("context", sContext);
            object.insert("what", "panel was deleted during control exercise (a click reloaded the view)");
            record("panel_replaced", object);
            return;
        }
        QAbstractButton *pButton = listButtons.at(i).data();
        if (!pButton) {
            continue;
        }
        if (qobject_cast<QCheckBox *>(pButton) || qobject_cast<QRadioButton *>(pButton)) {
            continue;
        }
        if (!pButton->isVisible() || !pButton->isEnabled()) {
            continue;
        }
        QToolButton *pToolButton = qobject_cast<QToolButton *>(pButton);
        if (pToolButton && pToolButton->menu()) {
            QJsonObject object = menuToJson(pToolButton->menu(), 0);
            object.insert("context", sContext);
            object.insert("widget", widgetPath(pToolButton));
            record("buttonmenu", object);
            continue;
        }
        QString sName = pButton->objectName();
        QString sText = actionText(pButton->text());
        if (bDialog) {
            if (pButton->parentWidget() && pButton->parentWidget()->inherits("QDialogButtonBox")) {
                continue;
            }
            if (sName.contains("OK", Qt::CaseInsensitive) || sName.contains("Cancel", Qt::CaseInsensitive) || sName.contains("Default", Qt::CaseInsensitive) ||
                sName.contains("Close", Qt::CaseInsensitive) || sName.contains("Apply", Qt::CaseInsensitive) || sName.contains("Save", Qt::CaseInsensitive) ||
                (sText == "OK") || (sText == "Cancel") || (sText == "Close") || (sText == "Default") || (sText == "Apply")) {
                continue;
            }
        }
        if (sName.contains("Exit", Qt::CaseInsensitive) || (sText == "Exit") || (sText == "Quit")) {
            continue;
        }
        QString sPath = widgetPath(pButton);
        setStep(sContext + " | click " + sPath + " '" + sText + "'");
        QElapsedTimer timer;
        timer.start();
        m_sContextMenuContext = sContext + "_btn_" + sName;
        QPointer<QAbstractButton> pGuard(pButton);
        pButton->click();
        settle(60, m_options.nStepTimeoutMs);
        waitModalGone(m_options.nStepTimeoutMs);
        if (pGuard && pGuard->isCheckable() && pGuard->isChecked()) {
            pGuard->click();
            settle(30, m_options.nStepTimeoutMs);
        }
        QJsonObject object;
        object.insert("action", "click");
        object.insert("context", sContext);
        object.insert("widget", sPath);
        object.insert("extra", sText);
        object.insert("ms", (double)timer.elapsed());
        object.insert("buttonDeleted", pGuard.isNull());
        record("control", object);
        nUsed++;
    }

    if (nUsed >= nBudget) {
        QJsonObject object;
        object.insert("context", sContext);
        object.insert("budget", nBudget);
        record("control_budget_exhausted", object);
    }
}

void XBVSmoke::inventoryContextMenus(QWidget *pRoot, const QString &sContext)
{
    if (!pRoot || !m_options.bContextMenus) {
        return;
    }

    QList<QWidget *> listTargets;
    QList<QAbstractScrollArea *> listAreas = pRoot->findChildren<QAbstractScrollArea *>();
    for (qint32 i = 0; i < listAreas.count(); i++) {
        if (listAreas.at(i)->isVisible()) {
            listTargets.append(listAreas.at(i)->viewport());
        }
    }
    listTargets.append(pRoot);

    for (qint32 i = 0; (i < listTargets.count()) && (i < 12); i++) {
        QWidget *pTarget = listTargets.at(i);
        QString sTargetPath = widgetPath(pTarget->parentWidget() ? pTarget->parentWidget() : pTarget);
        setStep(sContext + " | contextmenu " + sTargetPath);
        m_sContextMenuContext = sContext + "_" + safeName(sTargetPath.section('/', -1));
        QPoint point(qMax(2, pTarget->width() / 3), qMax(2, pTarget->height() / 3));
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, pTarget->mapToGlobal(point));
        QElapsedTimer timer;
        timer.start();
        QApplication::sendEvent(pTarget, &event);
        settle(300, 5000);
        // Make sure nothing is left open.
        waitModalGone(2000);
        QJsonObject object;
        object.insert("context", sContext);
        object.insert("target", sTargetPath);
        object.insert("accepted", event.isAccepted());
        object.insert("ms", (double)timer.elapsed());
        record("contextmenu_request", object);
    }
}

// ------------------------------------------------------------------ dialogs

void XBVSmoke::triggerWithDialogWalk(QAction *pAction, const QString &sName)
{
    if (!pAction) {
        return;
    }

    setStep("action " + sName);
    QElapsedTimer timer;
    timer.start();

    QTimer::singleShot(400, this, [this, sName]() { walkActiveDialog(sName); });
    pAction->trigger();
    settle(100, m_options.nStepTimeoutMs);
    waitModalGone(m_options.nStepTimeoutMs);

    QJsonObject object;
    object.insert("action", sName);
    object.insert("ms", (double)timer.elapsed());

    if (actionText(pAction->text()).contains("Copy File")) {
        QString sClipboard = QApplication::clipboard()->text();
        object.insert("clipboard", sClipboard);
        check(!sClipboard.isEmpty() && !m_sFileTag.isEmpty() ? QFileInfo(sClipboard).exists() : true, "Copy File Path put an existing path on the clipboard", sClipboard);
    }

    record("menuaction", object);
}

void XBVSmoke::walkActiveDialog(const QString &sName)
{
    QWidget *pModal = QApplication::activeModalWidget();
    QDialog *pDialog = qobject_cast<QDialog *>(pModal);

    if (!pDialog) {
        QJsonObject object;
        object.insert("action", sName);
        object.insert("modal", pModal ? QString(pModal->metaObject()->className()) : QString("none"));
        record("nodialog", object);
        return;
    }

    m_pWalkedDialog = pDialog;
    QString sClass = QString(pDialog->metaObject()->className());
    QString sContext = "dialog:" + sName;
    setStep(sContext);

    QJsonObject object;
    object.insert("name", sName);
    object.insert("class", sClass);
    object.insert("title", pDialog->windowTitle());
    object.insert("size", QString("%1x%2").arg(pDialog->width()).arg(pDialog->height()));
    object.insert("minimum", QString("%1x%2").arg(pDialog->minimumWidth()).arg(pDialog->minimumHeight()));
    record("dialog", object);

    settle(250, 5000);
    screenshot(pDialog, sContext);
    layoutChecks(pDialog, sContext);

    QFileDialog *pFileDialog = qobject_cast<QFileDialog *>(pDialog);

    if (!pFileDialog) {
        // Options-style page lists
        QList<QListWidget *> listLists = pDialog->findChildren<QListWidget *>("listWidgetOptions");
        for (qint32 i = 0; i < listLists.count(); i++) {
            QListWidget *pList = listLists.at(i);
            qint32 nOriginal = pList->currentRow();
            for (qint32 nRow = 0; nRow < pList->count(); nRow++) {
                QString sPage = pList->item(nRow)->text();
                setStep(sContext + " page " + sPage);
                pList->setCurrentRow(nRow);
                settle(120, 5000);
                screenshot(pDialog, sContext + "_page" + QString::number(nRow) + "_" + sPage);
                layoutChecks(pDialog, sContext + " page:" + sPage);
                selectionLeakCheck(pList, sContext + " page:" + sPage);
                exerciseControls(pDialog, sContext + " page:" + sPage, 40, true);
            }
            if (nOriginal >= 0) {
                pList->setCurrentRow(nOriginal);
            }
        }

        if (listLists.isEmpty()) {
            // Demangle: feed a mangled name so the output side gets exercised.
            QList<QLineEdit *> listEdits = pDialog->findChildren<QLineEdit *>();
            for (qint32 i = 0; i < listEdits.count(); i++) {
                if (listEdits.at(i)->isVisible() && !listEdits.at(i)->isReadOnly()) {
                    listEdits.at(i)->setText("_ZN9wikipedia7article6formatEv");
                    settle(80, 5000);
                    break;
                }
            }
            exerciseControls(pDialog, sContext, 60, true);
            settle(80, 5000);
            screenshot(pDialog, sContext + "_after");
            layoutChecks(pDialog, sContext + " after");
        }
    }

    setStep(sContext + " close");

    // With a file open (second menu walk) leave the Options dialog through OK
    // so the save + adjustView path runs; everything else is cancelled.
    QPushButton *pOK = (!m_sFileTag.isEmpty() && !pFileDialog) ? pDialog->findChild<QPushButton *>("pushButtonOK") : nullptr;
    if (pOK && pOK->isVisible() && pOK->isEnabled() && sClass.contains("Options")) {
        pOK->click();
        settle(50, 5000);
        if (pDialog->isVisible()) {
            pDialog->reject();
        }
        QJsonObject closeInfo;
        closeInfo.insert("dialog", sName);
        closeInfo.insert("via", "OK");
        record("dialogclose", closeInfo);
    } else {
        pDialog->reject();
    }
    settle(50, 5000);
    m_pWalkedDialog = nullptr;
}

void XBVSmoke::walkMenus(const QString &sContext)
{
    if (!m_options.bDialogs) {
        return;
    }

    QMenuBar *pMenuBar = m_pMainWindow->menuBar();
    if (!pMenuBar) {
        return;
    }

    QList<QAction *> listTop = pMenuBar->actions();
    QJsonArray arrayMenus;

    for (qint32 i = 0; i < listTop.count(); i++) {
        QMenu *pMenu = listTop.at(i)->menu();
        if (!pMenu) {
            continue;
        }
        arrayMenus.append(menuToJson(pMenu, 0));
    }

    QJsonObject inventory;
    inventory.insert("context", sContext);
    inventory.insert("menus", arrayMenus);
    record("menubar", inventory);

    for (qint32 i = 0; i < listTop.count(); i++) {
        QMenu *pMenu = listTop.at(i)->menu();
        if (!pMenu) {
            continue;
        }
        QList<QAction *> listActions = pMenu->actions();
        for (qint32 j = 0; j < listActions.count(); j++) {
            QAction *pAction = listActions.at(j);
            if (pAction->isSeparator() || !pAction->isEnabled()) {
                continue;
            }
            QString sText = actionText(pAction->text());
            if ((pAction->menuRole() == QAction::QuitRole) || sText.contains("Exit") || sText.contains("Close File")) {
                continue;
            }
            if (pAction->menu()) {
                continue;  // Recent files: inventoried above
            }
            triggerWithDialogWalk(pAction, sContext + "_" + actionText(listTop.at(i)->text()) + "_" + sText);
        }
    }

    // Toolbar buttons mirror the actions; still click each once so the
    // toolbar's own QToolButton path is covered.
    QList<QToolBar *> listToolBars = m_pMainWindow->findChildren<QToolBar *>();
    for (qint32 i = 0; i < listToolBars.count(); i++) {
        QList<QAction *> listActions = listToolBars.at(i)->actions();
        QJsonArray array;
        for (qint32 j = 0; j < listActions.count(); j++) {
            if (!listActions.at(j)->isSeparator()) {
                QJsonObject item;
                item.insert("text", actionText(listActions.at(j)->text()));
                item.insert("enabled", listActions.at(j)->isEnabled());
                array.append(item);
            }
        }
        QJsonObject object;
        object.insert("context", sContext);
        object.insert("toolbar", listToolBars.at(i)->objectName());
        object.insert("actions", array);
        record("toolbar", object);
    }
}

// --------------------------------------------------------------------- files

bool XBVSmoke::openFile(const QString &sFileName, const QString &sHow)
{
    setStep("open(" + sHow + ") " + sFileName);
    QElapsedTimer timer;
    timer.start();

    if (sHow == "drop") {
        dropFile(sFileName);
    } else {
        QMetaObject::invokeMethod(m_pMainWindow, "processFile", Qt::DirectConnection, Q_ARG(QString, sFileName));
    }

    settle(150, m_options.nStepTimeoutMs);
    waitModalGone(m_options.nStepTimeoutMs);

    bool bOpened = (mainPageIndex() == 1);

    QJsonObject object;
    object.insert("how", sHow);
    object.insert("path", sFileName);
    object.insert("opened", bOpened);
    object.insert("ms", (double)timer.elapsed());
    object.insert("title", m_pMainWindow->windowTitle());

    QLabel *pLabelType = m_pMainWindow->findChild<QLabel *>("statusType");
    QLabel *pLabelSize = m_pMainWindow->findChild<QLabel *>("statusSize");
    QLabel *pLabelFile = m_pMainWindow->findChild<QLabel *>("statusFile");
    if (pLabelType) object.insert("statusType", pLabelType->text());
    if (pLabelSize) object.insert("statusSize", pLabelSize->text());
    if (pLabelFile) object.insert("statusFile", pLabelFile->text());

    XFWidgetAdvanced *pViewer = viewer();
    if (pViewer) {
        QComboBox *pCombo = pViewer->findChild<QComboBox *>("comboBoxFileType");
        if (pCombo) {
            object.insert("interpretAs", pCombo->currentText());
            object.insert("interpretCount", pCombo->count());
        }
        XFTreeView *pTree = pViewer->findChild<XFTreeView *>("treeView");
        if (pTree && pTree->model()) {
            object.insert("treeRoots", pTree->model()->rowCount());
        }
    }

    record("open", object);
    check(bOpened, "file opened into the viewer page", sFileName);

    if (bOpened) {
        QString sBase = QFileInfo(sFileName).fileName();
        check(m_pMainWindow->windowTitle().contains(sBase), "window title names the open file", m_pMainWindow->windowTitle());
        check(pLabelType && !pLabelType->text().isEmpty(), "status bar shows the detected type");
        check(pLabelSize && !pLabelSize->text().isEmpty(), "status bar shows the file size");

        // Recent files menu must now list this file.
        bool bInRecent = false;
        QList<QMenu *> listMenus = m_pMainWindow->findChildren<QMenu *>();
        for (qint32 i = 0; i < listMenus.count(); i++) {
            if (actionText(listMenus.at(i)->title()).contains("Recent")) {
                QList<QAction *> listActions = listMenus.at(i)->actions();
                for (qint32 j = 0; j < listActions.count(); j++) {
                    if (listActions.at(j)->text().contains(sBase)) {
                        bInRecent = true;
                    }
                }
            }
        }
        check(bInRecent, "recent files menu lists the opened file", sBase);
    }

    return bOpened;
}

void XBVSmoke::fileLockCheck(const QString &sFileName, const QString &sWhen)
{
    // A read-only viewer must not keep the file locked against rename/delete
    // while it is displayed. The corpus files are copies, so renaming is safe.
    setStep("filelock " + sWhen);
    QString sTemp = sFileName + ".locktest";
    bool bRenamed = QFile::rename(sFileName, sTemp);
    if (bRenamed) {
        QFile::rename(sTemp, sFileName);
    }
    QJsonObject object;
    object.insert("when", sWhen);
    object.insert("renamable", bRenamed);
    record("filelock", object);
    check(bRenamed, "file is not locked (" + sWhen + ")", sFileName);
}

void XBVSmoke::closeFile()
{
    setStep("close file");
    QMetaObject::invokeMethod(m_pMainWindow, "closeCurrentFile", Qt::DirectConnection);
    settle(80, 10000);
    check(mainPageIndex() == 0, "welcome page shown after close");
}

void XBVSmoke::dropFile(const QString &sFileName)
{
    QMimeData *pMimeData = new QMimeData;
    pMimeData->setUrls(QList<QUrl>() << QUrl::fromLocalFile(sFileName));
    QPoint point(m_pMainWindow->width() / 2, m_pMainWindow->height() / 2);

    QDragEnterEvent enterEvent(point, Qt::CopyAction, pMimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(m_pMainWindow, &enterEvent);
    check(enterEvent.isAccepted(), "dragEnterEvent accepted a local file");

    QDropEvent dropEvent(point, Qt::CopyAction, pMimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(m_pMainWindow, &dropEvent);
    check(dropEvent.isAccepted(), "dropEvent accepted a local file");

    delete pMimeData;
}

void XBVSmoke::walkInterpretCombo(const QString &sFileTag)
{
    XFWidgetAdvanced *pViewer = viewer();
    if (!pViewer || !m_options.bInterpret) {
        return;
    }

    QComboBox *pCombo = pViewer->findChild<QComboBox *>("comboBoxFileType");
    XFTreeView *pTree = pViewer->findChild<XFTreeView *>("treeView");
    if (!pCombo || !pTree) {
        return;
    }

    qint32 nOriginal = pCombo->currentIndex();

    for (qint32 i = 0; i < pCombo->count(); i++) {
        if (i == nOriginal) {
            continue;
        }
        setStep(sFileTag + " | interpret as " + pCombo->itemText(i));
        QElapsedTimer timer;
        timer.start();
        pCombo->setCurrentIndex(i);
        settle(60, m_options.nStepTimeoutMs);
        waitModalGone(m_options.nStepTimeoutMs);
        QJsonObject object;
        object.insert("type", pCombo->itemText(i));
        object.insert("treeRoots", pTree->model() ? pTree->model()->rowCount() : -1);
        object.insert("ms", (double)timer.elapsed());
        record("interpret", object);
        // One node of the reinterpreted tree, to instantiate its first panel.
        if (pTree->model() && pTree->model()->rowCount() > 0) {
            pTree->setCurrentIndex(pTree->model()->index(0, 0));
            settle(60, m_options.nStepTimeoutMs);
        }
    }

    setStep(sFileTag + " | interpret restore");
    pCombo->setCurrentIndex(nOriginal);
    settle(60, m_options.nStepTimeoutMs);
    waitModalGone(m_options.nStepTimeoutMs);

    // Reload button
    QToolButton *pReload = pViewer->findChild<QToolButton *>("toolButtonReload");
    if (pReload) {
        setStep(sFileTag + " | reload");
        qint32 nBefore = pTree->model() ? pTree->model()->rowCount() : -1;
        QElapsedTimer timer;
        timer.start();
        pReload->click();
        settle(60, m_options.nStepTimeoutMs);
        waitModalGone(m_options.nStepTimeoutMs);
        qint32 nAfter = pTree->model() ? pTree->model()->rowCount() : -1;
        QJsonObject object;
        object.insert("treeRootsBefore", nBefore);
        object.insert("treeRootsAfter", nAfter);
        object.insert("ms", (double)timer.elapsed());
        record("reload", object);
        check(nBefore == nAfter, "reload keeps the same tree", QString("%1 -> %2").arg(nBefore).arg(nAfter));
    }
}

void XBVSmoke::walkTree(const QString &sFileTag)
{
    XFWidgetAdvanced *pViewer = viewer();
    if (!pViewer) {
        fail("viewer widget not found");
        return;
    }

    XFTreeView *pTree = pViewer->findChild<XFTreeView *>("treeView");
    QStackedWidget *pStack = pViewer->findChild<QStackedWidget *>("stackedWidget");
    if (!pTree || !pStack || !pTree->model()) {
        fail("viewer tree/stack not found");
        return;
    }

    QAbstractItemModel *pModel = pTree->model();
    pTree->expandAll();
    settle(50, 5000);

    // Collect every index depth-first.
    QList<QModelIndex> listIndexes;
    QList<QModelIndex> listStack;
    for (qint32 i = pModel->rowCount() - 1; i >= 0; i--) {
        listStack.append(pModel->index(i, 0));
    }
    while (!listStack.isEmpty() && (listIndexes.count() < m_options.nMaxNodes)) {
        QModelIndex index = listStack.takeLast();
        listIndexes.append(index);
        for (qint32 i = pModel->rowCount(index) - 1; i >= 0; i--) {
            listStack.append(pModel->index(i, 0, index));
        }
    }

    QJsonObject treeInfo;
    treeInfo.insert("nodes", listIndexes.count());
    treeInfo.insert("truncated", !listStack.isEmpty());
    record("tree", treeInfo);

    // Screenshot of the tree itself (tree filters row included).
    screenshot(pTree, sFileTag + "_tree");
    layoutChecks(pViewer, sFileTag + " viewer");

    QSet<QString> setPanelClassesDone;

    for (qint32 i = 0; i < listIndexes.count(); i++) {
        QModelIndex index = listIndexes.at(i);
        QString sName = index.data(Qt::DisplayRole).toString();

        QStringList listPath;
        QModelIndex indexParent = index;
        while (indexParent.isValid()) {
            listPath.prepend(indexParent.data(Qt::DisplayRole).toString());
            indexParent = indexParent.parent();
        }
        QString sPath = listPath.join("/");

        if (m_options.listSkipNodes.contains(sName) || m_options.listSkipNodes.contains(sPath)) {
            QJsonObject object;
            object.insert("node", sPath);
            record("skipped_node", object);
            continue;
        }

        setStep(sFileTag + " | node " + sPath);
        QElapsedTimer timer;
        timer.start();

        pTree->scrollTo(index);
        pTree->setCurrentIndex(index);
        settle(80, m_options.nStepTimeoutMs);
        waitModalGone(m_options.nStepTimeoutMs);

        XBinary::XFHEADER xfHeader = pTree->getSelectedHeader();
        QWidget *pPanel = pStack->currentWidget();
        QString sPanelClass = pPanel ? QString(pPanel->metaObject()->className()) : QString("(none)");

        QJsonObject object;
        object.insert("node", sPath);
        object.insert("index", i);
        object.insert("xfType", (qint32)xfHeader.xfType);
        object.insert("structID", (qint32)xfHeader.structID);
        object.insert("offset", QString("0x%1").arg(QString::number((qulonglong)xfHeader.xLoc.nLocation, 16)));
        object.insert("size", QString("0x%1").arg(QString::number((qulonglong)xfHeader.nSize, 16)));
        object.insert("rows", xfHeader.listRowLocations.count());
        object.insert("panel", sPanelClass);
        object.insert("ms", (double)timer.elapsed());

        QLabel *pLabelSelection = m_pMainWindow->findChild<QLabel *>("statusSelection");
        if (pLabelSelection) {
            object.insert("statusSelection", pLabelSelection->text());
        }
        record("node", object);

        check(pPanel != nullptr, "a detail panel is shown for the node", sPath);

        if (!pPanel) {
            continue;
        }

        QString sContext = sFileTag + " node:" + sPath;
        panelChecks(pPanel, sContext);

        // Status-bar size for TABLE nodes must be rows * record size
        if ((xfHeader.xfType == XBinary::XFTYPE_TABLE) && pLabelSelection && (xfHeader.nSize > 0) && (xfHeader.listRowLocations.count() > 1)) {
            QString sExpected = QString("0x%1").arg(QString::number((qulonglong)(xfHeader.nSize * xfHeader.listRowLocations.count()), 16));
            check(pLabelSelection->text().contains(sExpected), "status bar size of a TABLE node counts all rows",
                  QString("%1 expected size %2").arg(pLabelSelection->text(), sExpected));
        }

        bool bFirstOfClass = !setPanelClassesDone.contains(sPanelClass);
        if (bFirstOfClass) {
            setPanelClassesDone.insert(sPanelClass);
            screenshot(pPanel, sFileTag + "_panel_" + sPanelClass);
            screenshot(m_pMainWindow, sFileTag + "_main_" + sPanelClass);
        }

        // Exercise the controls once per panel class per file (the tables of a
        // PE with 400 nodes are identical widgets), but always for tool panels.
        bool bExercise = bFirstOfClass || (xfHeader.xfType == XBinary::XFTYPE_COMMAND);
        if (bExercise) {
            exerciseControls(pPanel, sContext, 80, false);
            inventoryContextMenus(pPanel, sContext);
            // Re-check after interaction: a control click can re-layout.
            layoutChecks(pPanel, sContext + " after");
        }
    }
}

void XBVSmoke::runFile(qint32 nIndex, const QString &sFileName)
{
    QFileInfo fileInfo(sFileName);
    m_sFileTag = QString("%1_%2").arg(nIndex, 2, 10, QChar('0')).arg(safeName(fileInfo.fileName()));

    QJsonObject object;
    object.insert("path", sFileName);
    object.insert("size", (double)fileInfo.size());
    object.insert("exists", fileInfo.exists());
    record("file", object);

    if (!fileInfo.exists()) {
        fail("sample file missing", sFileName);
        m_sFileTag.clear();
        return;
    }

    bool bOpened = openFile(sFileName, ((nIndex == 0) && m_options.bDrop) ? "drop" : "processFile");

    if (bOpened) {
        screenshot(m_pMainWindow, m_sFileTag + "_00_opened");
        layoutChecks(m_pMainWindow, m_sFileTag + " main");
        fileLockCheck(sFileName, "after open, Info node");
        walkTree(m_sFileTag);
        fileLockCheck(sFileName, "after visiting every node");
        walkInterpretCombo(m_sFileTag);
        if (nIndex == 0) {
            walkMenus(m_sFileTag + "_menus");
        }
    }

    closeFile();
    if (bOpened) {
        fileLockCheck(sFileName, "after close");
        check(!m_pMainWindow->windowTitle().contains(QFileInfo(sFileName).fileName()), "window title drops the file name after close", m_pMainWindow->windowTitle());
    }
    m_sFileTag.clear();
}

int XBVSmoke::run()
{
    QDir().mkpath(m_options.sReportDir);
    QString sReportFile = m_options.sReportDir + QDir::separator() + "smoke_report.jsonl";
    QByteArray baReport = sReportFile.toLocal8Bit();
#ifdef Q_OS_WIN
    // fopen_s() opens exclusively; the driver script wants to tail the report
    // while the run is in progress.
    g_pReportFile = _fsopen(baReport.constData(), "ab", _SH_DENYNO);
#else
    g_pReportFile = fopen(baReport.constData(), "ab");
#endif
#ifdef Q_OS_WIN
    g_nMainThreadId = GetCurrentThreadId();
#ifdef _MSC_VER
    // No "abort() has been called" / assertion dialogs in a headless run.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    {
        // Our own PDB sits next to the exe; Qt's debug PDBs sit next to the Qt DLLs.
        QString sSearchPath = QCoreApplication::applicationDirPath() + ";" + QLibraryInfo::location(QLibraryInfo::BinariesPath) + ";" +
                              QLibraryInfo::location(QLibraryInfo::LibrariesPath);
        QByteArray baSearchPath = QDir::toNativeSeparators(sSearchPath).toLocal8Bit();
        SymInitialize(GetCurrentProcess(), baSearchPath.constData(), TRUE);
    }
    AddVectoredExceptionHandler(1, smokeVectoredHandler);
    SetUnhandledExceptionFilter(smokeExceptionFilter);
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_hMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    g_nWatchdogMs = (DWORD)qMax<qint32>(120000, 3 * m_options.nStepTimeoutMs);
    CloseHandle(CreateThread(nullptr, 0, smokeWatchdogThread, nullptr, 0, nullptr));
#endif

    m_clock.start();
    qInstallMessageHandler(messageHandler);
    m_sentinel.start();

    if (!qgetenv("XBV_SMOKE_SELFCRASH").isEmpty()) {
        // Validates the crash-record path of the harness itself.
        setStep("selfcrash");
        volatile int *pNull = nullptr;
        *pNull = 1;
    }

    QJsonObject start;
    start.insert("platform", QGuiApplication::platformName());
    start.insert("qt", QString(qVersion()));
    start.insert("files", m_options.listFiles.count());
    start.insert("screenshots", m_options.bScreenshots);
    start.insert("controls", m_options.bControls);
    start.insert("contextmenus", m_options.bContextMenus);
    start.insert("interpret", m_options.bInterpret);
    start.insert("dialogs", m_options.bDialogs);
    record("start", start);

    setStep("welcome");
    m_pMainWindow->resize(1280, 860);
    m_pMainWindow->show();
    settle(300, 5000);

    screenshot(m_pMainWindow, "00_welcome");
    layoutChecks(m_pMainWindow, "welcome");
    check(mainPageIndex() == 0, "welcome page is the initial page");

    // Welcome page controls (Open button -> file dialog -> rejected by sentinel)
    exerciseControls(m_pMainWindow->centralWidget(), "welcome", 20, false);

    // Menus/dialogs with no file open
    walkMenus("welcome_menus");

    for (qint32 i = 0; i < m_options.listFiles.count(); i++) {
        runFile(i, m_options.listFiles.at(i));
    }

    // Re-open the first file and leave through the window close path
    if (!m_options.listFiles.isEmpty()) {
        openFile(m_options.listFiles.first(), "processFile");
    }

    setStep("close window");
    m_pMainWindow->close();
    settle(100, 5000);

    QJsonObject summary;
    summary.insert("steps", (double)m_nSteps);
    summary.insert("checks", (double)m_nChecks);
    summary.insert("fails", (double)m_nFails);
    summary.insert("qtWarnings", (double)m_nQtWarnings);
    summary.insert("elapsedMs", (double)m_clock.elapsed());
    record("summary", summary);

    m_sentinel.stop();
    qInstallMessageHandler(nullptr);
    if (g_pReportFile) {
        fclose(g_pReportFile);
        g_pReportFile = nullptr;
    }

    return (int)qMin<qint64>(m_nFails, 200);
}
