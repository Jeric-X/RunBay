#include "MainWindow.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontDatabase>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTextCursor>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
QString powershellSingleQuoted(QString value) {
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
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
        const QString id = selectedTaskId();
        if (!id.isEmpty()) {
            m_api.fetchLogs(id);
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
    connect(installServiceAction, &QAction::triggered, this, &MainWindow::installService);

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
    m_taskView->horizontalHeader()->setMinimumSectionSize(56);
    m_taskView->setColumnWidth(TaskTableModel::NameColumn, 190);
    m_taskView->setColumnWidth(TaskTableModel::StatusColumn, 110);
    m_taskView->setColumnWidth(TaskTableModel::PidColumn, 80);
    m_taskView->setColumnWidth(TaskTableModel::CommandColumn, 420);
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

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    m_logView->setFrameShape(QFrame::NoFrame);
    QFont logFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    logFont.setPointSize(10);
    m_logView->setFont(logFont);

    QFrame *leftPane = new QFrame(this);
    leftPane->setObjectName(QStringLiteral("LeftPane"));
    leftPane->setMinimumWidth(430);
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
    rightLayout->addWidget(m_logTitleLabel);
    rightLayout->addWidget(m_logView, 1);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    splitter->setOpaqueResize(true);
    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({620, 560});
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
    const QString elevateCommand = QStringLiteral(
        "Start-Process -FilePath 'powershell.exe' "
        "-ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-Command',%1) "
        "-Verb RunAs")
                                       .arg(powershellSingleQuoted(installCommand));

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
        m_api.fetchLogs(id);
    } else {
        m_logView->clear();
        m_detailLabel->setText(QStringLiteral("No task selected"));
    }
}

void MainWindow::onTasksLoaded(const QList<Task> &tasks) {
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

    if (!previous.isEmpty()) {
        for (int row = 0; row < m_taskModel.rowCount(); ++row) {
            if (m_taskModel.taskIdAt(row) == previous) {
                const QModelIndex sourceIndex = m_taskModel.index(row, 0);
                const QModelIndex proxyIndex = m_proxyModel.mapFromSource(sourceIndex);
                if (proxyIndex.isValid()) {
                    m_taskView->selectRow(proxyIndex.row());
                }
                break;
            }
        }
    }

    updateActions();
}

