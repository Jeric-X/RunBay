#include "ServiceRuntimeConfig.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace ServiceRuntimeConfig {

QString identifier(const QString &serviceName) {
    QString slug = serviceName.trimmed().toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (slug.isEmpty()) {
        slug = QStringLiteral("service");
    }
    slug = slug.left(32);

    const QByteArray digest = QCryptographicHash::hash(serviceName.trimmed().toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("%1-%2").arg(slug, QString::fromLatin1(digest.left(12)));
}

bool write(const QString &path, const QString &serviceId, const QString &name, const QString &listenAddress,
           const QString &dataFile, const QString &logDirectory, const QString &user, QString *errorMessage) {
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("service_id"), serviceId);
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("listen_address"), listenAddress);
    root.insert(QStringLiteral("data_file"), dataFile);
    root.insert(QStringLiteral("log_directory"), logDirectory);
    root.insert(QStringLiteral("user"), user);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace ServiceRuntimeConfig
