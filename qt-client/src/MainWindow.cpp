#include "MainWindow.h"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontDatabase>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QProcess>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>
#endif

namespace {
constexpr int kTaskListMinWidth = 330;
constexpr int kTaskColumnMinWidths[TaskTableModel::ColumnCount] = {96, 76, 44, 96};

QString powershellSingleQuoted(QString value) {
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString commandQuoted(QString value) {
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString resolveShortcutTarget(const QString &path) {
#ifndef Q_OS_WIN
    return path;
#else
    const QFileInfo fileInfo(path);
    if (fileInfo.suffix().compare(QStringLiteral("lnk"), Qt::CaseInsensitive) != 0) {
        return path;
    }

    HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initResult);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        return path;
    }

    IShellLinkW *shellLink = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                      reinterpret_cast<void **>(&shellLink));
    if (FAILED(result) || shellLink == nullptr) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return path;
    }

    QString targetPath = path;
    IPersistFile *persistFile = nullptr;
    result = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persistFile));
    if (SUCCEEDED(result) && persistFile != nullptr) {
        result = persistFile->Load(reinterpret_cast<LPCOLESTR>(path.utf16()), STGM_READ);
        if (SUCCEEDED(result)) {
            wchar_t resolvedPath[MAX_PATH] = {};
            result = shellLink->GetPath(resolvedPath, MAX_PATH, nullptr, SLGP_UNCPRIORITY);
            if (SUCCEEDED(result) && resolvedPath[0] != L'\0') {
                targetPath = QString::fromWCharArray(resolvedPath);
            }
        }
        persistFile->Release();
    }

    shellLink->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return targetPath;
#endif
}

bool isRunnableFile(const QFileInfo &fileInfo) {
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    static const QStringList runnableSuffixes = {
        QStringLiteral("bat"),
        QStringLiteral("cmd"),
        QStringLiteral("exe"),
        QStringLiteral("ps1"),
        QStringLiteral("py"),
        QStringLiteral("js"),
        QStringLiteral("mjs"),
        QStringLiteral("cjs"),
        QStringLiteral("sh"),
        QStringLiteral("pl"),
        QStringLiteral("rb"),
    };
    return runnableSuffixes.contains(suffix);
}

QString runnableCommand(const QFileInfo &fileInfo) {
    const QString fileName = commandQuoted(fileInfo.fileName());
    const QString suffix = fileInfo.suffix().toLower();
    if (suffix == QStringLiteral("py")) {
        return QStringLiteral("python %1").arg(fileName);
    }
    if (suffix == QStringLiteral("ps1")) {
        return QStringLiteral("powershell.exe -NoProfile -ExecutionPolicy Bypass -File %1").arg(fileName);
    }
    if (suffix == QStringLiteral("js") || suffix == QStringLiteral("mjs") || suffix == QStringLiteral("cjs")) {
        return QStringLiteral("node %1").arg(fileName);
    }
    if (suffix == QStringLiteral("sh")) {
        return QStringLiteral("sh %1").arg(fileName);
    }
    if (suffix == QStringLiteral("pl")) {
        return QStringLiteral("perl %1").arg(fileName);
    }
    if (suffix == QStringLiteral("rb")) {
        return QStringLiteral("ruby %1").arg(fileName);
    }
    return fileName;
}

QString firstDroppedRunnablePath(const QMimeData *mimeData) {
    if (!mimeData || !mimeData->hasUrls()) {
        return {};
    }

    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo fileInfo(resolveShortcutTarget(url.toLocalFile()));
        if (isRunnableFile(fileInfo)) {
            return fileInfo.absoluteFilePath();
        }
    }
    return {};
}

QColor ansiColor(int index) {
    static const QColor colors[] = {
        QColor(0x2e, 0x34, 0x40),
        QColor(0xbf, 0x61, 0x6a),
        QColor(0xa3, 0xbe, 0x8c),
        QColor(0xeb, 0xcb, 0x8b),
        QColor(0x81, 0xa1, 0xc1),
        QColor(0xb4, 0x8e, 0xad),
        QColor(0x88, 0xc0, 0xd0),
        QColor(0xe5, 0xe9, 0xf0),
        QColor(0x4c, 0x56, 0x6a),
        QColor(0xbf, 0x61, 0x6a).lighter(125),
        QColor(0xa3, 0xbe, 0x8c).lighter(125),
        QColor(0xeb, 0xcb, 0x8b).lighter(115),
        QColor(0x81, 0xa1, 0xc1).lighter(125),
        QColor(0xb4, 0x8e, 0xad).lighter(125),
        QColor(0x8f, 0xbc, 0xbb).lighter(115),
        QColor(0xec, 0xef, 0xf4),
    };
    return colors[qBound(0, index, 15)];
}

QColor ansi256Color(int index) {
    index = qBound(0, index, 255);
    if (index < 16) {
        return ansiColor(index);
    }
    if (index < 232) {
        const int color = index - 16;
        const int r = color / 36;
        const int g = (color / 6) % 6;
        const int b = color % 6;
        auto component = [](int value) {
            return value == 0 ? 0 : 55 + value * 40;
        };
        return QColor(component(r), component(g), component(b));
    }
    const int gray = 8 + (index - 232) * 10;
    return QColor(gray, gray, gray);
}

QList<int> sgrParameters(const QString &sequence) {
    QList<int> values;
    const QStringList parts = sequence.split(QLatin1Char(';'), Qt::KeepEmptyParts);
    for (const QString &part : parts) {
        bool ok = false;
        const int value = part.toInt(&ok);
        values.append(ok ? value : 0);
    }
    if (values.isEmpty()) {
        values.append(0);
    }
    return values;
}

