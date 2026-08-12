#include "ServiceManagerWindow.h"

#include "AppUtils.h"
#include "ServiceManagerPlatform.h"

#include <QAbstractItemView>
#include <QAbstractSocket>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
constexpr int kServiceColumnCount = 6;
constexpr int kServiceColumnMinWidths[kServiceColumnCount] = {80, 80, 60, 100, 160, 80};

QString serviceConfigFilePath() {
    return AppUtils::appDataFilePath(QStringLiteral("service.yml"));
}

void showSelectableWarning(QWidget *parent, const QString &title, const QString &text) {
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    for (QLabel *label : box.findChildren<QLabel *>()) {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    }
    box.exec();
}

} // namespace

ServiceManagerWindow::ServiceManagerWindow(QWidget *parent) : QWidget(parent, Qt::Window) {
    m_services = loadServices();
    buildUi();
    refreshTable();
    if (!m_services.isEmpty()) {
        m_table->selectRow(0);
    }
    refreshServiceStatuses();
    connect(&m_statusTimer, &QTimer::timeout, this, &ServiceManagerWindow::refreshServiceStatuses);
    m_statusTimer.start(1000);

    connect(&m_operationTimer, &QTimer::timeout, this, &ServiceManagerWindow::checkOperationResult);
}

void ServiceManagerWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_table) {
        resizeServiceColumnsToViewport(m_table->viewport()->width());
        saveWindowSettings();
    }
}

void ServiceManagerWindow::buildUi() {
    setWindowTitle(QStringLiteral("Service"));
    const QSize savedSize = loadWindowSize();
    resize(savedSize.isValid() ? savedSize.expandedTo(minimumSize()) : QSize(620, 420));

    QVBoxLayout *layout = new QVBoxLayout(this);
    QToolBar *toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QAction *addAction = toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("Add"));
    m_registerAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("Register"));
    m_unregisterAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("Unregister"));
    m_startAction = toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Start"));
    m_stopAction = toolbar->addAction(style()->standardIcon(QStyle::SP_MediaStop), QStringLiteral("Stop"));
    m_addServerAction = toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowForward), QStringLiteral("Add Server"));
    m_deleteAction = toolbar->addAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("Delete"));
    layout->addWidget(toolbar);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("IP"), QStringLiteral("Port"), QStringLiteral("User"), QStringLiteral("Path"),
         QStringLiteral("Status")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionsMovable(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table);

    m_tableColumnRatios = loadTableColumnRatios();
    QTimer::singleShot(0, this, &ServiceManagerWindow::applyTableColumnRatios);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ServiceManagerWindow::updateActions);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this, &ServiceManagerWindow::onServiceHeaderSectionResized);
    connect(addAction, &QAction::triggered, this, &ServiceManagerWindow::addService);
    connect(m_registerAction, &QAction::triggered, this, &ServiceManagerWindow::registerSelectedService);
    connect(m_unregisterAction, &QAction::triggered, this, &ServiceManagerWindow::unregisterSelectedService);
    connect(m_startAction, &QAction::triggered, this, &ServiceManagerWindow::startSelectedService);
    connect(m_stopAction, &QAction::triggered, this, &ServiceManagerWindow::stopSelectedService);
    connect(m_addServerAction, &QAction::triggered, this, &ServiceManagerWindow::addSelectedServiceToServerManager);
    connect(m_deleteAction, &QAction::triggered, this, &ServiceManagerWindow::deleteSelectedService);
}

void ServiceManagerWindow::refreshTable() {
    while (m_statuses.size() < m_services.size()) {
        m_statuses.append(ServiceStatus::NotRegistered);
    }
    while (m_statuses.size() > m_services.size()) {
        m_statuses.removeLast();
    }

    m_table->setRowCount(m_services.size());
    for (int row = 0; row < m_services.size(); ++row) {
        const ServiceConfig &service = m_services.at(row);
        m_table->setItem(row, 0, new QTableWidgetItem(service.name));
        m_table->setItem(row, 1, new QTableWidgetItem(service.ip));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::number(service.port)));
        m_table->setItem(row, 3, new QTableWidgetItem(service.user));
        m_table->setItem(row, 4, new QTableWidgetItem(service.path));
        m_table->setItem(row, 5, new QTableWidgetItem(statusText(m_statuses.at(row))));
    }
    updateActions();
}

