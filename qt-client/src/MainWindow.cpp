#include "MainWindow.h"

#include "AppUtils.h"
#include "ServiceManagerWindow.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontDatabase>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
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
const QSize kDefaultWindowSize(1180, 760);

QString settingsFilePath() {
    return AppUtils::appDataFilePath(QStringLiteral("settings.yml"));
}

QString serverConfigFilePath() {
    return AppUtils::appDataFilePath(QStringLiteral("servers.yml"));
}

QString normalizedServerUrl(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) {
        return {};
    }
    if (!value.contains(QStringLiteral("://"))) {
        value.prepend(QStringLiteral("http://"));
    }

    QUrl url(value);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        return {};
    }
    if (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) {
        return {};
    }

    url.setPath(QString());
    QString text = url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::StripTrailingSlash);
    while (text.endsWith(QLatin1Char('/'))) {
        text.chop(1);
    }
    return text;
}

QString serverTabTitle(const QString &serverUrl) {
    const QUrl url(serverUrl);
    if (!url.isValid() || url.host().isEmpty()) {
        return serverUrl;
    }

    QString title = url.host();
    if (url.port() > 0) {
        title += QStringLiteral(":%1").arg(url.port());
    }
    if (!url.path().isEmpty() && url.path() != QStringLiteral("/")) {
        title += url.path();
    }
    return title;
}

QString defaultServerName(const QString &serverUrl) {
    return serverTabTitle(serverUrl);
}

bool isLocalServerUrl(const QString &serverUrl) {
    const QString host = QUrl(serverUrl).host().toLower();
    return host == QStringLiteral("127.0.0.1") || host == QStringLiteral("localhost") || host == QStringLiteral("::1");
}

QString logClearsFilePath() {
    return AppUtils::appDataFilePath(QStringLiteral("log-clears.ini"));
}

QSettings logClearsSettings() {
    return QSettings(logClearsFilePath(), QSettings::IniFormat);
}

QString logClearPointKey(const QString &instanceId, const QString &taskId) {
    return QStringLiteral("%1/%2").arg(instanceId, taskId);
}

QJsonObject importFailure(const QString &name, const QString &reason) {
    QJsonObject failure;
    failure.insert(QStringLiteral("name"), name.trimmed().isEmpty() ? QStringLiteral("(unnamed)") : name.trimmed());
    failure.insert(QStringLiteral("reason"), reason);
    return failure;
}

QString importFailureMessage(const QJsonArray &failures) {
    QStringList lines;
    for (const QJsonValue &value : failures) {
        const QJsonObject failure = value.toObject();
        const QString name = failure.value(QStringLiteral("name")).toString(QStringLiteral("(unnamed)"));
        const QString reason = failure.value(QStringLiteral("reason")).toString(QStringLiteral("Unknown error"));
        lines.append(QStringLiteral("%1: %2").arg(name, reason));
    }
    return lines.join(QLatin1Char('\n'));
}

