#include "AppUtils.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace AppUtils {
QString appDataDirectoryPath() {
#ifdef Q_OS_WIN
    const QString roamingRoot = qEnvironmentVariable("APPDATA");
    const QString dataPath = roamingRoot.isEmpty() ? QDir::home().filePath(QStringLiteral(".runbay"))
                                                   : QDir(roamingRoot).filePath(QStringLiteral("RunBay"));
#else
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString dataPath = dataRoot.isEmpty() ? QDir::home().filePath(QStringLiteral(".runbay"))
                                                : QDir(dataRoot).filePath(QStringLiteral("RunBay"));
#endif
    QDir().mkpath(dataPath);
    return dataPath;
}

QString appDataFilePath(const QString &fileName) {
    return QDir(appDataDirectoryPath()).filePath(fileName);
}

YAML::Node loadYamlFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return YAML::Node(YAML::NodeType::Map);
    }

    try {
        YAML::Node root = YAML::Load(file.readAll().toStdString());
        return root.IsDefined() ? root : YAML::Node(YAML::NodeType::Map);
    } catch (const YAML::Exception &) {
        return YAML::Node(YAML::NodeType::Map);
    }
}

void saveYamlFile(const QString &path, const YAML::Node &root) {
    YAML::Emitter emitter;
    emitter.SetIndent(2);
    emitter << root;
    if (!emitter.good()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }
    file.write(emitter.c_str());
    file.write("\n");
    file.close();
}

QString powershellSingleQuoted(QString value) {
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}
} // namespace AppUtils