void ServiceManagerWindow::refreshServiceStatuses() {
    bool changed = m_statuses.size() != m_services.size();
    QList<ServiceStatus> statuses;
    statuses.reserve(m_services.size());
    for (const ServiceConfig &service : m_services) {
        statuses.append(queryServiceStatus(service.name));
    }

    for (int index = 0; index < statuses.size() && !changed; ++index) {
        changed = statuses.at(index) != m_statuses.at(index);
    }

    m_statuses = statuses;
    if (!changed) {
        updateActions();
        return;
    }

    for (int row = 0; row < m_statuses.size(); ++row) {
        QTableWidgetItem *item = m_table->item(row, 5);
        if (!item) {
            item = new QTableWidgetItem;
            m_table->setItem(row, 5, item);
        }
        item->setText(statusText(m_statuses.at(row)));
    }
    updateActions();
}

void ServiceManagerWindow::updateActions() {
    const bool isOperating = m_currentOperation.type != ServiceOperationType::None;
    const int row = m_table->currentRow();
    const bool hasSelection = row >= 0 && row < m_services.size() && row < m_statuses.size();
    const ServiceStatus status = hasSelection ? m_statuses.at(row) : ServiceStatus::NotRegistered;

    // During operation, disable all service control buttons
    if (isOperating) {
        m_registerAction->setEnabled(false);
        m_unregisterAction->setEnabled(false);
        m_startAction->setEnabled(false);
        m_stopAction->setEnabled(false);
        m_deleteAction->setEnabled(hasSelection);
        return;
    }

    m_registerAction->setEnabled(hasSelection && status == ServiceStatus::NotRegistered);
    m_unregisterAction->setEnabled(hasSelection && status != ServiceStatus::NotRegistered);
    m_startAction->setEnabled(hasSelection && status == ServiceStatus::Stopped);
    m_stopAction->setEnabled(hasSelection && status == ServiceStatus::Running);
    m_addServerAction->setEnabled(hasSelection);
    m_deleteAction->setEnabled(hasSelection);
}

