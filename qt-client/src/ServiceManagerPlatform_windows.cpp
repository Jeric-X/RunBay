#include "ServiceManagerPlatform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace ServiceManagerPlatform {

namespace {
bool accountNeedsPassword(const QString &user) {
    return user.compare(QStringLiteral("LocalSystem"), Qt::CaseInsensitive) != 0 &&
           !user.startsWith(QStringLiteral("NT AUTHORITY\\"), Qt::CaseInsensitive);
}
} // namespace

bool supportsInAppControl() {
    return true;
}

QString bundledDaemonPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("runbayd.exe")),
        QDir(appDir).filePath(QStringLiteral("daemon.exe")),
        QDir(appDir).filePath(QStringLiteral("runbay-daemon.exe")),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QStringList localServiceUsers() {
    QStringList users = {
        QStringLiteral("LocalSystem"),
        QStringLiteral("NT AUTHORITY\\LocalService"),
        QStringLiteral("NT AUTHORITY\\NetworkService"),
    };

    QProcess query;
    query.start(QStringLiteral("powershell.exe"),
                {QStringLiteral("-NoProfile"),
                 QStringLiteral("-ExecutionPolicy"),
                 QStringLiteral("Bypass"),
                 QStringLiteral("-Command"),
                 QStringLiteral("Get-LocalUser | Where-Object { $_.Enabled } | Select-Object -ExpandProperty Name")});
    if (query.waitForFinished(1200) && query.exitCode() == 0) {
        const QString output = QString::fromLocal8Bit(query.readAllStandardOutput());
        for (const QString &line : output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
            const QString user = line.trimmed();
            if (!user.isEmpty() && !users.contains(user, Qt::CaseInsensitive)) {
                users.append(user);
            }
        }
    } else {
        query.kill();
        const QString currentUser = qEnvironmentVariable("USERNAME").trimmed();
        if (!currentUser.isEmpty() && !users.contains(currentUser, Qt::CaseInsensitive)) {
            users.append(currentUser);
        }
    }
    return users;
}

QString defaultServicePath(const QString &user) {
    if (!accountNeedsPassword(user)) {
        const QString programData = qEnvironmentVariable("ProgramData");
        return QDir::toNativeSeparators(QDir(programData.isEmpty() ? QStringLiteral("C:/ProgramData") : programData)
                                            .filePath(QStringLiteral("RunBayd")));
    }

    QString userName = user;
    const int slash = userName.lastIndexOf(QLatin1Char('\\'));
    if (slash >= 0) {
        userName = userName.mid(slash + 1);
    }
    return QDir::toNativeSeparators(QStringLiteral("C:/Users/%1/AppData/Roaming/RunBayd").arg(userName));
}

Status queryServiceStatus(const QString &serviceName) {
    QProcess query;
    query.start(QStringLiteral("sc.exe"), {QStringLiteral("query"), serviceName});
    if (!query.waitForFinished(700)) {
        query.kill();
        return Status::NotRegistered;
    }

    const QString output = QString::fromLocal8Bit(query.readAllStandardOutput() + query.readAllStandardError());
    if (query.exitCode() != 0) {
        return Status::NotRegistered;
    }
    return output.contains(QStringLiteral("RUNNING"), Qt::CaseInsensitive) ? Status::Running : Status::Stopped;
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