void MainWindow::onLogsLoaded(const QString &taskId, const QStringList &lines) {
    if (taskId != selectedTaskId()) {
        return;
    }
    m_logView->setPlainText(lines.join(QLatin1Char('\n')));
    m_logView->moveCursor(QTextCursor::End);
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
    m_startAction->setEnabled(hasSelection);
    m_stopAction->setEnabled(hasSelection);
    m_restartAction->setEnabled(hasSelection);
    m_deleteAction->setEnabled(hasSelection);

    if (!hasSelection) {
        return;
    }

    const QModelIndex sourceIndex = m_proxyModel.mapToSource(rows.first());
    const Task task = m_taskModel.taskAt(sourceIndex.row());
    m_detailLabel->setText(QStringLiteral("<b>%1</b><br>Status: %2<br>Command: %3<br>Cwd: %4<br>Startup: %5")
                               .arg(task.name.toHtmlEscaped(), task.status.toHtmlEscaped(),
                                    task.command.toHtmlEscaped(), task.cwd.toHtmlEscaped(),
                                    task.startOnLaunch ? QStringLiteral("start when daemon starts")
                                                       : QStringLiteral("manual")));
    m_startAction->setEnabled(task.status != QStringLiteral("running") && task.status != QStringLiteral("starting"));
    m_stopAction->setEnabled(task.status == QStringLiteral("running") || task.status == QStringLiteral("starting"));
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

    if (output.contains(QStringLiteral("RUNNING"), Qt::CaseInsensitive)) {
        setServiceStatus(QStringLiteral("RunBay service is running; waiting for daemon..."));
        QTimer::singleShot(1000, this, &MainWindow::refresh);
        return;
    }

    QProcess start;
    start.start(QStringLiteral("sc.exe"), {QStringLiteral("start"), QStringLiteral("RunBay")});
    if (!start.waitForFinished(3000)) {
        start.kill();
        setServiceStatus(QStringLiteral("Starting RunBay service timed out"));
        QTimer::singleShot(1800, this, &MainWindow::refresh);
        return;
    }

    const QString startOutput = QString::fromLocal8Bit(start.readAllStandardOutput() + start.readAllStandardError()).simplified();
    if (start.exitCode() != 0) {
        setServiceStatus(QStringLiteral("Disconnected; failed to start RunBay service"));
        if (!startOutput.isEmpty()) {
            statusBar()->showMessage(startOutput, 7000);
        }
        return;
    }

    setServiceStatus(QStringLiteral("Starting RunBay service..."));
    QTimer::singleShot(1800, this, &MainWindow::refresh);
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
    const int minWidths[columnCount] = {110, 82, 60, 160};
    int widths[columnCount] = {
        m_taskView->columnWidth(TaskTableModel::NameColumn),
        m_taskView->columnWidth(TaskTableModel::StatusColumn),
        m_taskView->columnWidth(TaskTableModel::PidColumn),
        m_taskView->columnWidth(TaskTableModel::CommandColumn),
    };

    int totalWidth = 0;
    int minimumTotalWidth = 0;
    for (int column = 0; column < columnCount; ++column) {
        widths[column] = qMax(widths[column], minWidths[column]);
        totalWidth += widths[column];
        minimumTotalWidth += minWidths[column];
    }

    const int targetWidth = qMax(viewportWidth, minimumTotalWidth);
    int newWidths[columnCount] = {};
    int newTotalWidth = 0;
    for (int column = 0; column < columnCount; ++column) {
        newWidths[column] = qMax(minWidths[column], qRound(widths[column] * (double(targetWidth) / totalWidth)));
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
            } else if (newWidths[column] > minWidths[column]) {
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
    const int minWidths[columnCount] = {110, 82, 60, 160};
    const int targetWidth = qMax(m_taskView->viewport()->width(),
                                 minWidths[0] + minWidths[1] + minWidths[2] + minWidths[3]);
    int widths[columnCount] = {
        m_taskView->columnWidth(TaskTableModel::NameColumn),
        m_taskView->columnWidth(TaskTableModel::StatusColumn),
        m_taskView->columnWidth(TaskTableModel::PidColumn),
        m_taskView->columnWidth(TaskTableModel::CommandColumn),
    };
    for (int column = 0; column < columnCount; ++column) {
        widths[column] = qMax(widths[column], minWidths[column]);
    }

    int fixedWidth = 0;
    for (int column = 0; column <= resizedColumn; ++column) {
        fixedWidth += widths[column];
    }

    int trailingMinimumWidth = 0;
    int trailingCurrentWidth = 0;
    for (int column = resizedColumn + 1; column < columnCount; ++column) {
        trailingMinimumWidth += minWidths[column];
        trailingCurrentWidth += widths[column];
    }

    if (resizedColumn == columnCount - 1) {
        int previousWidth = 0;
        for (int column = 0; column < resizedColumn; ++column) {
            previousWidth += widths[column];
        }
        widths[resizedColumn] = qMax(minWidths[resizedColumn], targetWidth - previousWidth);
    } else if (fixedWidth + trailingMinimumWidth > targetWidth) {
        int availableForFixed = targetWidth - trailingMinimumWidth;
        for (int column = resizedColumn; column >= 0 && fixedWidth > availableForFixed; --column) {
            const int shrink = qMin(widths[column] - minWidths[column], fixedWidth - availableForFixed);
            widths[column] -= shrink;
            fixedWidth -= shrink;
        }
        for (int column = resizedColumn + 1; column < columnCount; ++column) {
            widths[column] = minWidths[column];
        }
    } else {
        int remainingWidth = targetWidth - fixedWidth;
        int trailingTotalWidth = 0;
        for (int column = resizedColumn + 1; column < columnCount; ++column) {
            widths[column] = qMax(minWidths[column], qRound(widths[column] * (double(remainingWidth) / trailingCurrentWidth)));
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
                } else if (widths[column] > minWidths[column]) {
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
