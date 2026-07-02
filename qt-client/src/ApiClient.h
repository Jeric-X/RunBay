#pragma once

#include "Task.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class ApiClient : public QObject {
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);

    QUrl baseUrl() const;
    void setBaseUrl(const QUrl &url);

    void listTasks();
    void createTask(const QString &name, const QString &command, const QString &cwd, bool startOnLaunch);
    void updateTask(const QString &id, const QString &name, const QString &command, const QString &cwd, bool startOnLaunch);
    void startTask(const QString &id);
    void stopTask(const QString &id);
    void restartTask(const QString &id);
    void deleteTask(const QString &id);
    void fetchLogs(const QString &id, int tail = 500);
    void health();

signals:
    void healthChanged(bool ok);
    void tasksLoaded(const QList<Task> &tasks);
    void taskUpdated(const Task &task);
    void logsLoaded(const QString &taskId, const QStringList &lines);
    void errorOccurred(const QString &message);

private:
    QNetworkRequest request(const QString &path) const;
    void postAction(const QString &id, const QString &action);
    void handleTaskReply(QNetworkReply *reply);
    static Task taskFromJson(const QJsonObject &object);

    QUrl m_baseUrl;
    QNetworkAccessManager m_network;
};