QIcon logJumpIcon(bool top) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(QStringLiteral("#52616f")), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);

    if (top) {
        painter.drawLine(QPointF(7, 6), QPointF(17, 6));
        painter.drawLine(QPointF(12, 18), QPointF(12, 10));
        painter.drawPolyline(QPolygonF({QPointF(8, 13), QPointF(12, 9), QPointF(16, 13)}));
    } else {
        painter.drawLine(QPointF(7, 18), QPointF(17, 18));
        painter.drawLine(QPointF(12, 6), QPointF(12, 14));
        painter.drawPolyline(QPolygonF({QPointF(8, 11), QPointF(12, 15), QPointF(16, 11)}));
    }

    return QIcon(pixmap);
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
    loadServerSettings();
    updateServerTabs();

    connect(&m_api, &ApiClient::tasksLoaded, this, &MainWindow::onTasksLoaded);
    connect(&m_api, &ApiClient::logsLoaded, this, &MainWindow::onLogsLoaded);
    connect(&m_api, &ApiClient::tasksExported, this, [this](int context, const QJsonArray &tasks) {
        if (context != m_serverRequestContext) {
            return;
        }
        const QString defaultPath =
            AppUtils::appDataFilePath(QStringLiteral("runbay-tasks-export.yml"));
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Tasks"), defaultPath,
                                                          QStringLiteral("YAML files (*.yml *.yaml);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }

        YAML::Node root;
        YAML::Node taskNodes(YAML::NodeType::Sequence);
        for (const QJsonValue &value : tasks) {
            const QJsonObject task = value.toObject();
            YAML::Node taskNode;
            taskNode[QStringLiteral("name").toStdString()] = task.value(QStringLiteral("name")).toString().toStdString();
            taskNode[QStringLiteral("command").toStdString()] = task.value(QStringLiteral("command")).toString().toStdString();
            taskNode[QStringLiteral("cwd").toStdString()] = task.value(QStringLiteral("cwd")).toString().toStdString();
            taskNode[QStringLiteral("start_on_launch").toStdString()] =
                task.value(QStringLiteral("start_on_launch")).toBool();
            taskNodes.push_back(taskNode);
        }
        root[QStringLiteral("tasks").toStdString()] = taskNodes;

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, QStringLiteral("Export Tasks"),
                                 QStringLiteral("Could not write %1").arg(QDir::toNativeSeparators(path)));
            return;
        }
        YAML::Emitter emitter;
        emitter.SetIndent(2);
        emitter << root;
        if (!emitter.good()) {
            QMessageBox::warning(this, QStringLiteral("Export Tasks"), QStringLiteral("Could not serialize tasks."));
            return;
        }
        file.write(emitter.c_str());
        file.write("\n");
        file.close();
        statusBar()->showMessage(QStringLiteral("Exported %1 tasks").arg(tasks.size()), 5000);
    });
    connect(&m_api, &ApiClient::tasksImported, this, [this](int context, int created, const QJsonArray &failures) {
        if (context != m_serverRequestContext) {
            return;
        }
        QJsonArray allFailures = m_pendingImportFailures;
        for (const QJsonValue &failure : failures) {
            allFailures.append(failure);
        }
        m_pendingImportFailures = QJsonArray();

        const QString message = allFailures.isEmpty()
                                    ? QStringLiteral("Imported %1 tasks").arg(created)
                                    : QStringLiteral("Imported %1 tasks; %2 failed").arg(created).arg(allFailures.size());
        statusBar()->showMessage(message, 5000);
        if (allFailures.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Import Tasks"), message);
        } else {
            QMessageBox::warning(this, QStringLiteral("Import Tasks"),
                                 QStringLiteral("%1\n\nFailed imports:\n%2").arg(message, importFailureMessage(allFailures)));
        }
    });
    connect(&m_api, &ApiClient::errorOccurred, this, [this](int context, const QString &message) {
        if (context != m_serverRequestContext) {
            return;
        }
        statusBar()->showMessage(message, 5000);
    });
    connect(&m_api, &ApiClient::taskRequestFailed, this, [this](int context, const QString &message) {
        if (context != m_serverRequestContext) {
            return;
        }
        QMessageBox::warning(this, QStringLiteral("RunBay"), message);
    });
    connect(&m_api, &ApiClient::healthChanged, this, [this](int context, bool ok, const QString &instanceId) {
        if (context != m_serverRequestContext) {
            return;
        }
        setDaemonConnected(ok);
        if (ok) {
            if (!instanceId.isEmpty() && instanceId != m_serverInstanceId) {
                m_serverInstanceId = instanceId;
                m_loadedLogTaskId.clear();
                m_lastLogEntryId = 0;
                m_logFormat = QTextCharFormat();
                if (m_logView) {
                    m_logView->clear();
                }
            }
            m_serviceStartAttempted = false;
            m_serviceStatusMessageActive = false;
            m_connectionLabel->setText(QStringLiteral("Connected: %1").arg(currentServerUrl()));
        } else if (!m_serviceStartAttempted) {
            m_serverInstanceId.clear();
            if (isLocalServerUrl(currentServerUrl())) {
                ensureDaemonServiceStarted();
            } else {
                setServiceStatus(QStringLiteral("Disconnected: %1").arg(currentServerUrl()));
            }
        } else if (!m_serviceStatusMessageActive) {
            m_serverInstanceId.clear();
            m_connectionLabel->setText(QStringLiteral("Disconnected: %1").arg(currentServerUrl()));
        }
    });

    connect(&m_refreshTimer, &QTimer::timeout, this, &MainWindow::refresh);
    connect(&m_logTimer, &QTimer::timeout, this, [this]() {
        if (!m_daemonConnected) {
            return;
        }
        const QString id = selectedTaskId();
        if (!id.isEmpty()) {
            m_api.fetchLogs(id, nextLogRequestAfter(id));
        }
    });

    m_refreshTimer.start(2500);
    m_logTimer.start(1200);
    if (m_serverUrls.isEmpty()) {
        m_currentServerIndex = -1;
        updateServerEmptyState();
        if (m_connectionLabel) {
            m_connectionLabel->setText(QStringLiteral("No server configured"));
        }
    } else {
        setCurrentServer(qBound(0, m_currentServerIndex, m_serverUrls.size() - 1));
        refresh();
    }
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("RunBay"));
    resize(kDefaultWindowSize);
    setMinimumSize(920, 560);
    applyStyle();

    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    QAction *openDataDirectoryAction = fileMenu->addAction(QStringLiteral("Open Data Directory"));
    connect(openDataDirectoryAction, &QAction::triggered, this, &MainWindow::openDataDirectory);

    QMenu *taskMenu = menuBar()->addMenu(QStringLiteral("Task"));
    QAction *addTaskAction = taskMenu->addAction(QStringLiteral("Add Task"));
    taskMenu->addSeparator();
    QAction *importTasksAction = taskMenu->addAction(QStringLiteral("Import Tasks..."));
    QAction *exportTasksAction = taskMenu->addAction(QStringLiteral("Export Tasks..."));
    connect(addTaskAction, &QAction::triggered, this, &MainWindow::addTask);
    connect(importTasksAction, &QAction::triggered, this, &MainWindow::importTasks);
    connect(exportTasksAction, &QAction::triggered, this, &MainWindow::exportTasks);

    QMenu *serverMenu = menuBar()->addMenu(QStringLiteral("Server"));
    QAction *addServerAction = serverMenu->addAction(QStringLiteral("Add..."));
    QAction *deleteServerAction = serverMenu->addAction(QStringLiteral("Delete"));
    connect(addServerAction, &QAction::triggered, this, &MainWindow::addServer);
    connect(deleteServerAction, &QAction::triggered, this, &MainWindow::deleteCurrentServer);

    QAction *serviceAction = menuBar()->addAction(QStringLiteral("Service"));
    connect(serviceAction, &QAction::triggered, this, &MainWindow::openServiceManager);

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

    m_serverTabs = new QTabBar(this);
    m_serverTabs->setDrawBase(false);
    m_serverTabs->setExpanding(false);
    m_serverTabs->setMovable(false);
    m_serverTabs->setElideMode(Qt::ElideRight);
    connect(m_serverTabs, &QTabBar::currentChanged, this, &MainWindow::onServerTabChanged);

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

    QToolButton *scrollLogTopButton = new QToolButton(this);
    scrollLogTopButton->setIcon(logJumpIcon(true));
    scrollLogTopButton->setToolTip(QStringLiteral("Go to top"));
    scrollLogTopButton->setAutoRaise(true);
    connect(scrollLogTopButton, &QToolButton::clicked, this, &MainWindow::scrollLogToTop);

    QToolButton *scrollLogBottomButton = new QToolButton(this);
    scrollLogBottomButton->setIcon(logJumpIcon(false));
    scrollLogBottomButton->setToolTip(QStringLiteral("Go to bottom"));
    scrollLogBottomButton->setAutoRaise(true);
    connect(scrollLogBottomButton, &QToolButton::clicked, this, &MainWindow::scrollLogToBottom);

    QFrame *logToolbar = new QFrame(this);
    logToolbar->setFrameShape(QFrame::NoFrame);
    QHBoxLayout *logToolbarLayout = new QHBoxLayout(logToolbar);
    logToolbarLayout->setContentsMargins(0, 0, 0, 0);
    logToolbarLayout->setSpacing(6);
    logToolbarLayout->addWidget(m_logTitleLabel);
    logToolbarLayout->addStretch(1);
    logToolbarLayout->addWidget(scrollLogTopButton);
    logToolbarLayout->addWidget(scrollLogBottomButton);
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

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(8);
    m_splitter->setOpaqueResize(true);
    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(rightPane);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({kTaskListMinWidth, 850});
    connect(m_splitter, &QSplitter::splitterMoved, this, [this]() {
        const int viewportWidth = m_taskView->viewport()->width();
        if (viewportWidth > 0 && viewportWidth != m_lastTableViewportWidth) {
            resizeTaskColumnsToViewport(viewportWidth);
            m_lastTableViewportWidth = viewportWidth;
        }
        saveUiState();
    });

    QWidget *emptyServerPage = new QWidget(this);
    QVBoxLayout *emptyServerLayout = new QVBoxLayout(emptyServerPage);
    emptyServerLayout->setContentsMargins(0, 0, 0, 0);
    emptyServerLayout->addStretch(1);
    m_emptyServerButton = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogNewFolder),
                                          QStringLiteral("Add Server"), emptyServerPage);
    m_emptyServerButton->setMinimumSize(180, 42);
    connect(m_emptyServerButton, &QPushButton::clicked, this, &MainWindow::addServer);
    QHBoxLayout *emptyServerButtonLayout = new QHBoxLayout;
    emptyServerButtonLayout->addStretch(1);
    emptyServerButtonLayout->addWidget(m_emptyServerButton);
    emptyServerButtonLayout->addStretch(1);
    emptyServerLayout->addLayout(emptyServerButtonLayout);
    emptyServerLayout->addStretch(1);

    m_contentStack = new QStackedWidget(this);
    m_contentStack->addWidget(emptyServerPage);
    m_contentStack->addWidget(m_splitter);

    QWidget *central = new QWidget(this);
    QVBoxLayout *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_serverTabs);
    centralLayout->addWidget(m_contentStack, 1);
    setCentralWidget(central);
    restoreUiState();

    m_connectionLabel = new QLabel(QStringLiteral("Checking daemon..."), this);
    statusBar()->addPermanentWidget(m_connectionLabel);

    updateActions();
}