void applyAnsiSgr(const QList<int> &params, QTextCharFormat *format) {
    if (!format) {
        return;
    }

    for (int i = 0; i < params.size(); ++i) {
        const int code = params.at(i);
        if (code == 0) {
            *format = QTextCharFormat();
        } else if (code == 1) {
            format->setFontWeight(QFont::Bold);
        } else if (code == 3) {
            format->setFontItalic(true);
        } else if (code == 4) {
            format->setFontUnderline(true);
        } else if (code == 22) {
            format->setFontWeight(QFont::Normal);
        } else if (code == 23) {
            format->setFontItalic(false);
        } else if (code == 24) {
            format->setFontUnderline(false);
        } else if (code == 39) {
            format->clearForeground();
        } else if (code == 49) {
            format->clearBackground();
        } else if (code >= 30 && code <= 37) {
            format->setForeground(ansiColor(code - 30));
        } else if (code >= 90 && code <= 97) {
            format->setForeground(ansiColor(code - 90 + 8));
        } else if (code >= 40 && code <= 47) {
            format->setBackground(ansiColor(code - 40));
        } else if (code >= 100 && code <= 107) {
            format->setBackground(ansiColor(code - 100 + 8));
        } else if ((code == 38 || code == 48) && i + 1 < params.size()) {
            const bool foreground = code == 38;
            const int mode = params.at(++i);
            QColor color;
            if (mode == 5 && i + 1 < params.size()) {
                color = ansi256Color(params.at(++i));
            } else if (mode == 2 && i + 3 < params.size()) {
                color = QColor(qBound(0, params.at(++i), 255),
                               qBound(0, params.at(++i), 255),
                               qBound(0, params.at(++i), 255));
            }
            if (color.isValid()) {
                if (foreground) {
                    format->setForeground(color);
                } else {
                    format->setBackground(color);
                }
            }
        }
    }
}

void appendAnsiText(QPlainTextEdit *edit, const QString &text, QTextCharFormat *format) {
    QTextCursor cursor(edit->document());
    cursor.movePosition(QTextCursor::End);
    if (!format) {
        return;
    }

    QString plain;

    auto flushPlainText = [&]() {
        if (!plain.isEmpty()) {
            cursor.insertText(plain, *format);
            plain.clear();
        }
    };

    for (int i = 0; i < text.size();) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\x1b') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('[')) {
            int end = i + 2;
            while (end < text.size()) {
                const ushort u = text.at(end).unicode();
                if (u >= 0x40 && u <= 0x7e) {
                    break;
                }
                ++end;
            }
            if (end < text.size()) {
                flushPlainText();
                if (text.at(end) == QLatin1Char('m')) {
                    applyAnsiSgr(sgrParameters(text.mid(i + 2, end - i - 2)), format);
                }
                i = end + 1;
                continue;
            }
        }

        if (ch == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
            plain.append(QLatin1Char('\n'));
            i += 2;
            continue;
        }

        if (ch == QLatin1Char('\r')) {
            flushPlainText();
            cursor.movePosition(QTextCursor::End);
            cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            ++i;
            continue;
        }
        plain.append(ch);
        ++i;
    }

    flushPlainText();
}

QString terminalTextForEntries(const QList<LogEntry> &entries, bool hasVisibleLog) {
    QString text;
    bool hasRenderedText = hasVisibleLog;
    bool previousEndedAtLineBoundary = !hasVisibleLog;

    for (const LogEntry &entry : entries) {
        const QString &entryText = entry.text;
        if (entryText.isEmpty()) {
            continue;
        }
        const bool startsAtLineBoundary = entryText.startsWith(QLatin1Char('\r')) || entryText.startsWith(QLatin1Char('\n'));
        if (hasRenderedText && !previousEndedAtLineBoundary && !startsAtLineBoundary) {
            if (!text.isEmpty()) {
                text.append(QLatin1Char('\n'));
            } else if (hasVisibleLog) {
                text.append(QLatin1Char('\n'));
            }
        }
        text.append(entryText);
        hasRenderedText = true;
        previousEndedAtLineBoundary = entryText.endsWith(QLatin1Char('\n')) || entryText.endsWith(QLatin1Char('\r'));
    }
    return text;
}

