#pragma once

#include <QDateTime>
#include <QString>

struct Task {
    QString id;
    QString name;
    QString command;
    QString cwd;
    QString status;
    int pid = 0;
    int restartCount = 0;
    bool startOnLaunch = false;
    QString startedAt;
    QString exitedAt;
};

struct LogEntry {
    quint64 id = 0;
    QString text;
};