void MainWindow::refresh() {
    if (m_currentServerIndex < 0 || m_currentServerIndex >= m_serverUrls.size()) {
        return;
    }
    m_api.health();
    m_api.listTasks();
}

void MainWindow::loadServerSettings() {
    QStringList normalizedNames;
    QStringList normalizedUrls;
    int savedCurrentIndex = 0;

    const YAML::Node root = AppUtils::loadYamlFile(serverConfigFilePath());
    if (root[QStringLiteral("currentIndex").toStdString()] &&
        root[QStringLiteral("currentIndex").toStdString()].IsScalar()) {
        savedCurrentIndex = root[QStringLiteral("currentIndex").toStdString()].as<int>();
    }

    const YAML::Node servers = root[QStringLiteral("servers").toStdString()];
    if (servers && servers.IsSequence()) {
        for (const YAML::Node &server : servers) {
            if (!server.IsMap()) {
                continue;
            }

            QString nameValue;
            QString urlValue;
            const YAML::Node nameNode = server[QStringLiteral("name").toStdString()];
            const YAML::Node urlNode = server[QStringLiteral("url").toStdString()];
            if (nameNode && nameNode.IsScalar()) {
                nameValue = QString::fromStdString(nameNode.as<std::string>()).trimmed();
            }
            if (urlNode && urlNode.IsScalar()) {
                urlValue = QString::fromStdString(urlNode.as<std::string>());
            }

            const QString normalized = normalizedServerUrl(urlValue);
            if (!normalized.isEmpty() && !normalizedUrls.contains(normalized)) {
                normalizedNames.append(nameValue.isEmpty() ? defaultServerName(normalized) : nameValue);
                normalizedUrls.append(normalized);
            }
        }
    }

    m_serverNames = normalizedNames;
    m_serverUrls = normalizedUrls;
    m_currentServerIndex = m_serverUrls.isEmpty() ? -1 : qBound(0, savedCurrentIndex, m_serverUrls.size() - 1);
}