QString serviceStartTypeLabel() {
#ifndef Q_OS_WIN
    return {};
#else
    QProcess queryConfig;
    queryConfig.start(QStringLiteral("sc.exe"), {QStringLiteral("qc"), QStringLiteral("RunBay")});
    if (!queryConfig.waitForFinished(2000) || queryConfig.exitCode() != 0) {
        queryConfig.kill();
        return QStringLiteral("startup unknown");
    }

    const QString output = QString::fromLocal8Bit(queryConfig.readAllStandardOutput() + queryConfig.readAllStandardError());
    if (output.contains(QStringLiteral("DISABLED"), Qt::CaseInsensitive)) {
        return QStringLiteral("disabled");
    }
    if (output.contains(QStringLiteral("DEMAND_START"), Qt::CaseInsensitive)) {
        return QStringLiteral("manual start");
    }
    if (output.contains(QStringLiteral("AUTO_START"), Qt::CaseInsensitive)) {
        return QStringLiteral("auto start");
    }
    return QStringLiteral("startup unknown");
#endif
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    buildUi();

    connect(&m_api, &ApiClient::tasksLoaded, this, &MainWindow::onTasksLoaded);
    connect(&m_api, &ApiClient::logsLoaded, this, &MainWindow::onLogsLoaded);
    connect(&m_api, &ApiClient::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 5000);
    });
    connect(&m_api, &ApiClient::healthChanged, this, [this](bool ok) {
        setDaemonConnected(ok);
        if (ok) {
            m_serviceStartAttempted = false;
            m_serviceStatusMessageActive = false;
            m_connectionLabel->setText(QStringLiteral("Connected: 127.0.0.1:8732"));
        } else if (!m_serviceStartAttempted) {
            ensureDaemonServiceStarted();
        } else if (!m_serviceStatusMessageActive) {
            m_connectionLabel->setText(QStringLiteral("Disconnected"));
        }
    });

    connect(&m_refreshTimer, &QTimer::timeout, this, &MainWindow::refresh);
    connect(&m_logTimer, &QTimer::timeout, this, [this]() {
        if (!m_daemonConnected) {
            return;
        }
        const QString id = selectedTaskId();
        if (!id.isEmpty()) {
            m_api.fetchLogs(id, id == m_loadedLogTaskId ? m_lastLogEntryId : 0);
        }
    });

    m_refreshTimer.start(2500);
    m_logTimer.start(1200);
    ensureDaemonServiceStarted();
    refresh();
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("RunBay"));
    resize(1180, 760);
    setMinimumSize(920, 560);
    applyStyle();

    QMenu *serviceMenu = menuBar()->addMenu(QStringLiteral("Service"));
    QAction *installServiceAction = serviceMenu->addAction(QStringLiteral("Install Service"));
    QAction *startServiceAction = serviceMenu->addAction(QStringLiteral("Start Service"));
    QAction *stopServiceAction = serviceMenu->addAction(QStringLiteral("Stop Service"));
    serviceMenu->addSeparator();
    QAction *deleteServiceAction = serviceMenu->addAction(QStringLiteral("Delete Service"));
    connect(installServiceAction, &QAction::triggered, this, &MainWindow::installService);
    connect(startServiceAction, &QAction::triggered, this, &MainWindow::startService);
    connect(stopServiceAction, &QAction::triggered, this, &MainWindow::stopService);
    connect(deleteServiceAction, &QAction::triggered, this, &MainWindow::deleteService);

    QToolBar *toolbar = addToolBar(QStringLiteral("Tasks"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    QAction *addAction = toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("Add"));
    connect(addAction, &QAction::triggered, this, &MainWindow::addTask);

    m_startAction = toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Start"));
    m_stopAction = toolbar->addAction(style()->standardIcon(QStyle::SP_MediaStop), QStringLiteral("Stop"));
    m_restartAction = toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Restart"));
    m_deleteAction = toolbar->addAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("Delete"));
    toolbar->addSeparator();
    QAction *refreshAction = toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Refresh"));

    connect(m_startAction, &QAction::triggered, this, &MainWindow::startSelectedTask);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopSelectedTask);
    connect(m_restartAction, &QAction::triggered, this, &MainWindow::restartSelectedTask);
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::deleteSelectedTask);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refresh);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search tasks"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(260);
    toolbar->addWidget(m_searchEdit);

    m_proxyModel.setSourceModel(&m_taskModel);
    m_proxyModel.setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel.setFilterKeyColumn(-1);
    connect(m_searchEdit, &QLineEdit::textChanged, &m_proxyModel, &QSortFilterProxyModel::setFilterFixedString);

    m_taskView = new QTableView(this);
    m_taskView->setModel(&m_proxyModel);
    m_taskView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_taskView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_taskView->setDragDropMode(QAbstractItemView::DropOnly);
    m_taskView->setDefaultDropAction(Qt::CopyAction);
    m_taskView->setAlternatingRowColors(true);
    m_taskView->setSortingEnabled(true);
    m_taskView->setShowGrid(false);
    m_taskView->setFrameShape(QFrame::NoFrame);
    m_taskView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_taskView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_taskView->verticalHeader()->setVisible(false);
    m_taskView->verticalHeader()->setDefaultSectionSize(34);
    m_taskView->setWordWrap(false);
    m_taskView->setTextElideMode(Qt::ElideRight);
    m_taskView->horizontalHeader()->setStretchLastSection(false);
    m_taskView->horizontalHeader()->setSectionsMovable(false);
    m_taskView->horizontalHeader()->setSectionsClickable(true);
    m_taskView->horizontalHeader()->setHighlightSections(false);
    m_taskView->horizontalHeader()->setSortIndicatorShown(true);
    m_taskView->horizontalHeader()->setCascadingSectionResizes(false);
    m_taskView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_taskView->horizontalHeader()->setSectionResizeMode(TaskTableModel::NameColumn, QHeaderView::Interactive);
    m_taskView->horizontalHeader()->setSectionResizeMode(TaskTableModel::StatusColumn, QHeaderView::Interactive);
    m_taskView->horizontalHeader()->setSectionResizeMode(TaskTableModel::PidColumn, QHeaderView::Interactive);
    m_taskView->horizontalHeader()->setSectionResizeMode(TaskTableModel::CommandColumn, QHeaderView::Interactive);
    m_taskView->horizontalHeader()->setMinimumSectionSize(44);
    m_taskView->setColumnWidth(TaskTableModel::NameColumn, 190);
    m_taskView->setColumnWidth(TaskTableModel::StatusColumn, 110);
    m_taskView->setColumnWidth(TaskTableModel::PidColumn, 80);
    m_taskView->setColumnWidth(TaskTableModel::CommandColumn, 420);
    m_taskView->setAcceptDrops(true);
    m_taskView->installEventFilter(this);
    m_taskView->viewport()->setAcceptDrops(true);
    m_taskView->viewport()->installEventFilter(this);

    connect(m_taskView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_taskView, &QTableView::doubleClicked, this, &MainWindow::editTaskAt);
    connect(m_taskView->horizontalHeader(), &QHeaderView::sectionResized, this, &MainWindow::onTaskHeaderSectionResized);

    m_detailLabel = new QLabel(QStringLiteral("No task selected"), this);
    m_detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setObjectName(QStringLiteral("DetailLabel"));
    m_detailLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_summaryLabel = new QLabel(QStringLiteral("0 tasks"), this);
    m_summaryLabel->setObjectName(QStringLiteral("SummaryLabel"));

    m_logTitleLabel = new QLabel(QStringLiteral("Logs"), this);
    m_logTitleLabel->setObjectName(QStringLiteral("SectionTitle"));

    QToolButton *clearLogButton = new QToolButton(this);
    clearLogButton->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    clearLogButton->setToolTip(QStringLiteral("Clear log"));
    clearLogButton->setAutoRaise(true);
    connect(clearLogButton, &QToolButton::clicked, this, &MainWindow::clearLogView);

    QFrame *logToolbar = new QFrame(this);
    logToolbar->setFrameShape(QFrame::NoFrame);
    QHBoxLayout *logToolbarLayout = new QHBoxLayout(logToolbar);
    logToolbarLayout->setContentsMargins(0, 0, 0, 0);
    logToolbarLayout->setSpacing(6);
    logToolbarLayout->addWidget(m_logTitleLabel);
    logToolbarLayout->addStretch(1);
    logToolbarLayout->addWidget(clearLogButton);

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    m_logView->setFrameShape(QFrame::NoFrame);
    QFont logFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    logFont.setPointSize(10);
    m_logView->setFont(logFont);

    QFrame *leftPane = new QFrame(this);
    leftPane->setObjectName(QStringLiteral("LeftPane"));
    leftPane->setMinimumWidth(kTaskListMinWidth);
    leftPane->setFrameShape(QFrame::NoFrame);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(m_taskView);

    QFrame *rightPane = new QFrame(this);
    rightPane->setObjectName(QStringLiteral("RightPane"));
    rightPane->setMinimumWidth(360);
    rightPane->setFrameShape(QFrame::NoFrame);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(14, 12, 14, 12);
    rightLayout->setSpacing(10);
    rightLayout->addWidget(m_summaryLabel);
    rightLayout->addWidget(m_detailLabel);
    rightLayout->addWidget(logToolbar);
    rightLayout->addWidget(m_logView, 1);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    splitter->setOpaqueResize(true);
    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({kTaskListMinWidth, 850});
    connect(splitter, &QSplitter::splitterMoved, this, [this]() {
        const int viewportWidth = m_taskView->viewport()->width();
        if (viewportWidth > 0 && viewportWidth != m_lastTableViewportWidth) {
            resizeTaskColumnsToViewport(viewportWidth);
            m_lastTableViewportWidth = viewportWidth;
        }
    });
    setCentralWidget(splitter);

    m_connectionLabel = new QLabel(QStringLiteral("Checking daemon..."), this);
    statusBar()->addPermanentWidget(m_connectionLabel);

    updateActions();
}

