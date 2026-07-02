#pragma once

#include "Task.h"

#include <QAbstractTableModel>

class TaskTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        NameColumn,
        StatusColumn,
        PidColumn,
        CommandColumn,
        ColumnCount
    };

    explicit TaskTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setTasks(const QList<Task> &tasks);
    Task taskAt(int row) const;
    QString taskIdAt(int row) const;

private:
    QList<Task> m_tasks;
};