void MainWindow::saveServerSettings() const {
    YAML::Node root;
    root[QStringLiteral("currentIndex").toStdString()] = m_currentServerIndex;
    YAML::Node servers(YAML::NodeType::Sequence);
    for (int i = 0; i < m_serverUrls.size(); ++i) {
        const QString name = i < m_serverNames.size() ? m_serverNames.at(i) : defaultServerName(m_serverUrls.at(i));
        YAML::Node server;
        server[QStringLiteral("name").toStdString()] = name.toStdString();
        server[QStringLiteral("url").toStdString()] = m_serverUrls.at(i).toStdString();
        servers.push_back(server);
    }
    root[QStringLiteral("servers").toStdString()] = servers;

    AppUtils::saveYamlFile(serverConfigFilePath(), root);
}

void MainWindow::updateServerTabs() {
    if (!m_serverTabs) {
        return;
    }

    const QSignalBlocker blocker(m_serverTabs);
    while (m_serverTabs->count() > 0) {
        m_serverTabs->removeTab(0);
    }
    for (int i = 0; i < m_serverUrls.size(); ++i) {
        const QString name = i < m_serverNames.size() ? m_serverNames.at(i) : defaultServerName(m_serverUrls.at(i));
        const int tabIndex = m_serverTabs->addTab(name);
        m_serverTabs->setTabData(tabIndex, m_serverUrls.at(i));
        m_serverTabs->setTabToolTip(tabIndex, m_serverUrls.at(i));
    }
    m_serverTabs->setCurrentIndex(m_currentServerIndex);
    updateServerEmptyState();
}

QString MainWindow::currentServerUrl() const {
    if (m_currentServerIndex < 0 || m_currentServerIndex >= m_serverUrls.size()) {
        return {};
    }
    return m_serverUrls.at(m_currentServerIndex);
}

QString MainWindow::currentServerName() const {
    if (m_currentServerIndex < 0 || m_currentServerIndex >= m_serverNames.size()) {
        return {};
    }
    return m_serverNames.at(m_currentServerIndex);
}

void MainWindow::updateServerEmptyState() {
    const bool hasServers = !m_serverUrls.isEmpty();
    if (m_serverTabs) {
        m_serverTabs->setVisible(hasServers);
    }
    if (m_contentStack) {
        m_contentStack->setCurrentIndex(hasServers ? 1 : 0);
    }
    if (m_searchEdit) {
        m_searchEdit->setEnabled(hasServers);
    }
    if (!hasServers) {
        m_daemonConnected = false;
        m_currentServerIndex = -1;
        m_serverInstanceId.clear();
        m_loadedLogTaskId.clear();
        m_lastLogEntryId = 0;
        m_logFormat = QTextCharFormat();
        m_api.cancelPendingRequests();
        m_taskModel.setTasks(QList<Task>());
        m_taskModel.setDisconnected(true);
        if (m_logView) {
            m_logView->clear();
        }
        if (m_detailLabel) {
            m_detailLabel->setText(QStringLiteral("No task selected"));
        }
        if (m_summaryLabel) {
            m_summaryLabel->setText(QStringLiteral("0 tasks"));
        }
        updateActions();
    }
}