void ServiceManagerWindow::addService() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add Service"));
    dialog.setMinimumWidth(460);

    QLineEdit nameEdit;
    QLineEdit ipEdit;
    QComboBox userCombo;
    QWidget *pathWidget = new QWidget(&dialog);
    QLineEdit *pathEdit = new QLineEdit(pathWidget);
    QPushButton *browsePathButton = new QPushButton(QStringLiteral("Browse..."), pathWidget);
    QSpinBox portSpin;
    nameEdit.setMinimumWidth(320);
    ipEdit.setMinimumWidth(320);
    nameEdit.setPlaceholderText(QStringLiteral("RunBay"));
    ipEdit.setPlaceholderText(QStringLiteral("127.0.0.1"));
    nameEdit.setText(QStringLiteral("RunBay"));
    ipEdit.setText(QStringLiteral("127.0.0.1"));
    userCombo.addItems(localServiceUsers());
    pathEdit->setMinimumWidth(320);
    pathEdit->setText(defaultServicePath(userCombo.currentText()));
    portSpin.setRange(1, 65535);
    portSpin.setValue(8732);

    QHBoxLayout *pathLayout = new QHBoxLayout(pathWidget);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->addWidget(pathEdit, 1);
    pathLayout->addWidget(browsePathButton);

    QFormLayout form(&dialog);
    form.addRow(QStringLiteral("Name"), &nameEdit);
    form.addRow(QStringLiteral("IP"), &ipEdit);
    form.addRow(QStringLiteral("Port"), &portSpin);
    form.addRow(QStringLiteral("User"), &userCombo);
    form.addRow(QStringLiteral("Path"), pathWidget);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&userCombo, &QComboBox::currentTextChanged, &dialog, [&](const QString &user) {
        pathEdit->setText(defaultServicePath(user));
    });
    connect(browsePathButton, &QPushButton::clicked, &dialog, [&]() {
        const QString path = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Select Service Data Directory"), pathEdit->text());
        if (!path.isEmpty()) {
            pathEdit->setText(path);
        }
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        ServiceConfig service;
        service.name = nameEdit.text().trimmed();
        service.ip = ipEdit.text().trimmed();
        service.user = userCombo.currentText().trimmed();
        service.path = QDir::toNativeSeparators(pathEdit->text().trimmed());
        service.port = portSpin.value();

        QHostAddress parsedAddress;
        if (service.name.isEmpty()) {
            showSelectableWarning(&dialog, QStringLiteral("Add Service"), QStringLiteral("Name is required."));
            return;
        }
        if (!parsedAddress.setAddress(service.ip)) {
            showSelectableWarning(&dialog, QStringLiteral("Add Service"), QStringLiteral("Enter a valid IP address."));
            return;
        }
        if (service.user.isEmpty()) {
            showSelectableWarning(&dialog, QStringLiteral("Add Service"), QStringLiteral("Select a user."));
            return;
        }
        if (service.path.isEmpty()) {
            showSelectableWarning(&dialog, QStringLiteral("Add Service"), QStringLiteral("Path is required."));
            return;
        }

        const QString serviceAccount = serviceAccountName(service.user);
        for (const ServiceConfig &existing : m_services) {
            if (existing.name.compare(service.name, Qt::CaseInsensitive) == 0) {
                showSelectableWarning(&dialog, QStringLiteral("Add Service"),
                                      QStringLiteral("Service name already exists: %1").arg(service.name));
                return;
            }
            if (existing.port == service.port) {
                showSelectableWarning(&dialog, QStringLiteral("Add Service"),
                                      QStringLiteral("Service port already exists: %1").arg(service.port));
                return;
            }
            if (serviceAccountName(existing.user).compare(serviceAccount, Qt::CaseInsensitive) == 0) {
                showSelectableWarning(&dialog, QStringLiteral("Add Service"),
                                      QStringLiteral("Service user already has a service: %1").arg(service.user));
                return;
            }
        }

        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    ServiceConfig service;
    service.name = nameEdit.text().trimmed();
    service.ip = ipEdit.text().trimmed();
    service.user = userCombo.currentText().trimmed();
    service.path = QDir::toNativeSeparators(pathEdit->text().trimmed());
    service.port = portSpin.value();

    m_services.append(service);
    m_statuses.append(queryServiceStatus(service.name));
    saveServices();
    refreshTable();
    m_table->selectRow(m_services.size() - 1);
}

void ServiceManagerWindow::registerSelectedService() {
    if (!ServiceManagerPlatform::supportsInAppControl()) {
        ServiceManagerPlatform::showRegisterHelp(this);
        return;
    }

    cancelCurrentOperation();

    const ServiceConfig service = selectedService();
    if (service.name.isEmpty()) {
        return;
    }

    QString daemonPath = bundledDaemonPath();
    if (daemonPath.isEmpty()) {
        daemonPath = QFileDialog::getOpenFileName(this, QStringLiteral("Select RunBay daemon"),
                                                  QCoreApplication::applicationDirPath(),
                                                  QStringLiteral("Executable (*.exe);;All files (*.*)"));
        if (daemonPath.isEmpty()) {
            return;
        }
    }

    const QString resultFile = generateResultFileName(service.name);
    const QString command = registerServiceCommand(service, daemonPath, resultFile);
    if (command.isEmpty()) {
        return;
    }

    beginElevatedOperation(ServiceOperationType::Register, service.name, command, resultFile,
                           QStringLiteral("Failed to open the service registration prompt."));
}

void ServiceManagerWindow::unregisterSelectedService() {
    if (!ServiceManagerPlatform::supportsInAppControl()) {
        ServiceManagerPlatform::showUnregisterHelp(this);
        return;
    }

    const int row = m_table->currentRow();
    if (row < 0 || row >= m_services.size()) {
        return;
    }
    const ServiceConfig service = m_services.at(row);
    if (QMessageBox::question(this, QStringLiteral("Unregister Service"),
                              QStringLiteral("Unregister service %1?").arg(service.name)) != QMessageBox::Yes) {
        return;
    }

    const QString resultFile = generateResultFileName(service.name);
    beginElevatedOperation(ServiceOperationType::Unregister, service.name,
                           serviceCommand(service.name, QStringLiteral("delete"), resultFile), resultFile,
                           QStringLiteral("Failed to open the service unregister prompt."));
}

void ServiceManagerWindow::startSelectedService() {
    if (!ServiceManagerPlatform::supportsInAppControl()) {
        ServiceManagerPlatform::showStartHelp(this);
        return;
    }

    const ServiceConfig service = selectedService();
    if (service.name.isEmpty()) {
        return;
    }

    const QString resultFile = generateResultFileName(service.name);
    beginElevatedOperation(ServiceOperationType::Start, service.name,
                           serviceCommand(service.name, QStringLiteral("start"), resultFile), resultFile,
                           QStringLiteral("Failed to open the service start prompt."));
}

void ServiceManagerWindow::stopSelectedService() {
    if (!ServiceManagerPlatform::supportsInAppControl()) {
        ServiceManagerPlatform::showStopHelp(this);
        return;
    }

    const ServiceConfig service = selectedService();
    if (service.name.isEmpty()) {
        return;
    }

    const QString resultFile = generateResultFileName(service.name);
    beginElevatedOperation(ServiceOperationType::Stop, service.name,
                           serviceCommand(service.name, QStringLiteral("stop"), resultFile), resultFile,
                           QStringLiteral("Failed to open the service stop prompt."));
}

void ServiceManagerWindow::deleteSelectedService() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_services.size()) {
        return;
    }
    const ServiceConfig service = m_services.at(row);
    if (QMessageBox::question(this, QStringLiteral("Delete Service"),
                              QStringLiteral("Delete service %1?").arg(service.name)) != QMessageBox::Yes) {
        return;
    }
    const bool isRegistered = row < m_statuses.size() && m_statuses.at(row) != ServiceStatus::NotRegistered;
    if (isRegistered) {
        if (ServiceManagerPlatform::supportsInAppControl()) {
            const QString resultFile = generateResultFileName(service.name);
            beginElevatedOperation(ServiceOperationType::Delete, service.name,
                                   serviceCommand(service.name, QStringLiteral("delete"), resultFile), resultFile,
                                   QStringLiteral("Failed to open the service deletion prompt."));
            return;
        }
        if (ServiceManagerPlatform::blockDeletingRegisteredService(this)) {
            return;
        }
    }
    m_services.removeAt(row);
    if (row < m_statuses.size()) {
        m_statuses.removeAt(row);
    }
    saveServices();
    refreshTable();
    if (!m_services.isEmpty()) {
        m_table->selectRow(qMin(row, m_services.size() - 1));
    }
}

