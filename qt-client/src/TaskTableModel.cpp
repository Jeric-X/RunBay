#include "TaskTableModel.h"

#include <QBrush>
#include <QColor>

TaskTableModel::TaskTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int TaskTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_tasks.size();
}

int TaskTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TaskTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size()) {
        return {};
    }

    const Task &task = m_tasks.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case NameColumn:
            return task.name;
        case StatusColumn:
            return QStringLiteral("  %1").arg(task.status);
        case PidColumn:
            return task.pid == 0 ? QVariant(QString()) : QVariant(task.pid);
        case CommandColumn:
            return task.command;
        default:
            return {};
        }
    }

    if (role == Qt::ForegroundRole && index.column() == StatusColumn) {
        if (task.status == QStringLiteral("running")) {
            return QBrush(QColor(28, 128, 72));
        }
        if (task.status == QStringLiteral("failed")) {
            return QBrush(QColor(190, 50, 50));
        }
        if (task.status == QStringLiteral("starting") || task.status == QStringLiteral("stopping")) {
            return QBrush(QColor(170, 105, 20));
        }
    }

    if (role == Qt::DecorationRole && index.column() == StatusColumn) {
        if (task.status == QStringLiteral("running")) {
            return QColor(34, 153, 84);
        }
        if (task.status == QStringLiteral("failed")) {
            return QColor(207, 68, 70);
        }
        if (task.status == QStringLiteral("starting") || task.status == QStringLiteral("stopping")) {
            return QColor(205, 133, 38);
        }
        return QColor(145, 145, 145);
    }

    if (role == Qt::TextAlignmentRole) {
        return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
    }

    return {};
}

QVariant TaskTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case NameColumn:
        return QStringLiteral("Name");
    case StatusColumn:
        return QStringLiteral("Status");
    case PidColumn:
        return QStringLiteral("PID");
    case CommandColumn:
        return QStringLiteral("Command");
    default:
        return {};
    }
}

void TaskTableModel::setTasks(const QList<Task> &tasks) {
    beginResetModel();
    m_tasks = tasks;
    endResetModel();
}

Task TaskTableModel::taskAt(int row) const {
    if (row < 0 || row >= m_tasks.size()) {
        return {};
    }
    return m_tasks.at(row);
}

QString TaskTableModel::taskIdAt(int row) const {
    return taskAt(row).id;
}