void MainWindow::setCurrentServer(int index) {
    if (m_serverUrls.isEmpty()) {
        m_currentServerIndex = -1;
        ++m_serverRequestContext;
        m_api.setContext(m_serverRequestContext);
        updateServerEmptyState();
        return;
    }

    index = qBound(0, index, m_serverUrls.size() - 1);
    m_currentServerIndex = index;
    ++m_serverRequestContext;
    m_api.setContext(m_serverRequestContext);
    m_api.cancelPendingRequests();
    m_api.setBaseUrl(QUrl(m_serverUrls.at(index)));
    m_serverInstanceId.clear();
    m_loadedLogTaskId.clear();
    m_lastLogEntryId = 0;
    m_logFormat = QTextCharFormat();
    m_daemonConnected = false;
    m_serviceStartAttempted = false;
    m_serviceStatusMessageActive = false;
    m_taskModel.setTasks(QList<Task>());
    m_taskModel.setDisconnected(true);
    if (m_taskView) {
        m_taskView->clearSelection();
    }
    if (m_logView) {
        m_logView->clear();
    }
    if (m_detailLabel) {
        m_detailLabel->setText(QStringLiteral("No task selected"));
    }
    if (m_summaryLabel) {
        m_summaryLabel->setText(QStringLiteral("0 tasks"));
    }
    if (m_connectionLabel) {
        m_connectionLabel->setText(QStringLiteral("Connecting: %1").arg(currentServerUrl()));
    }
    if (m_serverTabs && m_serverTabs->currentIndex() != index) {
        const QSignalBlocker blocker(m_serverTabs);
        m_serverTabs->setCurrentIndex(index);
    }
    saveServerSettings();
    updateServerEmptyState();
    updateActions();
    refresh();
}

void MainWindow::addServer() {
    addServerWithDefaults(QStringLiteral("Local daemon"), QStringLiteral("http://127.0.0.1:8732"));
}

void MainWindow::addServerWithDefaults(const QString &defaultName, const QString &defaultUrl) {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add Server"));
    dialog.setMinimumWidth(460);

    QLineEdit nameEdit;
    QLineEdit urlEdit;
    nameEdit.setMinimumWidth(320);
    urlEdit.setMinimumWidth(320);
    nameEdit.setPlaceholderText(QStringLiteral("Local daemon"));
    urlEdit.setPlaceholderText(QStringLiteral("http://127.0.0.1:8732"));
    nameEdit.setText(defaultName);
    urlEdit.setText(defaultUrl);

    QFormLayout form(&dialog);
    form.addRow(QStringLiteral("Name"), &nameEdit);
    form.addRow(QStringLiteral("URL"), &urlEdit);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString name = nameEdit.text().trimmed();
    const QString url = normalizedServerUrl(urlEdit.text());
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Add Server"), QStringLiteral("Name is required."));
        return;
    }
    if (url.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Add Server"), QStringLiteral("Enter a valid http or https URL."));
        return;
    }

    for (const QString &existingName : m_serverNames) {
        if (existingName.compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, QStringLiteral("Add Server"),
                                 QStringLiteral("Server name already exists: %1").arg(name));
            return;
        }
    }

    const int existingIndex = m_serverUrls.indexOf(url);
    if (existingIndex >= 0) {
        QMessageBox::warning(this, QStringLiteral("Add Server"),
                             QStringLiteral("Server URL already exists: %1").arg(url));
        setCurrentServer(existingIndex);
        return;
    }

    m_serverNames.append(name);
    m_serverUrls.append(url);
    m_currentServerIndex = m_serverUrls.size() - 1;
    updateServerTabs();
    setCurrentServer(m_currentServerIndex);
}

void MainWindow::deleteCurrentServer() {
    if (m_currentServerIndex < 0 || m_currentServerIndex >= m_serverUrls.size()) {
        QMessageBox::information(this, QStringLiteral("Delete Server"), QStringLiteral("No server is selected."));
        return;
    }

    const QString name = currentServerName();
    const QString url = currentServerUrl();
    if (QMessageBox::question(this, QStringLiteral("Delete Server"),
                              QStringLiteral("Delete server %1 (%2)?").arg(name, url)) != QMessageBox::Yes) {
        return;
    }

    if (m_currentServerIndex < m_serverNames.size()) {
        m_serverNames.removeAt(m_currentServerIndex);
    }
    m_serverUrls.removeAt(m_currentServerIndex);
    m_currentServerIndex = m_serverUrls.isEmpty() ? -1 : qMin(m_currentServerIndex, m_serverUrls.size() - 1);

    if (m_currentServerIndex < 0) {
        ++m_serverRequestContext;
        m_api.setContext(m_serverRequestContext);
        m_api.cancelPendingRequests();
        saveServerSettings();
        updateServerTabs();
        updateServerEmptyState();
        if (m_connectionLabel) {
            m_connectionLabel->setText(QStringLiteral("No server configured"));
        }
        return;
    }

    updateServerTabs();
    setCurrentServer(m_currentServerIndex);
}

void MainWindow::onServerTabChanged(int index) {
    if (index < 0 || index == m_currentServerIndex) {
        return;
    }
    setCurrentServer(index);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveUiState();
    QMainWindow::closeEvent(event);
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
            saveUiState();
        }
        m_lastTableViewportWidth = viewportWidth;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (!event->oldSize().isValid() || event->size() == event->oldSize()) {
        return;
    }
    saveUiState();
}

void MainWindow::addTask() {
    if (currentServerUrl().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Add a server before creating tasks."));
        return;
    }

    QString name;
    QString command;
    QString cwd;
    bool startOnLaunch = false;
    if (!taskEditorDialog(QStringLiteral("Add Task"), nullptr, &name, &command, &cwd, &startOnLaunch)) {
        return;
    }
    m_api.createTask(name, command, cwd, startOnLaunch);
}

