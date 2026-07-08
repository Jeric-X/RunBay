#include "ApiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSharedPointer>
#include <QVariant>

ApiClient::ApiClient(QObject *parent)
    : QObject(parent), m_baseUrl(QStringLiteral("http://127.0.0.1:8732")) {}

namespace {
QString replyErrorMessage(QNetworkReply *reply, const QByteArray &body) {
    const QString apiError = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toString().trimmed();
    if (!apiError.isEmpty()) {
        return apiError;
    }
    return reply ? reply->errorString() : QStringLiteral("Unknown error");
}
} // namespace

QUrl ApiClient::baseUrl() const {
    return m_baseUrl;
}

void ApiClient::setBaseUrl(const QUrl &url) {
    m_baseUrl = url;
}

int ApiClient::context() const {
    return m_context;
}

void ApiClient::setContext(int context) {
    m_context = context;
}

void ApiClient::cancelPendingRequests() {
    const QSet<QNetworkReply *> replies = m_replies;
    for (QNetworkReply *reply : replies) {
        if (reply && reply->isRunning()) {
            reply->abort();
        }
    }
}

void ApiClient::health() {
    const int context = m_context;
    QNetworkRequest req = request(QStringLiteral("/api/health"));
    req.setTransferTimeout(1500);
    QNetworkReply *reply = m_network.get(req);
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, context]() {
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString instanceId = ok ? QJsonDocument::fromJson(body).object().value(QStringLiteral("instance_id")).toString() : QString();
        reply->deleteLater();
        emit healthChanged(context, ok, instanceId);
    });
}

void ApiClient::listTasks() {
    const int context = m_context;
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/api/tasks")));
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, context]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(context, reply->errorString());
            reply->deleteLater();
            return;
        }

        QList<Task> tasks;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        for (const QJsonValue &value : doc.array()) {
            tasks.append(taskFromJson(value.toObject()));
        }
        reply->deleteLater();
        emit tasksLoaded(context, tasks);
    });
}

void ApiClient::createTask(const QString &name, const QString &command, const QString &cwd, bool startOnLaunch) {
    QJsonObject payload;
    payload.insert(QStringLiteral("name"), name);
    payload.insert(QStringLiteral("command"), command);
    payload.insert(QStringLiteral("cwd"), cwd);
    payload.insert(QStringLiteral("start_on_launch"), startOnLaunch);

    const int context = m_context;
    QNetworkRequest req = request(QStringLiteral("/api/tasks"));
    QNetworkReply *reply = m_network.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, context]() {
        handleTaskReply(reply, context);
        if (context == m_context) {
            listTasks();
        }
    });
}

void ApiClient::updateTask(const QString &id, const QString &name, const QString &command, const QString &cwd, bool startOnLaunch) {
    QJsonObject payload;
    payload.insert(QStringLiteral("name"), name);
    payload.insert(QStringLiteral("command"), command);
    payload.insert(QStringLiteral("cwd"), cwd);
    payload.insert(QStringLiteral("start_on_launch"), startOnLaunch);

    const int context = m_context;
    QNetworkRequest req = request(QStringLiteral("/api/tasks/%1").arg(id));
    QNetworkReply *reply = m_network.put(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, context]() {
        handleTaskReply(reply, context);
        if (context == m_context) {
            listTasks();
        }
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
    const int context = m_context;
    QNetworkReply *reply = m_network.deleteResource(request(QStringLiteral("/api/tasks/%1").arg(id)));
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, context]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(context, reply->errorString());
        }
        reply->deleteLater();
        if (context == m_context) {
            listTasks();
        }
    });
}

void ApiClient::exportTasks() {
    const int context = m_context;
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/api/tasks")));
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, context]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(context, reply->errorString());
            reply->deleteLater();
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isArray()) {
            emit errorOccurred(context, QStringLiteral("Task export response was not a JSON array"));
            reply->deleteLater();
            return;
        }

        QJsonArray tasks;
        for (const QJsonValue &value : doc.array()) {
            const QJsonObject source = value.toObject();
            QJsonObject task;
            task.insert(QStringLiteral("name"), source.value(QStringLiteral("name")).toString());
            task.insert(QStringLiteral("command"), source.value(QStringLiteral("command")).toString());
            task.insert(QStringLiteral("cwd"), source.value(QStringLiteral("cwd")).toString());
            task.insert(QStringLiteral("start_on_launch"), source.value(QStringLiteral("start_on_launch")).toBool());
            tasks.append(task);
        }
        reply->deleteLater();
        emit tasksExported(context, tasks);
    });
}