void ServiceManagerWindow::addSelectedServiceToServerManager() {
    const ServiceConfig service = selectedService();
    if (service.name.isEmpty()) {
        return;
    }

    emit addServerRequested(service.name, serviceServerUrl(service));
}

ServiceManagerWindow::ServiceConfig ServiceManagerWindow::selectedService() const {
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_services.size()) {
        return {};
    }
    return m_services.at(row);
}

QString ServiceManagerWindow::bundledDaemonPath() const {
    return ServiceManagerPlatform::bundledDaemonPath();
}

bool ServiceManagerWindow::beginElevatedOperation(ServiceOperationType type, const QString &serviceName, const QString &command,
                                                  const QString &resultFile, const QString &promptFailureMessage) {
    cancelCurrentOperation();

    if (!runElevatedCommand(command, resultFile)) {
        QFile::remove(resultFile + QStringLiteral(".service.json"));
        showSelectableWarning(this, QStringLiteral("RunBay"), promptFailureMessage);
        return false;
    }

    startOperation(type, serviceName, resultFile);
    return true;
}

QString ServiceManagerWindow::serviceAddress(const ServiceConfig &service) const {
    QHostAddress parsedAddress;
    parsedAddress.setAddress(service.ip);
    return parsedAddress.protocol() == QAbstractSocket::IPv6Protocol
               ? QStringLiteral("[%1]:%2").arg(service.ip).arg(service.port)
               : QStringLiteral("%1:%2").arg(service.ip).arg(service.port);
}

QString ServiceManagerWindow::serviceServerUrl(const ServiceConfig &service) const {
    QHostAddress parsedAddress;
    parsedAddress.setAddress(service.ip);

    QString host = service.ip;
    if (parsedAddress == QHostAddress::Any || parsedAddress == QHostAddress::AnyIPv4) {
        host = QStringLiteral("127.0.0.1");
    } else if (parsedAddress == QHostAddress::AnyIPv6) {
        host = QStringLiteral("::1");
    }

    return parsedAddress.protocol() == QAbstractSocket::IPv6Protocol
               ? QStringLiteral("http://[%1]:%2").arg(host).arg(service.port)
               : QStringLiteral("http://%1:%2").arg(host).arg(service.port);
}