void MainWindow::importTasks() {
    m_pendingImportFailures = QJsonArray();

    if (currentServerUrl().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Add a server before importing tasks."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import Tasks"), AppUtils::appDataDirectoryPath(),
                                                      QStringLiteral("YAML files (*.yml *.yaml);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Import Tasks"),
                             QStringLiteral("Could not read %1").arg(QDir::toNativeSeparators(path)));
        return;
    }

    YAML::Node root;
    try {
        root = YAML::Load(file.readAll().toStdString());
    } catch (const YAML::Exception &err) {
        QMessageBox::warning(this, QStringLiteral("Import Tasks"),
                             QStringLiteral("Invalid YAML: %1").arg(QString::fromStdString(err.msg)));
        return;
    }

    QStringList knownNames;
    for (int row = 0; row < m_taskModel.rowCount(); ++row) {
        const QString taskName = m_taskModel.taskAt(row).name.trimmed();
        if (!taskName.isEmpty()) {
            knownNames.append(taskName.toCaseFolded());
        }
    }

    QJsonArray tasks;
    QJsonArray failures;
    const YAML::Node taskNodes = root[QStringLiteral("tasks").toStdString()];
    if (taskNodes && taskNodes.IsSequence()) {
        for (const YAML::Node &taskNode : taskNodes) {
            if (!taskNode.IsMap()) {
                failures.append(importFailure(QString(), QStringLiteral("Task entry must be a map.")));
                continue;
            }

            const YAML::Node nameNode = taskNode[QStringLiteral("name").toStdString()];
            const YAML::Node commandNode = taskNode[QStringLiteral("command").toStdString()];
            const YAML::Node cwdNode = taskNode[QStringLiteral("cwd").toStdString()];
            const YAML::Node startOnLaunchNode = taskNode[QStringLiteral("start_on_launch").toStdString()];

            QString name;
            QString command;
            QString cwd;
            bool startOnLaunch = false;
            if (nameNode && nameNode.IsScalar()) {
                name = QString::fromStdString(nameNode.as<std::string>()).trimmed();
            }
            if (commandNode && commandNode.IsScalar()) {
                command = QString::fromStdString(commandNode.as<std::string>()).trimmed();
            }
            if (cwdNode && cwdNode.IsScalar()) {
                cwd = QString::fromStdString(cwdNode.as<std::string>()).trimmed();
            }
            if (startOnLaunchNode && startOnLaunchNode.IsScalar()) {
                startOnLaunch = startOnLaunchNode.as<bool>(false);
            }

            if (name.isEmpty() || command.isEmpty()) {
                failures.append(importFailure(name, QStringLiteral("Name and command are required.")));
                continue;
            }

            const QString foldedName = name.toCaseFolded();
            if (knownNames.contains(foldedName)) {
                failures.append(importFailure(name, QStringLiteral("A task with this name already exists.")));
                continue;
            }

            knownNames.append(foldedName);
            QJsonObject task;
            task.insert(QStringLiteral("name"), name);
            task.insert(QStringLiteral("command"), command);
            task.insert(QStringLiteral("cwd"), cwd);
            task.insert(QStringLiteral("start_on_launch"), startOnLaunch);
            tasks.append(task);
        }
    }

    if (tasks.isEmpty()) {
        if (failures.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Import Tasks"), QStringLiteral("No tasks found in this YAML file."));
        } else {
            QMessageBox::warning(this, QStringLiteral("Import Tasks"),
                                 QStringLiteral("No tasks were imported.\n\nFailed imports:\n%1").arg(importFailureMessage(failures)));
        }
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("Import Tasks"),
                              failures.isEmpty()
                                  ? QStringLiteral("Import %1 tasks into %2?").arg(tasks.size()).arg(currentServerUrl())
                                  : QStringLiteral("Import %1 tasks into %2? %3 task(s) will be skipped.")
                                        .arg(tasks.size())
                                        .arg(currentServerUrl())
                                        .arg(failures.size())) !=
        QMessageBox::Yes) {
        return;
    }

    m_pendingImportFailures = failures;
    m_api.importTasks(tasks);
}

void MainWindow::exportTasks() {
    if (currentServerUrl().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Add a server before exporting tasks."));
        return;
    }

    m_api.exportTasks();
}

void MainWindow::addTaskFromRunnable(const QString &runnablePath) {
    if (currentServerUrl().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Add a server before creating tasks."));
        return;
    }

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
    saveUiState();
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
    const QString id = selectedTaskId();
    if (!id.isEmpty() && !m_serverInstanceId.isEmpty() && m_lastLogEntryId > 0) {
        saveClearedLogEntryId(id, m_lastLogEntryId);
    }
    m_logView->clear();
    m_logFormat = QTextCharFormat();
}

void MainWindow::scrollLogToTop() {
    if (!m_logView) {
        return;
    }
    m_logView->moveCursor(QTextCursor::Start);
    m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->minimum());
    m_logView->setFocus();
}

void MainWindow::scrollLogToBottom() {
    if (!m_logView) {
        return;
    }
    m_logView->moveCursor(QTextCursor::End);
    m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
    m_logView->setFocus();
}