void MainWindow::refresh() {
    m_api.health();
    m_api.listTasks();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    const bool watchedTaskList = watched == m_taskView || watched == m_taskView->viewport();
    if (!watchedTaskList) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (event->type() == QEvent::DragEnter) {
        auto *dragEvent = static_cast<QDragEnterEvent *>(event);
        if (!firstDroppedRunnablePath(dragEvent->mimeData()).isEmpty()) {
            dragEvent->acceptProposedAction();
            return true;
        }
    }

    if (event->type() == QEvent::DragMove) {
        auto *dragEvent = static_cast<QDragMoveEvent *>(event);
        if (!firstDroppedRunnablePath(dragEvent->mimeData()).isEmpty()) {
            dragEvent->acceptProposedAction();
            return true;
        }
    }

    if (event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        const QString runnablePath = firstDroppedRunnablePath(dropEvent->mimeData());
        if (!runnablePath.isEmpty()) {
            dropEvent->acceptProposedAction();
            addTaskFromRunnable(runnablePath);
            return true;
        }
    }

    if (watched == m_taskView->viewport() && event->type() == QEvent::Resize) {
        const int viewportWidth = m_taskView->viewport()->width();
        if (viewportWidth > 0 && viewportWidth != m_lastTableViewportWidth) {
            resizeTaskColumnsToViewport(viewportWidth);
        }
        m_lastTableViewportWidth = viewportWidth;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::addTask() {
    QString name;
    QString command;
    QString cwd;
    bool startOnLaunch = false;
    if (!taskEditorDialog(QStringLiteral("Add Task"), nullptr, &name, &command, &cwd, &startOnLaunch)) {
        return;
    }
    m_api.createTask(name, command, cwd, startOnLaunch);
}

void MainWindow::addTaskFromRunnable(const QString &runnablePath) {
    const QFileInfo runnableInfo(runnablePath);
    if (!isRunnableFile(runnableInfo)) {
        return;
    }

    QString name = QStringLiteral("run %1").arg(runnableInfo.completeBaseName());
    QString command = runnableCommand(runnableInfo);
    QString cwd = runnableInfo.absolutePath();
    bool startOnLaunch = false;
    if (!taskEditorDialog(QStringLiteral("Add Task"), nullptr, &name, &command, &cwd, &startOnLaunch)) {
        return;
    }
    m_api.createTask(name, command, cwd, startOnLaunch);
}

bool MainWindow::taskEditorDialog(const QString &title, const Task *task, QString *name, QString *command, QString *cwd, bool *startOnLaunch) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(560);

    QLineEdit nameEdit;
    QLineEdit commandEdit;
    QLineEdit cwdEdit;
    QCheckBox startOnLaunchCheck(QStringLiteral("Start when daemon starts"));
    nameEdit.setMinimumWidth(390);
    commandEdit.setMinimumWidth(390);
    cwdEdit.setMinimumWidth(390);
    commandEdit.setPlaceholderText(QStringLiteral("python app.py"));
    cwdEdit.setPlaceholderText(QStringLiteral("Optional working directory"));
    if (task != nullptr) {
        nameEdit.setText(task->name);
        commandEdit.setText(task->command);
        cwdEdit.setText(task->cwd);
        startOnLaunchCheck.setChecked(task->startOnLaunch);
    } else {
        nameEdit.setText(*name);
        commandEdit.setText(*command);
        cwdEdit.setText(*cwd);
        startOnLaunchCheck.setChecked(*startOnLaunch);
    }

    QFormLayout form(&dialog);
    form.addRow(QStringLiteral("Name"), &nameEdit);
    form.addRow(QStringLiteral("Command"), &commandEdit);
    form.addRow(QStringLiteral("Working directory"), &cwdEdit);
    form.addRow(QStringLiteral("Startup"), &startOnLaunchCheck);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (nameEdit.text().trimmed().isEmpty() || commandEdit.text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("RunBay"), QStringLiteral("Name and command are required."));
        return false;
    }
    *name = nameEdit.text().trimmed();
    *command = commandEdit.text().trimmed();
    *cwd = cwdEdit.text().trimmed();
    *startOnLaunch = startOnLaunchCheck.isChecked();
    return true;
}

