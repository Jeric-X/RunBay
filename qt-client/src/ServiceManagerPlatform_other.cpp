#include "ServiceManagerPlatform.h"

#include "AppUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

namespace ServiceManagerPlatform {

bool supportsInAppControl() {
    return false;
}

QString bundledDaemonPath() {
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runbayd"));
    return QFileInfo::exists(path) ? path : QString();
}

QStringList localServiceUsers() {
    const QString user = qEnvironmentVariable("USER").trimmed();
    return user.isEmpty() ? QStringList{} : QStringList{user};
}

QString defaultServicePath(const QString &user) {
    Q_UNUSED(user)
    return AppUtils::appDataDirectoryPath();
}

Status queryServiceStatus(const QString &serviceName) {
    Q_UNUSED(serviceName)
    return Status::NotRegistered;
}

namespace {
void showUnsupported(QWidget *parent) {
    QMessageBox::information(parent, QStringLiteral("RunBay"),
                             QStringLiteral("System service control is not implemented on this platform."));
}
} // namespace

void showRegisterHelp(QWidget *parent) {
    showUnsupported(parent);
}

void showUnregisterHelp(QWidget *parent) {
    showUnsupported(parent);
}

void showStartHelp(QWidget *parent) {
    showUnsupported(parent);
}

void showStopHelp(QWidget *parent) {
    showUnsupported(parent);
}

bool blockDeletingRegisteredService(QWidget *parent) {
    Q_UNUSED(parent)
    return false;
}

} // namespace ServiceManagerPlatform