void MainWindow::openDataDirectory() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppUtils::appDataDirectoryPath()));
}

void MainWindow::openServiceManager() {
    if (m_serviceManagerWindow) {
        m_serviceManagerWindow->show();
        m_serviceManagerWindow->raise();
        m_serviceManagerWindow->activateWindow();
        return;
    }

    m_serviceManagerWindow = new ServiceManagerWindow;
    m_serviceManagerWindow->setAttribute(Qt::WA_DeleteOnClose);
    connect(this, &QObject::destroyed, m_serviceManagerWindow, &QWidget::close);
    connect(m_serviceManagerWindow, &QObject::destroyed, this, [this]() {
        m_serviceManagerWindow = nullptr;
    });
    connect(m_serviceManagerWindow, &ServiceManagerWindow::serviceActionRequested, this, [this](const QString &message) {
        setServiceStatus(message);
        QTimer::singleShot(3000, this, [this]() {
            m_serviceStartAttempted = false;
            refresh();
        });
    });
    connect(m_serviceManagerWindow, &ServiceManagerWindow::addServerRequested, this, [this](const QString &name, const QString &url) {
        if (m_serviceManagerWindow) {
            m_serviceManagerWindow->close();
        }
        QTimer::singleShot(0, this, [this, name, url]() {
            addServerWithDefaults(name, url);
        });
    });
    m_serviceManagerWindow->show();
    m_serviceManagerWindow->raise();
    m_serviceManagerWindow->activateWindow();
}

void MainWindow::onSelectionChanged() {
    updateActions();
    const QString id = selectedTaskId();
    if (!id.isEmpty()) {
        if (id != m_loadedLogTaskId) {
            m_loadedLogTaskId = id;
            m_lastLogEntryId = clearedLogEntryId(id);
            m_logFormat = QTextCharFormat();
            m_logView->clear();
        }
        m_api.fetchLogs(id, nextLogRequestAfter(id));
    } else {
        m_loadedLogTaskId.clear();
        m_lastLogEntryId = 0;
        m_logFormat = QTextCharFormat();
        m_logView->clear();
        m_detailLabel->setText(QStringLiteral("No task selected"));
    }
}