void MainWindow::editTaskAt(const QModelIndex &index) {
    if (!index.isValid()) {
        return;
    }

    const QModelIndex sourceIndex = m_proxyModel.mapToSource(index);
    const Task task = m_taskModel.taskAt(sourceIndex.row());
    if (task.id.isEmpty()) {
        return;
    }

    QString name;
    QString command;
    QString cwd;
    bool startOnLaunch = false;
    if (!taskEditorDialog(QStringLiteral("Edit Task"), &task, &name, &command, &cwd, &startOnLaunch)) {
        return;
    }
    m_api.updateTask(task.id, name, command, cwd, startOnLaunch);
}

void MainWindow::onTaskHeaderSectionResized(int logicalIndex, int oldSize, int newSize) {
    Q_UNUSED(oldSize)
    Q_UNUSED(newSize)

    if (m_resizingColumns) {
        return;
    }
    resizeTrailingTaskColumnsToViewport(logicalIndex);
}

void MainWindow::startSelectedTask() {
    const QString id = selectedTaskId();
    if (!id.isEmpty()) {
        m_api.startTask(id);
    }
}

void MainWindow::stopSelectedTask() {
    const QString id = selectedTaskId();
    if (!id.isEmpty()) {
        m_api.stopTask(id);
    }
}

void MainWindow::restartSelectedTask() {
    const QString id = selectedTaskId();
    if (!id.isEmpty()) {
        m_api.restartTask(id);
    }
}

void MainWindow::deleteSelectedTask() {
    const QString id = selectedTaskId();
    if (id.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Delete Task"), QStringLiteral("Delete the selected task?")) == QMessageBox::Yes) {
        m_api.deleteTask(id);
    }
}

void MainWindow::clearLogView() {
    m_logView->clear();
    m_logFormat = QTextCharFormat();
}

void MainWindow::installService() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Service installation is only available on Windows."));
    return;
#else
    QString daemonPath = bundledDaemonPath();
    if (daemonPath.isEmpty()) {
        daemonPath = QFileDialog::getOpenFileName(this, QStringLiteral("Select RunBay daemon"),
                                                  QCoreApplication::applicationDirPath(),
                                                  QStringLiteral("Executable (*.exe);;All files (*.*)"));
        if (daemonPath.isEmpty()) {
            return;
        }
    }

    if (!installServiceWithDaemon(daemonPath)) {
        QMessageBox::warning(this, QStringLiteral("RunBay"), QStringLiteral("Failed to open the service installer."));
        return;
    }

    setServiceStatus(QStringLiteral("Service installer requested administrator approval..."));
    QTimer::singleShot(3000, this, [this]() {
        m_serviceStartAttempted = false;
        refresh();
    });
#endif
}

void MainWindow::deleteService() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Service deletion is only available on Windows."));
    return;
#else
    if (QMessageBox::question(this, QStringLiteral("Delete Service"), QStringLiteral("Delete the RunBay service?")) != QMessageBox::Yes) {
        return;
    }

    const QString command = QStringLiteral(
        "$ErrorActionPreference='Stop'; "
        "$existing=Get-Service -Name 'RunBay' -ErrorAction SilentlyContinue; "
        "if (-not $existing) { exit 0; } "
        "if ($existing.Status -ne 'Stopped') { sc.exe stop RunBay | Out-Null; Start-Sleep -Milliseconds 700; } "
        "sc.exe delete RunBay | Out-Null;");
    if (!runElevatedPowerShell(command)) {
        QMessageBox::warning(this, QStringLiteral("RunBay"), QStringLiteral("Failed to open the service deletion prompt."));
        return;
    }

    setServiceStatus(QStringLiteral("Service deletion requested administrator approval..."));
    QTimer::singleShot(3000, this, [this]() {
        m_serviceStartAttempted = false;
        refresh();
    });
#endif
}

void MainWindow::startService() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Service control is only available on Windows."));
    return;
#else
    const QString command = QStringLiteral(
        "$ErrorActionPreference='Stop'; "
        "$existing=Get-Service -Name 'RunBay' -ErrorAction SilentlyContinue; "
        "if (-not $existing) { throw 'RunBay service is not installed.'; } "
        "if ($existing.Status -ne 'Running') { sc.exe start RunBay | Out-Null; }");
    if (!runElevatedPowerShell(command)) {
        QMessageBox::warning(this, QStringLiteral("RunBay"), QStringLiteral("Failed to open the service start prompt."));
        return;
    }

    setServiceStatus(QStringLiteral("Service start requested administrator approval..."));
    QTimer::singleShot(3000, this, [this]() {
        m_serviceStartAttempted = false;
        refresh();
    });
#endif
}

void MainWindow::stopService() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Service control is only available on Windows."));
    return;
#else
    const QString command = QStringLiteral(
        "$ErrorActionPreference='Stop'; "
        "$existing=Get-Service -Name 'RunBay' -ErrorAction SilentlyContinue; "
        "if (-not $existing) { throw 'RunBay service is not installed.'; } "
        "if ($existing.Status -ne 'Stopped') { sc.exe stop RunBay | Out-Null; }");
    if (!runElevatedPowerShell(command)) {
        QMessageBox::warning(this, QStringLiteral("RunBay"), QStringLiteral("Failed to open the service stop prompt."));
        return;
    }

    setServiceStatus(QStringLiteral("Service stop requested administrator approval..."));
    QTimer::singleShot(3000, this, [this]() {
        m_serviceStartAttempted = false;
        refresh();
    });
#endif
}

