#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace ServiceManagerPlatform {

enum class Status {
    NotRegistered,
    Stopped,
    Running,
};

bool supportsInAppControl();
QString bundledDaemonPath();
QStringList localServiceUsers();
QString defaultServicePath(const QString &user);
Status queryServiceStatus(const QString &serviceName);

void showRegisterHelp(QWidget *parent);
void showUnregisterHelp(QWidget *parent);
void showStartHelp(QWidget *parent);
void showStopHelp(QWidget *parent);
bool blockDeletingRegisteredService(QWidget *parent);

} // namespace ServiceManagerPlatform