QStringList ServiceManagerWindow::localServiceUsers() const {
    return ServiceManagerPlatform::localServiceUsers();
}

QString ServiceManagerWindow::defaultServicePath(const QString &user) const {
    return ServiceManagerPlatform::defaultServicePath(user);
}

ServiceManagerWindow::ServiceStatus ServiceManagerWindow::queryServiceStatus(const QString &serviceName) const {
    switch (ServiceManagerPlatform::queryServiceStatus(serviceName)) {
    case ServiceManagerPlatform::Status::Running:
        return ServiceStatus::Running;
    case ServiceManagerPlatform::Status::Stopped:
        return ServiceStatus::Stopped;
    case ServiceManagerPlatform::Status::NotRegistered:
        return ServiceStatus::NotRegistered;
    }
    return ServiceStatus::NotRegistered;
}

QString ServiceManagerWindow::statusText(ServiceStatus status) const {
    switch (status) {
    case ServiceStatus::NotRegistered:
        return QStringLiteral("未注册");
    case ServiceStatus::Stopped:
        return QStringLiteral("未运行");
    case ServiceStatus::Running:
        return QStringLiteral("运行中");
    }
    return QStringLiteral("未注册");
}

QList<ServiceManagerWindow::ServiceConfig> ServiceManagerWindow::loadServices() const {
    QList<ServiceConfig> services;
    const YAML::Node root = AppUtils::loadYamlFile(serviceConfigFilePath());
    const YAML::Node serviceNodes = root[QStringLiteral("services").toStdString()];
    if (!serviceNodes || !serviceNodes.IsSequence()) {
        return services;
    }

    for (const YAML::Node &serviceNode : serviceNodes) {
        if (!serviceNode.IsMap()) {
            continue;
        }

        ServiceConfig service;
        const YAML::Node nameNode = serviceNode[QStringLiteral("name").toStdString()];
        const YAML::Node ipNode = serviceNode[QStringLiteral("ip").toStdString()];
        const YAML::Node userNode = serviceNode[QStringLiteral("user").toStdString()];
        const YAML::Node pathNode = serviceNode[QStringLiteral("path").toStdString()];
        const YAML::Node portNode = serviceNode[QStringLiteral("port").toStdString()];
        if (nameNode && nameNode.IsScalar()) {
            service.name = QString::fromStdString(nameNode.as<std::string>()).trimmed();
        }
        if (ipNode && ipNode.IsScalar()) {
            service.ip = QString::fromStdString(ipNode.as<std::string>()).trimmed();
        }
        if (userNode && userNode.IsScalar()) {
            service.user = QString::fromStdString(userNode.as<std::string>()).trimmed();
        }
        if (pathNode && pathNode.IsScalar()) {
            service.path = QString::fromStdString(pathNode.as<std::string>()).trimmed();
        }
        if (portNode && portNode.IsScalar()) {
            service.port = portNode.as<int>(0);
        }
        if (!service.name.isEmpty() && !service.ip.isEmpty() && !service.user.isEmpty() && !service.path.isEmpty() &&
            service.port > 0) {
            services.append(service);
        }
    }
    return services;
}

void ServiceManagerWindow::saveServices() const {
    YAML::Node root = AppUtils::loadYamlFile(serviceConfigFilePath());
    YAML::Node serviceNodes(YAML::NodeType::Sequence);
    for (const ServiceConfig &service : m_services) {
        YAML::Node serviceNode;
        serviceNode[QStringLiteral("name").toStdString()] = service.name.toStdString();
        serviceNode[QStringLiteral("ip").toStdString()] = service.ip.toStdString();
        serviceNode[QStringLiteral("user").toStdString()] = service.user.toStdString();
        serviceNode[QStringLiteral("path").toStdString()] = service.path.toStdString();
        serviceNode[QStringLiteral("port").toStdString()] = service.port;
        serviceNodes.push_back(serviceNode);
    }
    root[QStringLiteral("services").toStdString()] = serviceNodes;
    AppUtils::saveYamlFile(serviceConfigFilePath(), root);
    saveWindowSettings();
}

