#include "ServiceManagerWindow.h"

QString ServiceManagerWindow::registerServiceCommand(const ServiceConfig &service, const QString &daemonPath,
                                                       const QString &resultFile) {
    Q_UNUSED(service)
    Q_UNUSED(daemonPath)
    Q_UNUSED(resultFile)
    return {};
}

bool ServiceManagerWindow::runElevatedCommand(const QString &command, const QString &resultFile) {
    Q_UNUSED(command)
    Q_UNUSED(resultFile)
    return false;
}

QString ServiceManagerWindow::serviceCommand(const QString &serviceName, const QString &operation,
                                               const QString &resultFile) const {
    Q_UNUSED(serviceName)
    Q_UNUSED(operation)
    Q_UNUSED(resultFile)
    return {};
}

QString ServiceManagerWindow::serviceAccountName(const QString &user) const {
    return user;
}

bool ServiceManagerWindow::serviceAccountNeedsPassword(const QString &user) const {
    Q_UNUSED(user)
    return false;
}

bool ServiceManagerWindow::verifyAccountPassword(const QString &account, const QString &password) const {
    Q_UNUSED(account)
    Q_UNUSED(password)
    return true;
}