void ApiClient::importTasks(const QJsonArray &tasks) {
    const int context = m_context;
    auto pending = QSharedPointer<int>::create(0);
    auto created = QSharedPointer<int>::create(0);
    auto failures = QSharedPointer<QJsonArray>::create();

    auto finishIfDone = [this, pending, created, failures, context]() {
        if (*pending > 0) {
            return;
        }
        emit tasksImported(context, *created, *failures);
        if (context == m_context) {
            listTasks();
        }
    };

    for (const QJsonValue &value : tasks) {
        const QJsonObject source = value.toObject();
        const QString name = source.value(QStringLiteral("name")).toString().trimmed();
        const QString command = source.value(QStringLiteral("command")).toString().trimmed();
        if (name.isEmpty() || command.isEmpty()) {
            QJsonObject failure;
            failure.insert(QStringLiteral("name"), name.isEmpty() ? QStringLiteral("(unnamed)") : name);
            failure.insert(QStringLiteral("reason"), QStringLiteral("Name and command are required."));
            failures->append(failure);
            continue;
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("name"), name);
        payload.insert(QStringLiteral("command"), command);
        payload.insert(QStringLiteral("cwd"), source.value(QStringLiteral("cwd")).toString());
        payload.insert(QStringLiteral("start_on_launch"), source.value(QStringLiteral("start_on_launch")).toBool());

        ++(*pending);
        QNetworkReply *reply = m_network.post(request(QStringLiteral("/api/tasks")),
                                              QJsonDocument(payload).toJson(QJsonDocument::Compact));
        trackReply(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, pending, created, failures, finishIfDone, context, name]() mutable {
            const QByteArray body = reply->readAll();
            if (reply->error() == QNetworkReply::NoError) {
                ++(*created);
            } else {
                QJsonObject failure;
                failure.insert(QStringLiteral("name"), name);
                failure.insert(QStringLiteral("reason"), replyErrorMessage(reply, body));
                failures->append(failure);
            }
            reply->deleteLater();
            --(*pending);
            finishIfDone();
        });
    }

    finishIfDone();
}

void ApiClient::fetchLogs(const QString &id, quint64 after, int tail) {
    const int context = m_context;
    const QString query = after > 0
                              ? QStringLiteral("after=%1").arg(after)
                              : QStringLiteral("tail=%1").arg(tail);
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/api/tasks/%1/logs?%2").arg(id, query)));
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, context]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(context, reply->errorString());
            reply->deleteLater();
            return;
        }

        QList<LogEntry> entries;
        const QJsonObject object = QJsonDocument::fromJson(body).object();
        for (const QJsonValue &value : object.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject entryObject = value.toObject();
            LogEntry entry;
            entry.id = entryObject.value(QStringLiteral("id")).toVariant().toULongLong();
            entry.text = entryObject.value(QStringLiteral("text")).toString();
            entries.append(entry);
        }
        const quint64 startId = object.value(QStringLiteral("start_id")).toVariant().toULongLong();
        const quint64 endId = object.value(QStringLiteral("end_id")).toVariant().toULongLong();
        const bool truncated = object.value(QStringLiteral("truncated")).toBool();
        const QString instanceId = object.value(QStringLiteral("instance_id")).toString();
        reply->deleteLater();
        emit logsLoaded(context, id, instanceId, entries, startId, endId, truncated);
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
    const int context = m_context;
    QNetworkReply *reply = m_network.post(request(QStringLiteral("/api/tasks/%1/%2").arg(id, action)), QByteArray());
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, context]() {
        handleTaskReply(reply, context);
        if (context == m_context) {
            listTasks();
        }
    });
}

void ApiClient::handleTaskReply(QNetworkReply *reply, int context) {
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        const QString message = replyErrorMessage(reply, body);
        emit errorOccurred(context, message);
        emit taskRequestFailed(context, message);
        reply->deleteLater();
        return;
    }
    const Task task = taskFromJson(QJsonDocument::fromJson(body).object());
    reply->deleteLater();
    emit taskUpdated(context, task);
}

void ApiClient::trackReply(QNetworkReply *reply) {
    if (!reply) {
        return;
    }

    m_replies.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_replies.remove(reply);
    });
    connect(reply, &QObject::destroyed, this, [this, reply]() {
        m_replies.remove(reply);
    });
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
