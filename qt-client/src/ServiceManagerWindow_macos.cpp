#include "ServiceManagerWindow.h"

#include "ServiceRuntimeConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QProcess>

namespace {
QString shellQuote(QString value) {
    value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(value);
}

QString appleScriptString(QString value) {
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return value;
}

QString managerPath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/manage-launchdaemon.sh"));
}
} // namespace

QString ServiceManagerWindow::registerServiceCommand(const ServiceConfig &service, const QString &daemonPath,
                                                       const QString &resultFile) {
    const QString configTemp = resultFile + QStringLiteral(".service.json");
    const QString dataFile = QDir(service.path).filePath(QStringLiteral("tasks.json"));
    const QString logDirectory = QDir(service.path).filePath(QStringLiteral("logs"));
    const QString serviceId = ServiceRuntimeConfig::identifier(service.name);
    QString errorMessage;
    if (!ServiceRuntimeConfig::write(configTemp, serviceId, service.name, serviceAddress(service), dataFile, logDirectory,
                                     service.user, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("RunBay"),
                             QStringLiteral("Failed to create service configuration: %1").arg(errorMessage));
        return {};
    }

    return QStringLiteral("%1 install %2 %3 %4")
        .arg(shellQuote(managerPath()), shellQuote(daemonPath), shellQuote(configTemp), shellQuote(resultFile));
}

bool ServiceManagerWindow::runElevatedCommand(const QString &command, const QString &resultFile) {
    const QString authorizationError = resultFile + QStringLiteral(".authorization-error");
    const QString appleScript =
        QStringLiteral("do shell script \"%1\" with administrator privileges").arg(appleScriptString(command));
    const QString wrapper =
        QStringLiteral("/usr/bin/osascript -e %1 >/dev/null 2>%2 || { status=$?; "
                       "message=$(/bin/cat %2); if [ ! -f %3 ]; then "
                       "/usr/bin/printf 'ERROR: %s\\n' \"$message\" >%3; fi; "
                       "/bin/rm -f %2; exit $status; }")
            .arg(shellQuote(appleScript), shellQuote(authorizationError), shellQuote(resultFile));
    return QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), wrapper});
}

QString ServiceManagerWindow::serviceCommand(const QString &serviceName, const QString &operation,
                                               const QString &resultFile) const {
    const QString serviceId = ServiceRuntimeConfig::identifier(serviceName);
    const QString managerOperation = operation == QStringLiteral("delete") ? QStringLiteral("uninstall") : operation;
    return QStringLiteral("%1 %2 %3 %4")
        .arg(shellQuote(managerPath()), shellQuote(managerOperation), shellQuote(serviceId), shellQuote(resultFile));
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