QSize ServiceManagerWindow::loadWindowSize() const {
    const YAML::Node root = AppUtils::loadYamlFile(serviceConfigFilePath());
    const YAML::Node windowNode = root[QStringLiteral("window").toStdString()];
    if (!windowNode || !windowNode.IsMap()) {
        return {};
    }

    const YAML::Node widthNode = windowNode[QStringLiteral("width").toStdString()];
    const YAML::Node heightNode = windowNode[QStringLiteral("height").toStdString()];
    const int savedWidth = widthNode && widthNode.IsScalar() ? widthNode.as<int>(0) : 0;
    const int savedHeight = heightNode && heightNode.IsScalar() ? heightNode.as<int>(0) : 0;
    return savedWidth > 0 && savedHeight > 0 ? QSize(savedWidth, savedHeight) : QSize();
}

QList<double> ServiceManagerWindow::loadTableColumnRatios() const {
    QList<double> ratios;
    const YAML::Node root = AppUtils::loadYamlFile(serviceConfigFilePath());
    const YAML::Node ratioNodes = root[QStringLiteral("tableColumnRatios").toStdString()];
    if (!ratioNodes || !ratioNodes.IsSequence()) {
        return ratios;
    }

    for (const YAML::Node &ratioNode : ratioNodes) {
        if (ratioNode && ratioNode.IsScalar()) {
            ratios.append(ratioNode.as<double>(0.0));
        }
    }
    return ratios;
}

void ServiceManagerWindow::applyTableColumnRatios() {
    if (!m_table || m_table->columnCount() <= 0) {
        return;
    }

    QList<double> ratios = m_tableColumnRatios;
    if (ratios.size() != m_table->columnCount()) {
        ratios = {0.16, 0.14, 0.08, 0.16, 0.36, 0.10};
    }

    m_applyingTableColumnRatios = true;
    for (int column = 0; column < m_table->columnCount(); ++column) {
        const int width = qMax(kServiceColumnMinWidths[column], qRound(1000.0 * qMax(0.0, ratios.at(column))));
        m_table->setColumnWidth(column, width);
    }
    m_applyingTableColumnRatios = false;
    resizeServiceColumnsToViewport(m_table->viewport()->width());
}

