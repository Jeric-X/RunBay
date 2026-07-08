#pragma once

#include "Task.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QUrl>

class QNetworkReply;

class ApiClient : public QObject {
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);

    QUrl baseUrl() const;
    void setBaseUrl(const QUrl &url);
    int context() const;
    void setContext(int context);

    void listTasks();
    void createTask(const QString &name, const QString &command, const QString &cwd, bool startOnLaunch);
    void updateTask(const QString &id, const QString &name, const QString &command, const QString &cwd, bool startOnLaunch);
    void startTask(const QString &id);
    void stopTask(const QString &id);
    void restartTask(const QString &id);
    void deleteTask(const QString &id);
    void exportTasks();
    void importTasks(const QJsonArray &tasks);
    void fetchLogs(const QString &id, quint64 after = 0, int tail = 500);
    void health();
    void cancelPendingRequests();

signals:
    void healthChanged(int context, bool ok, const QString &instanceId);
    void tasksLoaded(int context, const QList<Task> &tasks);
    void taskUpdated(int context, const Task &task);
    void taskRequestFailed(int context, const QString &message);
    void tasksExported(int context, const QJsonArray &tasks);
    void tasksImported(int context, int created, const QJsonArray &failures);
    void logsLoaded(int context, const QString &taskId, const QString &instanceId, const QList<LogEntry> &entries, quint64 startId, quint64 endId, bool truncated);
    void errorOccurred(int context, const QString &message);

private:
    QNetworkRequest request(const QString &path) const;
    void postAction(const QString &id, const QString &action);
    void handleTaskReply(QNetworkReply *reply, int context);
    void trackReply(QNetworkReply *reply);
    static Task taskFromJson(const QJsonObject &object);

    QUrl m_baseUrl;
    int m_context = 0;
    QSet<QNetworkReply *> m_replies;
    QNetworkAccessManager m_network;
};
