#include "ServiceManagerPlatform.h"

#include "ServiceRuntimeConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace ServiceManagerPlatform {

namespace {
QString launchDaemonLabel(const QString &serviceName) {
    return QStringLiteral("com.runbay.daemon.%1").arg(ServiceRuntimeConfig::identifier(serviceName));
}

QString userHomeDirectory(const QString &user) {
    if (user == QStringLiteral("root")) {
        return QStringLiteral("/var/root");
    }
    QProcess query;
    query.start(QStringLiteral("/usr/bin/dscl"),
                {QStringLiteral("."), QStringLiteral("-read"), QStringLiteral("/Users/%1").arg(user),
                 QStringLiteral("NFSHomeDirectory")});
    if (query.waitForFinished(700) && query.exitCode() == 0) {
        const QString output = QString::fromUtf8(query.readAllStandardOutput()).trimmed();
        const int separator = output.indexOf(QLatin1Char(':'));
        if (separator >= 0) {
            const QString home = output.mid(separator + 1).trimmed();
            if (!home.isEmpty()) {
                return home;
            }
        }
    }
    return QDir(QStringLiteral("/Users")).filePath(user);
}
} // namespace

bool supportsInAppControl() {
    return true;
}

QString bundledDaemonPath() {
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runbayd"));
    return QFileInfo::exists(path) ? path : QString();
}

QStringList localServiceUsers() {
    QStringList users;
    const QString currentUser = qEnvironmentVariable("USER").trimmed();
    if (!currentUser.isEmpty() && currentUser != QStringLiteral("root")) {
        users.append(currentUser);
    }
    users.append(QStringLiteral("root"));

    QProcess query;
    query.start(QStringLiteral("/usr/bin/dscl"),
                {QStringLiteral("."), QStringLiteral("-list"), QStringLiteral("/Users"), QStringLiteral("UniqueID")});
    if (query.waitForFinished(1200) && query.exitCode() == 0) {
        const QString output = QString::fromUtf8(query.readAllStandardOutput());
        for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList fields = line.simplified().split(QLatin1Char(' '));
            if (fields.size() != 2) {
                continue;
            }
            bool ok = false;
            const int uid = fields.at(1).toInt(&ok);
            const QString user = fields.at(0);
            if (ok && uid >= 500 && uid < 65534 && !user.startsWith(QLatin1Char('_')) &&
                !users.contains(user, Qt::CaseInsensitive)) {
                users.append(user);
            }
        }
    }
    return users;
}

QString defaultServicePath(const QString &user) {
    if (user == QStringLiteral("root")) {
        return QStringLiteral("/Library/Application Support/RunBayd");
    }
    return QDir(userHomeDirectory(user)).filePath(QStringLiteral("Library/Application Support/RunBayd"));
}

Status queryServiceStatus(const QString &serviceName) {
    const QString label = launchDaemonLabel(serviceName);
    const QString plist = QStringLiteral("/Library/LaunchDaemons/%1.plist").arg(label);
    QProcess query;
    query.start(QStringLiteral("/bin/launchctl"),
                {QStringLiteral("print"), QStringLiteral("system/%1").arg(label)});
    if (query.waitForFinished(700) && query.exitCode() == 0) {
        return Status::Running;
    }
    if (query.state() != QProcess::NotRunning) {
        query.kill();
    }
    return QFileInfo::exists(plist) ? Status::Stopped : Status::NotRegistered;
}

void showRegisterHelp(QWidget *parent) {
    Q_UNUSED(parent)
}

void showUnregisterHelp(QWidget *parent) {
    Q_UNUSED(parent)
}

void showStartHelp(QWidget *parent) {
    Q_UNUSED(parent)
}

void showStopHelp(QWidget *parent) {
    Q_UNUSED(parent)
}

bool blockDeletingRegisteredService(QWidget *parent) {
    Q_UNUSED(parent)
    return false;
}

} // namespace ServiceManagerPlatform