void MainWindow::onTasksLoaded(int context, const QList<Task> &tasks) {
    if (context != m_serverRequestContext) {
        return;
    }
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

void MainWindow::onLogsLoaded(int context, const QString &taskId, const QString &instanceId, const QList<LogEntry> &entries, quint64 startId, quint64 endId, bool truncated) {
    if (context != m_serverRequestContext) {
        return;
    }
    if (taskId != selectedTaskId()) {
        return;
    }

    const bool instanceChanged = !instanceId.isEmpty() && instanceId != m_serverInstanceId;
    if (instanceChanged) {
        m_serverInstanceId = instanceId;
        m_loadedLogTaskId.clear();
        m_lastLogEntryId = clearedLogEntryId(taskId);
    }

    const bool resetLog = instanceChanged || taskId != m_loadedLogTaskId || truncated;
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

void MainWindow::restoreUiState() {
    const YAML::Node settings = AppUtils::loadYamlFile(settingsFilePath());

    const YAML::Node windowNode = settings[QStringLiteral("window").toStdString()];
    const int savedWindowWidth = windowNode && windowNode[QStringLiteral("width").toStdString()]
                                     ? windowNode[QStringLiteral("width").toStdString()].as<int>(0)
                                     : 0;
    const int savedWindowHeight = windowNode && windowNode[QStringLiteral("height").toStdString()]
                                      ? windowNode[QStringLiteral("height").toStdString()].as<int>(0)
                                      : 0;
    if (savedWindowWidth > 0 && savedWindowHeight > 0) {
        resize(QSize(savedWindowWidth, savedWindowHeight).expandedTo(minimumSize()));
    }

    if (m_splitter) {
        const YAML::Node splitterNode = settings[QStringLiteral("splitter").toStdString()];
        const int leftWidth = splitterNode && splitterNode[QStringLiteral("leftWidth").toStdString()]
                                  ? splitterNode[QStringLiteral("leftWidth").toStdString()].as<int>(0)
                                  : 0;
        const int rightWidth = splitterNode && splitterNode[QStringLiteral("rightWidth").toStdString()]
                                   ? splitterNode[QStringLiteral("rightWidth").toStdString()].as<int>(0)
                                   : 0;
        if (leftWidth >= kTaskListMinWidth && rightWidth > 0) {
            m_splitter->setSizes({leftWidth, rightWidth});
        }
    }

    if (!m_taskView) {
        return;
    }

    const int savedColumnWidths[TaskTableModel::ColumnCount] = {
        settings[QStringLiteral("taskList").toStdString()] &&
                settings[QStringLiteral("taskList").toStdString()][QStringLiteral("nameWidth").toStdString()]
            ? settings[QStringLiteral("taskList").toStdString()][QStringLiteral("nameWidth").toStdString()].as<int>(0)
            : 0,
        settings[QStringLiteral("taskList").toStdString()] &&
                settings[QStringLiteral("taskList").toStdString()][QStringLiteral("statusWidth").toStdString()]
            ? settings[QStringLiteral("taskList").toStdString()][QStringLiteral("statusWidth").toStdString()].as<int>(0)
            : 0,
        settings[QStringLiteral("taskList").toStdString()] &&
                settings[QStringLiteral("taskList").toStdString()][QStringLiteral("pidWidth").toStdString()]
            ? settings[QStringLiteral("taskList").toStdString()][QStringLiteral("pidWidth").toStdString()].as<int>(0)
            : 0,
        settings[QStringLiteral("taskList").toStdString()] &&
                settings[QStringLiteral("taskList").toStdString()][QStringLiteral("commandWidth").toStdString()]
            ? settings[QStringLiteral("taskList").toStdString()][QStringLiteral("commandWidth").toStdString()].as<int>(0)
            : 0,
    };
    bool hasSavedColumnWidths = true;
    for (int column = 0; column < TaskTableModel::ColumnCount; ++column) {
        if (savedColumnWidths[column] <= 0) {
            hasSavedColumnWidths = false;
            break;
        }
    }
    if (hasSavedColumnWidths) {
        m_resizingColumns = true;
        for (int column = 0; column < TaskTableModel::ColumnCount; ++column) {
            m_taskView->setColumnWidth(column, qMax(kTaskColumnMinWidths[column], savedColumnWidths[column]));
        }
        m_resizingColumns = false;
        m_columnsSizedToContents = true;
    }

    m_lastTableViewportWidth = m_taskView->viewport()->width();
}

void MainWindow::saveUiState() const {
    YAML::Node settings;
    settings[QStringLiteral("window").toStdString()][QStringLiteral("width").toStdString()] = width();
    settings[QStringLiteral("window").toStdString()][QStringLiteral("height").toStdString()] = height();

    if (m_splitter) {
        const QList<int> splitterSizes = m_splitter->sizes();
        if (splitterSizes.size() >= 2) {
            settings[QStringLiteral("splitter").toStdString()][QStringLiteral("leftWidth").toStdString()] = splitterSizes.at(0);
            settings[QStringLiteral("splitter").toStdString()][QStringLiteral("rightWidth").toStdString()] = splitterSizes.at(1);
        }
    }

    if (m_taskView) {
        settings[QStringLiteral("taskList").toStdString()][QStringLiteral("nameWidth").toStdString()] =
            m_taskView->columnWidth(TaskTableModel::NameColumn);
        settings[QStringLiteral("taskList").toStdString()][QStringLiteral("statusWidth").toStdString()] =
            m_taskView->columnWidth(TaskTableModel::StatusColumn);
        settings[QStringLiteral("taskList").toStdString()][QStringLiteral("pidWidth").toStdString()] =
            m_taskView->columnWidth(TaskTableModel::PidColumn);
        settings[QStringLiteral("taskList").toStdString()][QStringLiteral("commandWidth").toStdString()] =
            m_taskView->columnWidth(TaskTableModel::CommandColumn);
    }

    AppUtils::saveYamlFile(settingsFilePath(), settings);
}

quint64 MainWindow::clearedLogEntryId(const QString &taskId) const {
    if (m_serverInstanceId.isEmpty() || taskId.isEmpty()) {
        return 0;
    }
    QSettings settings = logClearsSettings();
    settings.beginGroup(QStringLiteral("clearPoints"));
    const quint64 entryId = settings.value(logClearPointKey(m_serverInstanceId, taskId), 0).toULongLong();
    settings.endGroup();
    return entryId;
}

void MainWindow::saveClearedLogEntryId(const QString &taskId, quint64 entryId) const {
    if (m_serverInstanceId.isEmpty() || taskId.isEmpty() || entryId == 0) {
        return;
    }
    QSettings settings = logClearsSettings();
    settings.beginGroup(QStringLiteral("clearPoints"));
    settings.setValue(logClearPointKey(m_serverInstanceId, taskId), entryId);
    settings.endGroup();
    settings.sync();
}

quint64 MainWindow::nextLogRequestAfter(const QString &taskId) const {
    const quint64 clearPoint = clearedLogEntryId(taskId);
    if (taskId == m_loadedLogTaskId) {
        return qMax(m_lastLogEntryId, clearPoint);
    }
    return clearPoint;
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
        newWidths[column] =
            qMax(kTaskColumnMinWidths[column], qRound(widths[column] * (double(targetWidth) / totalWidth)));
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
            widths[column] =
                qMax(kTaskColumnMinWidths[column], qRound(widths[column] * (double(remainingWidth) / trailingCurrentWidth)));
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

        QTabBar {
            background: #fbfbfc;
            border-bottom: 1px solid #d7dce1;
        }

        QTabBar::tab {
            min-width: 120px;
            max-width: 260px;
            padding: 8px 14px;
            border: 0;
            border-right: 1px solid #d7dce1;
            color: #52606d;
            background: #eef2f5;
        }

        QTabBar::tab:selected {
            color: #1f2933;
            background: #ffffff;
            font-weight: 600;
        }

        QTabBar::tab:hover {
            background: #f6f8fa;
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
