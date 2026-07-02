#pragma once

#include "ApiClient.h"
#include "TaskTableModel.h"

#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTableView>
#include <QTextCharFormat>
#include <QTimer>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void refresh();
    void addTask();
    void startSelectedTask();
    void stopSelectedTask();
    void restartSelectedTask();
    void deleteSelectedTask();
    void clearLogView();
    void installService();
    void deleteService();
    void startService();
    void stopService();
    void editTaskAt(const QModelIndex &index);
    void onTaskHeaderSectionResized(int logicalIndex, int oldSize, int newSize);
    void onSelectionChanged();
    void onTasksLoaded(const QList<Task> &tasks);
    void onLogsLoaded(const QString &taskId, const QList<LogEntry> &entries, quint64 startId, quint64 endId, bool truncated);

private:
    QString selectedTaskId() const;
    void buildUi();
    void updateActions();
    void applyStyle();
    void ensureDaemonServiceStarted();
    QString bundledDaemonPath() const;
    bool installServiceWithDaemon(const QString &daemonPath);
    bool runElevatedPowerShell(const QString &command);
    void setDaemonConnected(bool connected);
    void setServiceStatus(const QString &message);
    bool taskEditorDialog(const QString &title, const Task *task, QString *name, QString *command, QString *cwd, bool *startOnLaunch);
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
    QAction *m_startAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_restartAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QTimer m_refreshTimer;
    QTimer m_logTimer;
    QString m_loadedLogTaskId;
    quint64 m_lastLogEntryId = 0;
    QTextCharFormat m_logFormat;
    bool m_columnsSizedToContents = false;
    bool m_resizingColumns = false;
    bool m_daemonConnected = true;
    bool m_serviceStartAttempted = false;
    bool m_serviceStatusMessageActive = false;
    int m_lastTableViewportWidth = 0;
};
