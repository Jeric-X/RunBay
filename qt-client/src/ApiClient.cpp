#include "ApiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

ApiClient::ApiClient(QObject *parent)
    : QObject(parent), m_baseUrl(QStringLiteral("http://127.0.0.1:8732")) {}

QUrl ApiClient::baseUrl() const {
    return m_baseUrl;
}

void ApiClient::setBaseUrl(const QUrl &url) {
    m_baseUrl = url;
}

void ApiClient::health() {
    QNetworkRequest req = request(QStringLiteral("/api/health"));
    req.setTransferTimeout(1500);
    QNetworkReply *reply = m_network.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        emit healthChanged(ok);
    });
}

void ApiClient::listTasks() {
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/api/tasks")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(reply->errorString());
            reply->deleteLater();
            return;
        }

        QList<Task> tasks;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        for (const QJsonValue &value : doc.array()) {
            tasks.append(taskFromJson(value.toObject()));
        }
        reply->deleteLater();
        emit tasksLoaded(tasks);
    });
}

void ApiClient::createTask(const QString &name, const QString &command, const QString &cwd, bool startOnLaunch) {
    QJsonObject payload;
    payload.insert(QStringLiteral("name"), name);
    payload.insert(QStringLiteral("command"), command);
    payload.insert(QStringLiteral("cwd"), cwd);
    payload.insert(QStringLiteral("start_on_launch"), startOnLaunch);

    QNetworkRequest req = request(QStringLiteral("/api/tasks"));
    QNetworkReply *reply = m_network.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleTaskReply(reply);
        listTasks();
    });
}

void ApiClient::updateTask(const QString &id, const QString &name, const QString &command, const QString &cwd, bool startOnLaunch) {
    QJsonObject payload;
    payload.insert(QStringLiteral("name"), name);
    payload.insert(QStringLiteral("command"), command);
    payload.insert(QStringLiteral("cwd"), cwd);
    payload.insert(QStringLiteral("start_on_launch"), startOnLaunch);

    QNetworkRequest req = request(QStringLiteral("/api/tasks/%1").arg(id));
    QNetworkReply *reply = m_network.put(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleTaskReply(reply);
        listTasks();
    });
}

void ApiClient::startTask(const QString &id) {
    postAction(id, QStringLiteral("start"));
}

void ApiClient::stopTask(const QString &id) {
    postAction(id, QStringLiteral("stop"));
}

void ApiClient::restartTask(const QString &id) {
    postAction(id, QStringLiteral("restart"));
}

void ApiClient::deleteTask(const QString &id) {
    QNetworkReply *reply = m_network.deleteResource(request(QStringLiteral("/api/tasks/%1").arg(id)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(reply->errorString());
        }
        reply->deleteLater();
        listTasks();
    });
}

void ApiClient::fetchLogs(const QString &id, int tail) {
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/api/tasks/%1/logs?tail=%2").arg(id).arg(tail)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(reply->errorString());
            reply->deleteLater();
            return;
        }

        QStringList lines;
        const QJsonObject object = QJsonDocument::fromJson(body).object();
        for (const QJsonValue &value : object.value(QStringLiteral("lines")).toArray()) {
            lines.append(value.toString());
        }
        reply->deleteLater();
        emit logsLoaded(id, lines);
    });
}

QNetworkRequest ApiClient::request(const QString &path) const {
    QUrl url = m_baseUrl;
    url.setPath(path.section(QLatin1Char('?'), 0, 0));
    if (path.contains(QLatin1Char('?'))) {
        url.setQuery(path.section(QLatin1Char('?'), 1));
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return req;
}

void ApiClient::postAction(const QString &id, const QString &action) {
    QNetworkReply *reply = m_network.post(request(QStringLiteral("/api/tasks/%1/%2").arg(id, action)), QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleTaskReply(reply);
        listTasks();
    });
}

void ApiClient::handleTaskReply(QNetworkReply *reply) {
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(reply->errorString());
        reply->deleteLater();
        return;
    }
    const Task task = taskFromJson(QJsonDocument::fromJson(body).object());
    reply->deleteLater();
    emit taskUpdated(task);
}

Task ApiClient::taskFromJson(const QJsonObject &object) {
    Task task;
    task.id = object.value(QStringLiteral("id")).toString();
    task.name = object.value(QStringLiteral("name")).toString();
    task.command = object.value(QStringLiteral("command")).toString();
    task.cwd = object.value(QStringLiteral("cwd")).toString();
    task.status = object.value(QStringLiteral("status")).toString();
    task.pid = object.value(QStringLiteral("pid")).toInt();
    task.restartCount = object.value(QStringLiteral("restart_count")).toInt();
    task.startOnLaunch = object.value(QStringLiteral("start_on_launch")).toBool();
    task.startedAt = object.value(QStringLiteral("started_at")).toString();
    task.exitedAt = object.value(QStringLiteral("exited_at")).toString();
    return task;
}