void ServiceManagerWindow::resizeServiceColumnsToViewport(int viewportWidth) {
    if (m_applyingTableColumnRatios || !m_table || viewportWidth <= 0) {
        return;
    }

    int widths[kServiceColumnCount] = {};
    int totalWidth = 0;
    int minimumTotalWidth = 0;
    for (int column = 0; column < kServiceColumnCount; ++column) {
        widths[column] = qMax(m_table->columnWidth(column), kServiceColumnMinWidths[column]);
        totalWidth += widths[column];
        minimumTotalWidth += kServiceColumnMinWidths[column];
    }
    if (totalWidth <= 0) {
        return;
    }

    const int targetWidth = qMax(viewportWidth, minimumTotalWidth);
    int newWidths[kServiceColumnCount] = {};
    int newTotalWidth = 0;
    for (int column = 0; column < kServiceColumnCount; ++column) {
        newWidths[column] = qMax(kServiceColumnMinWidths[column], qRound(widths[column] * (double(targetWidth) / totalWidth)));
        newTotalWidth += newWidths[column];
    }

    int delta = targetWidth - newTotalWidth;
    while (delta != 0) {
        bool changed = false;
        for (int column = kServiceColumnCount - 1; column >= 0 && delta != 0; --column) {
            if (delta > 0) {
                ++newWidths[column];
                --delta;
                changed = true;
            } else if (newWidths[column] > kServiceColumnMinWidths[column]) {
                --newWidths[column];
                ++delta;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    m_applyingTableColumnRatios = true;
    for (int column = 0; column < kServiceColumnCount; ++column) {
        m_table->setColumnWidth(column, newWidths[column]);
    }
    m_applyingTableColumnRatios = false;
}

void ServiceManagerWindow::resizeTrailingServiceColumnsToViewport(int resizedColumn) {
    if (m_applyingTableColumnRatios || !m_table || resizedColumn < 0 || resizedColumn >= kServiceColumnCount) {
        return;
    }

    int widths[kServiceColumnCount] = {};
    int minimumTotalWidth = 0;
    for (int column = 0; column < kServiceColumnCount; ++column) {
        widths[column] = qMax(m_table->columnWidth(column), kServiceColumnMinWidths[column]);
        minimumTotalWidth += kServiceColumnMinWidths[column];
    }

    const int targetWidth = qMax(m_table->viewport()->width(), minimumTotalWidth);
    int fixedWidth = 0;
    for (int column = 0; column <= resizedColumn; ++column) {
        fixedWidth += widths[column];
    }

    int trailingMinimumWidth = 0;
    int trailingCurrentWidth = 0;
    for (int column = resizedColumn + 1; column < kServiceColumnCount; ++column) {
        trailingMinimumWidth += kServiceColumnMinWidths[column];
        trailingCurrentWidth += widths[column];
    }

    if (resizedColumn == kServiceColumnCount - 1) {
        int previousWidth = 0;
        for (int column = 0; column < resizedColumn; ++column) {
            previousWidth += widths[column];
        }
        widths[resizedColumn] = qMax(kServiceColumnMinWidths[resizedColumn], targetWidth - previousWidth);
    } else if (fixedWidth + trailingMinimumWidth > targetWidth) {
        const int availableForFixed = targetWidth - trailingMinimumWidth;
        for (int column = resizedColumn; column >= 0 && fixedWidth > availableForFixed; --column) {
            const int shrink = qMin(widths[column] - kServiceColumnMinWidths[column], fixedWidth - availableForFixed);
            widths[column] -= shrink;
            fixedWidth -= shrink;
        }
        for (int column = resizedColumn + 1; column < kServiceColumnCount; ++column) {
            widths[column] = kServiceColumnMinWidths[column];
        }
    } else {
        const int remainingWidth = targetWidth - fixedWidth;
        int trailingTotalWidth = 0;
        for (int column = resizedColumn + 1; column < kServiceColumnCount; ++column) {
            widths[column] = qMax(kServiceColumnMinWidths[column],
                                  qRound(widths[column] * (double(remainingWidth) / trailingCurrentWidth)));
            trailingTotalWidth += widths[column];
        }

        int delta = remainingWidth - trailingTotalWidth;
        while (delta != 0) {
            bool changed = false;
            for (int column = kServiceColumnCount - 1; column > resizedColumn && delta != 0; --column) {
                if (delta > 0) {
                    ++widths[column];
                    --delta;
                    changed = true;
                } else if (widths[column] > kServiceColumnMinWidths[column]) {
                    --widths[column];
                    ++delta;
                    changed = true;
                }
            }
            if (!changed) {
                break;
            }
        }
    }

    m_applyingTableColumnRatios = true;
    for (int column = 0; column < kServiceColumnCount; ++column) {
        m_table->setColumnWidth(column, widths[column]);
    }
    m_applyingTableColumnRatios = false;
}

void ServiceManagerWindow::onServiceHeaderSectionResized(int logicalIndex, int oldSize, int newSize) {
    Q_UNUSED(oldSize)
    Q_UNUSED(newSize)

    if (m_applyingTableColumnRatios) {
        return;
    }
    resizeTrailingServiceColumnsToViewport(logicalIndex);
    saveWindowSettings();
}

void ServiceManagerWindow::saveWindowSettings() const {
    YAML::Node root = AppUtils::loadYamlFile(serviceConfigFilePath());

    root[QStringLiteral("window").toStdString()][QStringLiteral("width").toStdString()] = width();
    root[QStringLiteral("window").toStdString()][QStringLiteral("height").toStdString()] = height();

    if (m_table && m_table->columnCount() > 0) {
        int totalWidth = 0;
        for (int column = 0; column < m_table->columnCount(); ++column) {
            totalWidth += m_table->columnWidth(column);
        }
        if (totalWidth > 0) {
            YAML::Node ratioNodes(YAML::NodeType::Sequence);
            for (int column = 0; column < m_table->columnCount(); ++column) {
                ratioNodes.push_back(static_cast<double>(m_table->columnWidth(column)) / totalWidth);
            }
            root[QStringLiteral("tableColumnRatios").toStdString()] = ratioNodes;
        }
    }

    AppUtils::saveYamlFile(serviceConfigFilePath(), root);
}

QString ServiceManagerWindow::generateResultFileName(const QString &serviceName) const {
    const QString hash = QString::number(QRandomGenerator::global()->bounded(100000000, 999999999));
    return QDir::temp().filePath(QStringLiteral("runbay_service_result_%1_%2.txt").arg(serviceName, hash));
}

void ServiceManagerWindow::startOperation(ServiceOperationType type, const QString &serviceName, const QString &resultFile) {
    m_currentOperation.type = type;
    m_currentOperation.serviceName = serviceName;
    m_currentOperation.resultFile = resultFile;
    m_currentOperation.elapsedMs = 0;

    switch (type) {
    case ServiceOperationType::Start:
        m_currentOperation.statusText = QStringLiteral("启动中...");
        break;
    case ServiceOperationType::Stop:
        m_currentOperation.statusText = QStringLiteral("停止中...");
        break;
    case ServiceOperationType::Register:
        m_currentOperation.statusText = QStringLiteral("注册中...");
        break;
    case ServiceOperationType::Unregister:
        m_currentOperation.statusText = QStringLiteral("取消注册中...");
        break;
    case ServiceOperationType::Delete:
        m_currentOperation.statusText = QStringLiteral("删除中...");
        break;
    default:
        m_currentOperation.statusText = QString();
        break;
    }

    updateOperationStatus();
    updateActions();
    m_operationTimer.start(500); // Check every 500ms
}

void ServiceManagerWindow::cancelCurrentOperation() {
    if (m_currentOperation.type != ServiceOperationType::None) {
        m_operationTimer.stop();
        // Remove the result file if it exists
        if (!m_currentOperation.resultFile.isEmpty() && QFileInfo::exists(m_currentOperation.resultFile)) {
            QFile::remove(m_currentOperation.resultFile);
        }
        QFile::remove(m_currentOperation.resultFile + QStringLiteral(".service.json"));
        m_currentOperation.type = ServiceOperationType::None;
        updateActions();
    }
}

void ServiceManagerWindow::checkOperationResult() {
    if (m_currentOperation.type == ServiceOperationType::None) {
        m_operationTimer.stop();
        return;
    }

    m_currentOperation.elapsedMs += 500;

    // Check if result file exists
    if (QFileInfo::exists(m_currentOperation.resultFile)) {
        QFile file(m_currentOperation.resultFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(file.readAll()).trimmed();
            file.close();
            QFile::remove(m_currentOperation.resultFile);
            QFile::remove(m_currentOperation.resultFile + QStringLiteral(".service.json"));

            if (content.startsWith(QStringLiteral("SUCCESS"))) {
                completeOperation(true, QString());
            } else if (content.startsWith(QStringLiteral("ERROR:"))) {
                completeOperation(false, content.mid(6).trimmed());
            } else {
                completeOperation(false, content);
            }
        }
        return;
    }

    // Timeout check (60 seconds)
    if (m_currentOperation.elapsedMs >= 60000) {
        completeOperation(false, QStringLiteral("操作超时"));
    }
}

void ServiceManagerWindow::completeOperation(bool success, const QString &message) {
    m_operationTimer.stop();
    QString operationName;
    switch (m_currentOperation.type) {
    case ServiceOperationType::Start:
        operationName = QStringLiteral("启动");
        break;
    case ServiceOperationType::Stop:
        operationName = QStringLiteral("停止");
        break;
    case ServiceOperationType::Register:
        operationName = QStringLiteral("注册");
        break;
    case ServiceOperationType::Unregister:
        operationName = QStringLiteral("取消注册");
        break;
    case ServiceOperationType::Delete:
        operationName = QStringLiteral("删除");
        break;
    default:
        operationName = QStringLiteral("执行");
        break;
    }

    bool removedLocalService = false;
    if (!success) {
        showSelectableWarning(this, QStringLiteral("RunBay"),
                              QStringLiteral("%1服务失败: %2").arg(operationName, message));
    } else if (m_currentOperation.type == ServiceOperationType::Delete) {
        for (int row = 0; row < m_services.size(); ++row) {
            if (m_services.at(row).name.compare(m_currentOperation.serviceName, Qt::CaseInsensitive) == 0) {
                m_services.removeAt(row);
                if (row < m_statuses.size()) {
                    m_statuses.removeAt(row);
                }
                saveServices();
                removedLocalService = true;
                break;
            }
        }
    }

    m_currentOperation.type = ServiceOperationType::None;
    if (removedLocalService) {
        refreshTable();
    }
    refreshServiceStatuses();
    updateActions();
}

void ServiceManagerWindow::updateOperationStatus() {
    if (m_currentOperation.type != ServiceOperationType::None) {
        emit serviceActionRequested(QStringLiteral("%1 %2...").arg(m_currentOperation.serviceName, m_currentOperation.statusText));
    }
}
