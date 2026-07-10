#pragma once

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QAction;
class QResizeEvent;
class QTableWidget;

class ServiceManagerWindow : public QWidget {
    Q_OBJECT

public:
    explicit ServiceManagerWindow(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void serviceActionRequested(const QString &message);
    void addServerRequested(const QString &name, const QString &url);

private:
    struct ServiceConfig {
        QString name;
        QString ip;
        QString user;
        QString path;
        int port = 0;
    };

    enum class ServiceStatus {
        NotRegistered,
        Stopped,
        Running,
    };

    enum class ServiceOperationType {
        None,
        Start,
        Stop,
        Register,
        Unregister,
        Delete,
    };

    struct ServiceOperation {
        ServiceOperationType type = ServiceOperationType::None;
        QString serviceName;
        QString resultFile;
        QString statusText;
        int elapsedMs = 0;
    };

    void buildUi();
    void refreshTable();
    void refreshServiceStatuses();
    void updateActions();
    void addService();
    void registerSelectedService();
    void unregisterSelectedService();
    void startSelectedService();
    void stopSelectedService();
    void deleteSelectedService();
    void addSelectedServiceToServerManager();
    ServiceConfig selectedService() const;
    QString bundledDaemonPath() const;
    QString registerServiceCommand(const ServiceConfig &service, const QString &daemonPath, const QString &resultFile);
    bool beginElevatedOperation(ServiceOperationType type, const QString &serviceName, const QString &command,
                                const QString &resultFile, const QString &promptFailureMessage);
    bool runElevatedPowerShell(const QString &command);
    QString serviceAddress(const ServiceConfig &service) const;
    QString serviceServerUrl(const ServiceConfig &service) const;
    QString serviceCommand(const QString &serviceName, const QString &operation, const QString &resultFile = QString()) const;
    QStringList localServiceUsers() const;
    QString serviceAccountName(const QString &user) const;
    bool serviceAccountNeedsPassword(const QString &user) const;
    bool verifyAccountPassword(const QString &account, const QString &password) const;
    QString defaultServicePath(const QString &user) const;
    ServiceStatus queryServiceStatus(const QString &serviceName) const;
    QString statusText(ServiceStatus status) const;
    QList<ServiceConfig> loadServices() const;
    void saveServices() const;
    QSize loadWindowSize() const;
    QList<double> loadTableColumnRatios() const;
    void applyTableColumnRatios();
    void resizeServiceColumnsToViewport(int viewportWidth);
    void resizeTrailingServiceColumnsToViewport(int resizedColumn);
    void onServiceHeaderSectionResized(int logicalIndex, int oldSize, int newSize);
    void saveWindowSettings() const;

    // Operation state machine
    void startOperation(ServiceOperationType type, const QString &serviceName, const QString &resultFile);
    void cancelCurrentOperation();
    void checkOperationResult();
    void completeOperation(bool success, const QString &message);
    QString generateResultFileName(const QString &serviceName) const;
    void updateOperationStatus();

    QList<ServiceConfig> m_services;
    QList<ServiceStatus> m_statuses;
    QTableWidget *m_table = nullptr;
    QAction *m_registerAction = nullptr;
    QAction *m_unregisterAction = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_addServerAction = nullptr;
    QTimer m_statusTimer;
    QTimer m_operationTimer;
    ServiceOperation m_currentOperation;
    QList<double> m_tableColumnRatios;
    bool m_applyingTableColumnRatios = false;
};