QString MainWindow::bundledDaemonPath() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/runbayd.exe"),
        appDir + QStringLiteral("/daemon.exe"),
        appDir + QStringLiteral("/runbay-daemon.exe"),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool MainWindow::installServiceWithDaemon(const QString &daemonPath) {
#ifndef Q_OS_WIN
    Q_UNUSED(daemonPath)
    return false;
#else
    const QFileInfo daemonInfo(daemonPath);
    if (!daemonInfo.exists() || !daemonInfo.isFile()) {
        QMessageBox::warning(this, QStringLiteral("RunBay"), QStringLiteral("Selected daemon executable does not exist."));
        return false;
    }

    const QString quotedDaemon = powershellSingleQuoted(daemonInfo.absoluteFilePath());
    const QString installCommand = QStringLiteral(
        "$ErrorActionPreference='Stop'; "
        "$daemon=%1; "
        "$binPath='\"' + $daemon + '\"'; "
        "$existing=Get-Service -Name 'RunBay' -ErrorAction SilentlyContinue; "
        "if ($existing) { sc.exe stop RunBay | Out-Null; Start-Sleep -Milliseconds 500; sc.exe delete RunBay | Out-Null; Start-Sleep -Milliseconds 500; } "
        "sc.exe create RunBay binPath= $binPath start= auto DisplayName= 'RunBay' | Out-Null; "
        "sc.exe description RunBay 'RunBay daemon service' | Out-Null; "
        "sc.exe failure RunBay reset= 60 actions= restart/5000/restart/5000/none/0 | Out-Null; "
        "sc.exe start RunBay | Out-Null;")
                                       .arg(quotedDaemon);
    return runElevatedPowerShell(installCommand);
#endif
}

bool MainWindow::runElevatedPowerShell(const QString &command) {
#ifndef Q_OS_WIN
    Q_UNUSED(command)
    return false;
#else
    const QString elevateCommand = QStringLiteral(
        "Start-Process -FilePath 'powershell.exe' "
        "-ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-Command',%1) "
        "-Verb RunAs")
                                       .arg(powershellSingleQuoted(command));

    return QProcess::startDetached(QStringLiteral("powershell.exe"),
                                   {QStringLiteral("-NoProfile"),
                                    QStringLiteral("-ExecutionPolicy"),
                                    QStringLiteral("Bypass"),
                                    QStringLiteral("-Command"),
                                    elevateCommand});
#endif
}

void MainWindow::onSelectionChanged() {
    updateActions();
    const QString id = selectedTaskId();
    if (!id.isEmpty()) {
        if (id != m_loadedLogTaskId) {
            m_loadedLogTaskId = id;
            m_lastLogEntryId = 0;
            m_logFormat = QTextCharFormat();
            m_logView->clear();
        }
        m_api.fetchLogs(id);
    } else {
        m_loadedLogTaskId.clear();
        m_lastLogEntryId = 0;
        m_logFormat = QTextCharFormat();
        m_logView->clear();
        m_detailLabel->setText(QStringLiteral("No task selected"));
    }
}

void MainWindow::onTasksLoaded(const QList<Task> &tasks) {
    setDaemonConnected(true);
    const QString previous = selectedTaskId();
    m_taskModel.setTasks(tasks);

    if (!m_columnsSizedToContents && !tasks.isEmpty()) {
        m_resizingColumns = true;
        m_taskView->resizeColumnsToContents();
        m_taskView->setColumnWidth(TaskTableModel::NameColumn, qMax(m_taskView->columnWidth(TaskTableModel::NameColumn), 160));
        m_taskView->setColumnWidth(TaskTableModel::StatusColumn, qMax(m_taskView->columnWidth(TaskTableModel::StatusColumn), 105));
        m_taskView->setColumnWidth(TaskTableModel::PidColumn, qMax(m_taskView->columnWidth(TaskTableModel::PidColumn), 72));
        m_taskView->horizontalHeader()->setSectionResizeMode(TaskTableModel::NameColumn, QHeaderView::Interactive);
        m_taskView->horizontalHeader()->setSectionResizeMode(TaskTableModel::StatusColumn, QHeaderView::Interactive);
        m_taskView->horizontalHeader()->setSectionResizeMode(TaskTableModel::PidColumn, QHeaderView::Interactive);
        m_taskView->horizontalHeader()->setSectionResizeMode(TaskTableModel::CommandColumn, QHeaderView::Interactive);
        m_resizingColumns = false;
        resizeTaskColumnsToViewport(m_taskView->viewport()->width());
        m_lastTableViewportWidth = m_taskView->viewport()->width();
        m_columnsSizedToContents = true;
    }

    int running = 0;
    int failed = 0;
    for (const Task &task : tasks) {
        if (task.status == QStringLiteral("running")) {
            ++running;
        } else if (task.status == QStringLiteral("failed")) {
            ++failed;
        }
    }
    m_summaryLabel->setText(QStringLiteral("%1 tasks  |  %2 running  |  %3 failed").arg(tasks.size()).arg(running).arg(failed));

    bool restoredSelection = false;
    if (!previous.isEmpty()) {
        for (int row = 0; row < m_taskModel.rowCount(); ++row) {
            if (m_taskModel.taskIdAt(row) == previous) {
                const QModelIndex sourceIndex = m_taskModel.index(row, 0);
                const QModelIndex proxyIndex = m_proxyModel.mapFromSource(sourceIndex);
                if (proxyIndex.isValid()) {
                    m_taskView->selectRow(proxyIndex.row());
                    restoredSelection = true;
                }
                break;
            }
        }
    }
    if (!restoredSelection && previous.isEmpty() && m_proxyModel.rowCount() > 0) {
        m_taskView->selectRow(0);
    }

    updateActions();
}

void MainWindow::onLogsLoaded(const QString &taskId, const QList<LogEntry> &entries, quint64 startId, quint64 endId, bool truncated) {
    if (taskId != selectedTaskId()) {
        return;
    }

    const bool resetLog = taskId != m_loadedLogTaskId || truncated;
    QList<LogEntry> newEntries;
    for (const LogEntry &entry : entries) {
        if (resetLog || entry.id > m_lastLogEntryId) {
            newEntries.append(entry);
        }
    }
    if (newEntries.isEmpty() && !resetLog) {
        m_lastLogEntryId = qMax(m_lastLogEntryId, endId);
        return;
    }

    const QTextCursor savedCursor = m_logView->textCursor();
    const bool hadSelection = savedCursor.hasSelection();
    QScrollBar *verticalScrollBar = m_logView->verticalScrollBar();
    const bool wasAtBottom = verticalScrollBar->value() == verticalScrollBar->maximum();

    if (resetLog) {
        m_logView->clear();
        m_logFormat = QTextCharFormat();
        if (truncated && startId > 0) {
            appendAnsiText(m_logView, QStringLiteral("--- log history truncated ---\n"), &m_logFormat);
        }
    }

    const bool hasVisibleLog = !resetLog && !m_logView->document()->isEmpty();
    QString text = terminalTextForEntries(newEntries, hasVisibleLog);
    appendAnsiText(m_logView, text, &m_logFormat);

    m_loadedLogTaskId = taskId;
    for (const LogEntry &entry : newEntries) {
        m_lastLogEntryId = qMax(m_lastLogEntryId, entry.id);
    }
    m_lastLogEntryId = qMax(m_lastLogEntryId, endId);

    if (hadSelection && !resetLog) {
        m_logView->setTextCursor(savedCursor);
    } else if (wasAtBottom || resetLog) {
        m_logView->moveCursor(QTextCursor::End);
    }
}

