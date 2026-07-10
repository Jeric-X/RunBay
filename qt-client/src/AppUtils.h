#pragma once

#include <QString>

#include <yaml-cpp/yaml.h>

namespace AppUtils {
QString appDataDirectoryPath();
QString appDataFilePath(const QString &fileName);
YAML::Node loadYamlFile(const QString &path);
void saveYamlFile(const QString &path, const YAML::Node &root);
QString powershellSingleQuoted(QString value);
} // namespace AppUtils
