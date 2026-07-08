#pragma once

#include "ApiClient.h"
#include "TaskTableModel.h"

#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableView>
#include <QTextCharFormat>
#include <QTimer>

class QCloseEvent;
class QResizeEvent;
class QSplitter;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void refresh();
    void addTask();
    void importTasks();
    void exportTasks();
    void startSelectedTask();
    void stopSelectedTask();
    void restartSelectedTask();
    void deleteSelectedTask();
    void clearLogView();
    void scrollLogToTop();
    void scrollLogToBottom();
    void openDataDirectory();
    void addServer();
    void deleteCurrentServer();
    void onServerTabChanged(int index);
    void installService();
    void deleteService();
    void startService();
    void stopService();
    void editTaskAt(const QModelIndex &index);
    void onTaskHeaderSectionResized(int logicalIndex, int oldSize, int newSize);
    void onSelectionChanged();
    void onTasksLoaded(int context, const QList<Task> &tasks);
    void onLogsLoaded(int context, const QString &taskId, const QString &instanceId, const QList<LogEntry> &entries, quint64 startId, quint64 endId, bool truncated);

private:
    QString selectedTaskId() const;
    void buildUi();
    void updateActions();
    void applyStyle();
    void ensureDaemonServiceStarted();
    QString bundledDaemonPath() const;
    bool installServiceWithDaemon(const QString &daemonPath);
    bool runElevatedPowerShell(const QString &command);
    void addTaskFromRunnable(const QString &runnablePath);
    void setDaemonConnected(bool connected);
    void setServiceStatus(const QString &message);
    void loadServerSettings();
    void saveServerSettings() const;
    void setCurrentServer(int index);
    void updateServerTabs();
    void updateServerEmptyState();
    QString currentServerUrl() const;
    QString currentServerName() const;
    bool taskEditorDialog(const QString &title, const Task *task, QString *name, QString *command, QString *cwd, bool *startOnLaunch);
    void restoreUiState();
    void saveUiState() const;
    quint64 clearedLogEntryId(const QString &taskId) const;
    void saveClearedLogEntryId(const QString &taskId, quint64 entryId) const;
    quint64 nextLogRequestAfter(const QString &taskId) const;
    void resizeTaskColumnsToViewport(int viewportWidth);
    void resizeTrailingTaskColumnsToViewport(int resizedColumn);

    ApiClient m_api;
    TaskTableModel m_taskModel;
    QSortFilterProxyModel m_proxyModel;
    QTableView *m_taskView = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_logTitleLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QSplitter *m_splitter = nullptr;
    QStackedWidget *m_contentStack = nullptr;
    QPushButton *m_emptyServerButton = nullptr;
    QTabBar *m_serverTabs = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_restartAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QTimer m_refreshTimer;
    QTimer m_logTimer;
    QStringList m_serverNames;
    QStringList m_serverUrls;
    int m_currentServerIndex = -1;
    int m_serverRequestContext = 0;
    QString m_serverInstanceId;
    QString m_loadedLogTaskId;
    QJsonArray m_pendingImportFailures;
    quint64 m_lastLogEntryId = 0;
    QTextCharFormat m_logFormat;
    bool m_columnsSizedToContents = false;
    bool m_resizingColumns = false;
    bool m_daemonConnected = true;
    bool m_serviceStartAttempted = false;
    bool m_serviceStatusMessageActive = false;
    int m_lastTableViewportWidth = 0;
};