QString MainWindow::selectedTaskId() const {
    const QModelIndexList rows = m_taskView->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return {};
    }
    const QModelIndex sourceIndex = m_proxyModel.mapToSource(rows.first());
    return m_taskModel.taskIdAt(sourceIndex.row());
}

void MainWindow::updateActions() {
    const QModelIndexList rows = m_taskView->selectionModel()->selectedRows();
    const bool hasSelection = !rows.isEmpty();
    m_startAction->setEnabled(hasSelection && m_daemonConnected);
    m_stopAction->setEnabled(hasSelection && m_daemonConnected);
    m_restartAction->setEnabled(hasSelection && m_daemonConnected);
    m_deleteAction->setEnabled(hasSelection && m_daemonConnected);

    if (!hasSelection) {
        return;
    }

    const QModelIndex sourceIndex = m_proxyModel.mapToSource(rows.first());
    const Task task = m_taskModel.taskAt(sourceIndex.row());
    const QString status = m_daemonConnected ? task.status : QStringLiteral("disconnected");
    m_detailLabel->setText(QStringLiteral("<b>%1</b><br>Status: %2<br>Command: %3<br>Cwd: %4<br>Startup: %5")
                               .arg(task.name.toHtmlEscaped(), status.toHtmlEscaped(),
                                    task.command.toHtmlEscaped(), task.cwd.toHtmlEscaped(),
                                    task.startOnLaunch ? QStringLiteral("start when daemon starts")
                                                       : QStringLiteral("manual")));
    if (!m_daemonConnected) {
        return;
    }
    m_startAction->setEnabled(task.status != QStringLiteral("running") && task.status != QStringLiteral("starting"));
    m_stopAction->setEnabled(task.status == QStringLiteral("running") || task.status == QStringLiteral("starting"));
}

void MainWindow::setDaemonConnected(bool connected) {
    if (m_daemonConnected == connected) {
        return;
    }
    m_daemonConnected = connected;
    m_taskModel.setDisconnected(!connected);
    updateActions();
}

void MainWindow::ensureDaemonServiceStarted() {
    m_serviceStartAttempted = true;
    setServiceStatus(QStringLiteral("Checking RunBay service..."));

#ifdef Q_OS_WIN
    QProcess query;
    query.start(QStringLiteral("sc.exe"), {QStringLiteral("query"), QStringLiteral("RunBay")});
    if (!query.waitForFinished(2000)) {
        query.kill();
        setServiceStatus(QStringLiteral("Disconnected; service check timed out"));
        return;
    }

    const QString output = QString::fromLocal8Bit(query.readAllStandardOutput() + query.readAllStandardError());
    if (query.exitCode() != 0) {
        setServiceStatus(QStringLiteral("Disconnected; RunBay service is not installed"));
        return;
    }

    const QString startType = serviceStartTypeLabel();
    if (output.contains(QStringLiteral("RUNNING"), Qt::CaseInsensitive)) {
        setServiceStatus(QStringLiteral("RunBay service is running (%1); waiting for daemon...").arg(startType));
        QTimer::singleShot(1000, this, &MainWindow::refresh);
        return;
    }

    setServiceStatus(QStringLiteral("Disconnected; RunBay service is not running (%1)").arg(startType));
#else
    setServiceStatus(QStringLiteral("Disconnected"));
#endif
}

