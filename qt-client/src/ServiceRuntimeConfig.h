#pragma once

#include <QString>

namespace ServiceRuntimeConfig {

QString identifier(const QString &serviceName);

bool write(const QString &path, const QString &serviceId, const QString &name, const QString &listenAddress,
           const QString &dataFile, const QString &logDirectory, const QString &user, QString *errorMessage = nullptr);

} // namespace ServiceRuntimeConfig