void MainWindow::setServiceStatus(const QString &message) {
    m_serviceStatusMessageActive = true;
    m_connectionLabel->setText(message);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::resizeTaskColumnsToViewport(int viewportWidth) {
    if (m_resizingColumns || !m_taskView || viewportWidth <= 0) {
        return;
    }

    constexpr int columnCount = TaskTableModel::ColumnCount;
    int widths[columnCount] = {
        m_taskView->columnWidth(TaskTableModel::NameColumn),
        m_taskView->columnWidth(TaskTableModel::StatusColumn),
        m_taskView->columnWidth(TaskTableModel::PidColumn),
        m_taskView->columnWidth(TaskTableModel::CommandColumn),
    };

    int totalWidth = 0;
    int minimumTotalWidth = 0;
    for (int column = 0; column < columnCount; ++column) {
        widths[column] = qMax(widths[column], kTaskColumnMinWidths[column]);
        totalWidth += widths[column];
        minimumTotalWidth += kTaskColumnMinWidths[column];
    }

    const int targetWidth = qMax(viewportWidth, minimumTotalWidth);
    int newWidths[columnCount] = {};
    int newTotalWidth = 0;
    for (int column = 0; column < columnCount; ++column) {
        newWidths[column] = qMax(kTaskColumnMinWidths[column], qRound(widths[column] * (double(targetWidth) / totalWidth)));
        newTotalWidth += newWidths[column];
    }

    int delta = targetWidth - newTotalWidth;
    while (delta != 0) {
        bool changed = false;
        for (int column = columnCount - 1; column >= 0 && delta != 0; --column) {
            if (delta > 0) {
                ++newWidths[column];
                --delta;
                changed = true;
            } else if (newWidths[column] > kTaskColumnMinWidths[column]) {
                --newWidths[column];
                ++delta;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    m_resizingColumns = true;
    for (int column = 0; column < columnCount; ++column) {
        m_taskView->setColumnWidth(column, newWidths[column]);
    }
    m_resizingColumns = false;
}

void MainWindow::resizeTrailingTaskColumnsToViewport(int resizedColumn) {
    if (m_resizingColumns || !m_taskView || resizedColumn < 0 || resizedColumn >= TaskTableModel::ColumnCount) {
        return;
    }

    constexpr int columnCount = TaskTableModel::ColumnCount;
    const int targetWidth = qMax(m_taskView->viewport()->width(),
                                 kTaskColumnMinWidths[0] + kTaskColumnMinWidths[1] + kTaskColumnMinWidths[2] +
                                     kTaskColumnMinWidths[3]);
    int widths[columnCount] = {
        m_taskView->columnWidth(TaskTableModel::NameColumn),
        m_taskView->columnWidth(TaskTableModel::StatusColumn),
        m_taskView->columnWidth(TaskTableModel::PidColumn),
        m_taskView->columnWidth(TaskTableModel::CommandColumn),
    };
    for (int column = 0; column < columnCount; ++column) {
        widths[column] = qMax(widths[column], kTaskColumnMinWidths[column]);
    }

    int fixedWidth = 0;
    for (int column = 0; column <= resizedColumn; ++column) {
        fixedWidth += widths[column];
    }

    int trailingMinimumWidth = 0;
    int trailingCurrentWidth = 0;
    for (int column = resizedColumn + 1; column < columnCount; ++column) {
        trailingMinimumWidth += kTaskColumnMinWidths[column];
        trailingCurrentWidth += widths[column];
    }

    if (resizedColumn == columnCount - 1) {
        int previousWidth = 0;
        for (int column = 0; column < resizedColumn; ++column) {
            previousWidth += widths[column];
        }
        widths[resizedColumn] = qMax(kTaskColumnMinWidths[resizedColumn], targetWidth - previousWidth);
    } else if (fixedWidth + trailingMinimumWidth > targetWidth) {
        int availableForFixed = targetWidth - trailingMinimumWidth;
        for (int column = resizedColumn; column >= 0 && fixedWidth > availableForFixed; --column) {
            const int shrink = qMin(widths[column] - kTaskColumnMinWidths[column], fixedWidth - availableForFixed);
            widths[column] -= shrink;
            fixedWidth -= shrink;
        }
        for (int column = resizedColumn + 1; column < columnCount; ++column) {
            widths[column] = kTaskColumnMinWidths[column];
        }
    } else {
        int remainingWidth = targetWidth - fixedWidth;
        int trailingTotalWidth = 0;
        for (int column = resizedColumn + 1; column < columnCount; ++column) {
            widths[column] = qMax(kTaskColumnMinWidths[column], qRound(widths[column] * (double(remainingWidth) / trailingCurrentWidth)));
            trailingTotalWidth += widths[column];
        }

        int delta = remainingWidth - trailingTotalWidth;
        while (delta != 0) {
            bool changed = false;
            for (int column = columnCount - 1; column > resizedColumn && delta != 0; --column) {
                if (delta > 0) {
                    ++widths[column];
                    --delta;
                    changed = true;
                } else if (widths[column] > kTaskColumnMinWidths[column]) {
                    --widths[column];
                    ++delta;
                    changed = true;
                }
            }
            if (!changed) {
                break;
            }
        }
    }

    m_resizingColumns = true;
    for (int column = 0; column < columnCount; ++column) {
        m_taskView->setColumnWidth(column, widths[column]);
    }
    m_resizingColumns = false;
}

void MainWindow::applyStyle() {
    setStyleSheet(QStringLiteral(R"(
        QMainWindow {
            background: #f6f7f8;
        }

        QToolBar {
            spacing: 6px;
            padding: 7px 10px;
            border: 0;
            border-bottom: 1px solid #d7dce1;
            background: #fbfbfc;
        }

        QToolButton {
            padding: 5px 9px;
            border: 1px solid transparent;
            border-radius: 4px;
        }

        QToolButton:hover {
            background: #eef2f5;
            border-color: #d7dde3;
        }

        QToolButton:pressed {
            background: #e2e8ee;
        }

        QLineEdit {
            min-height: 28px;
            padding: 3px 9px;
            border: 1px solid #cfd6dd;
            border-radius: 4px;
            background: #ffffff;
        }

        QLineEdit:focus {
            border-color: #5d8cc1;
        }

        QTableView {
            background: #ffffff;
            alternate-background-color: #f8fafb;
            selection-background-color: #dbeafe;
            selection-color: #0f172a;
            border: 0;
        }

        QFrame#LeftPane {
            background: #ffffff;
            border: 0;
        }

        QFrame#RightPane {
            background: #f6f7f8;
            border: 0;
        }

        QSplitter::handle {
            background: #eef1f4;
            border-left: 1px solid #d7dce1;
            border-right: 1px solid #d7dce1;
        }

        QSplitter::handle:hover {
            background: #dbe3ea;
        }

        QHeaderView::section {
            padding: 7px 8px;
            border: 0;
            border-right: 1px solid #d2d8df;
            border-bottom: 1px solid #d7dce1;
            background: #f1f3f5;
            color: #44515f;
            font-weight: 600;
        }

        QHeaderView::section:hover {
            background: #e8edf2;
        }

        QLabel#SummaryLabel {
            color: #52606d;
            font-weight: 600;
            padding: 2px 0 6px 0;
        }

        QLabel#DetailLabel {
            padding: 12px;
            border: 1px solid #d9dee4;
            border-radius: 6px;
            background: #ffffff;
            color: #20262d;
            line-height: 140%;
        }

        QLabel#SectionTitle {
            color: #303841;
            font-weight: 700;
            padding-top: 4px;
        }

        QPlainTextEdit {
            padding: 10px;
            border: 1px solid #202a34;
            border-radius: 6px;
            background: #111820;
            color: #d8dee9;
            selection-background-color: #315a7d;
        }

        QStatusBar {
            background: #fbfbfc;
            border-top: 1px solid #d7dce1;
            color: #52606d;
        }
    )"));
}
