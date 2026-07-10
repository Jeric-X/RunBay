#include "ServiceManagerWindow.h"

#include "AppUtils.h"

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
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QVarLengthArray>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

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

#ifdef Q_OS_WIN
bool promptWindowsCredentials(QWidget *parent, QString &account, QString &password) {
    const std::wstring title = QStringLiteral("RunBay").toStdWString();
    const std::wstring message = QStringLiteral("Service account").toStdWString();

    CREDUI_INFOW info = {};
    info.cbSize = sizeof(info);
    info.hwndParent = parent ? reinterpret_cast<HWND>(parent->winId()) : nullptr;
    info.pszCaptionText = title.c_str();
    info.pszMessageText = message.c_str();

    QByteArray inputBuffer;
    PBYTE inputAuthBuffer = nullptr;
    ULONG inputAuthBufferSize = 0;
    const std::wstring inputAccount = account.toStdWString();
    if (!inputAccount.empty() &&
        !CredPackAuthenticationBufferW(0, const_cast<wchar_t *>(inputAccount.c_str()), const_cast<wchar_t *>(L""),
                                       nullptr, &inputAuthBufferSize) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        inputBuffer.resize(static_cast<int>(inputAuthBufferSize));
        inputAuthBuffer = reinterpret_cast<PBYTE>(inputBuffer.data());
        if (!CredPackAuthenticationBufferW(0, const_cast<wchar_t *>(inputAccount.c_str()), const_cast<wchar_t *>(L""),
                                           inputAuthBuffer, &inputAuthBufferSize)) {
            inputBuffer.fill('\0');
            inputBuffer.clear();
            inputAuthBuffer = nullptr;
            inputAuthBufferSize = 0;
        }
    }

    ULONG authPackage = 0;
    void *outputAuthBuffer = nullptr;
    ULONG outputAuthBufferSize = 0;
    BOOL save = FALSE;
    const DWORD result = CredUIPromptForWindowsCredentialsW(&info, 0, &authPackage, inputAuthBuffer, inputAuthBufferSize,
                                                           &outputAuthBuffer, &outputAuthBufferSize, &save, CREDUIWIN_GENERIC);
    inputBuffer.fill('\0');

    if (result == ERROR_CANCELLED) {
        return false;
    }
    if (result != NO_ERROR) {
        showSelectableWarning(parent, QStringLiteral("RunBay"),
                              QStringLiteral("Windows credential prompt failed with error code: %1").arg(result));
        return false;
    }

    DWORD usernameLength = 0;
    DWORD domainLength = 0;
    DWORD passwordLength = 0;
    CredUnPackAuthenticationBufferW(0, outputAuthBuffer, outputAuthBufferSize, nullptr, &usernameLength, nullptr,
                                    &domainLength, nullptr, &passwordLength);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CoTaskMemFree(outputAuthBuffer);
        showSelectableWarning(parent, QStringLiteral("RunBay"),
                              QStringLiteral("Failed to read credentials from the Windows credential prompt."));
        return false;
    }

    QVarLengthArray<wchar_t> username(qMax<DWORD>(usernameLength, 1));
    QVarLengthArray<wchar_t> domain(qMax<DWORD>(domainLength, 1));
    QVarLengthArray<wchar_t> passwordBuffer(qMax<DWORD>(passwordLength, 1));
    if (!CredUnPackAuthenticationBufferW(0, outputAuthBuffer, outputAuthBufferSize, username.data(), &usernameLength,
                                         domain.data(), &domainLength, passwordBuffer.data(), &passwordLength)) {
        SecureZeroMemory(passwordBuffer.data(), passwordBuffer.size() * sizeof(wchar_t));
        CoTaskMemFree(outputAuthBuffer);
        showSelectableWarning(parent, QStringLiteral("RunBay"),
                              QStringLiteral("Failed to unpack credentials from the Windows credential prompt."));
        return false;
    }

    const QString usernameText = QString::fromWCharArray(username.data()).trimmed();
    const QString domainText = QString::fromWCharArray(domain.data()).trimmed();
    account = domainText.isEmpty() ? usernameText : QStringLiteral("%1\\%2").arg(domainText, usernameText);
    password = QString::fromWCharArray(passwordBuffer.data());

    SecureZeroMemory(passwordBuffer.data(), passwordBuffer.size() * sizeof(wchar_t));
    CoTaskMemFree(outputAuthBuffer);
    return true;
}
#endif

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
#ifndef Q_OS_WIN
    QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Service registration is only available on Windows."));
#else
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
#endif
}

void ServiceManagerWindow::unregisterSelectedService() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Service control is only available on Windows."));
#else
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
#endif
}

void ServiceManagerWindow::startSelectedService() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Service control is only available on Windows."));
#else
    const ServiceConfig service = selectedService();
    if (service.name.isEmpty()) {
        return;
    }

    const QString resultFile = generateResultFileName(service.name);
    beginElevatedOperation(ServiceOperationType::Start, service.name,
                           serviceCommand(service.name, QStringLiteral("start"), resultFile), resultFile,
                           QStringLiteral("Failed to open the service start prompt."));
#endif
}

void ServiceManagerWindow::stopSelectedService() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, QStringLiteral("RunBay"), QStringLiteral("Service control is only available on Windows."));
#else
    const ServiceConfig service = selectedService();
    if (service.name.isEmpty()) {
        return;
    }

    const QString resultFile = generateResultFileName(service.name);
    beginElevatedOperation(ServiceOperationType::Stop, service.name,
                           serviceCommand(service.name, QStringLiteral("stop"), resultFile), resultFile,
                           QStringLiteral("Failed to open the service stop prompt."));
#endif
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
#ifdef Q_OS_WIN
    const bool isRegistered = row < m_statuses.size() && m_statuses.at(row) != ServiceStatus::NotRegistered;
    if (isRegistered) {
        const QString resultFile = generateResultFileName(service.name);
        beginElevatedOperation(ServiceOperationType::Delete, service.name,
                               serviceCommand(service.name, QStringLiteral("delete"), resultFile), resultFile,
                               QStringLiteral("Failed to open the service deletion prompt."));
        return;
    }
#endif
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
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/runbayd.exe"),
        appDir + QStringLiteral("/daemon.exe"),
        appDir + QStringLiteral("/runbay-daemon.exe"),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QString ServiceManagerWindow::registerServiceCommand(const ServiceConfig &service, const QString &daemonPath, const QString &resultFile) {
#ifndef Q_OS_WIN
    Q_UNUSED(service)
    Q_UNUSED(daemonPath)
    Q_UNUSED(resultFile)
    return {};
#else
    const QFileInfo daemonInfo(daemonPath);
    if (!daemonInfo.exists() || !daemonInfo.isFile()) {
        showSelectableWarning(this, QStringLiteral("RunBay"), QStringLiteral("Selected daemon executable does not exist."));
        return {};
    }

    QString account = serviceAccountName(service.user);
    QString password;

    if (serviceAccountNeedsPassword(service.user)) {
        while (true) {
            if (!promptWindowsCredentials(this, account, password)) {
                return {};
            }

            if (account.isEmpty()) {
                showSelectableWarning(this, QStringLiteral("RunBay"), QStringLiteral("Account is required."));
                continue;
            }
            if (password.isEmpty()) {
                showSelectableWarning(this, QStringLiteral("RunBay"), QStringLiteral("Password is required."));
                continue;
            }

            // Verify credentials before proceeding
            if (!verifyAccountPassword(account, password)) {
                showSelectableWarning(this, QStringLiteral("RunBay"),
                                      QStringLiteral("密码验证失败，请检查账户名和密码是否正确。"));
                password.clear();
                continue;
            }
            break;
        }
    }

    const QString quotedServiceName = AppUtils::powershellSingleQuoted(service.name);
    const QString quotedDaemon = AppUtils::powershellSingleQuoted(daemonInfo.absoluteFilePath());
    const QString quotedAddr = AppUtils::powershellSingleQuoted(serviceAddress(service));
    const QString quotedAccount = AppUtils::powershellSingleQuoted(account);
    const QString dataFile = QDir(service.path).filePath(QStringLiteral("tasks.json"));
    const QString logDir = QDir(service.path).filePath(QStringLiteral("logs"));
    const QString quotedRootPath = AppUtils::powershellSingleQuoted(service.path);
    const QString quotedDataFile = AppUtils::powershellSingleQuoted(QDir::toNativeSeparators(dataFile));
    const QString quotedLogDir = AppUtils::powershellSingleQuoted(QDir::toNativeSeparators(logDir));
    const QString quotedResultFile = AppUtils::powershellSingleQuoted(resultFile);
    QString accountCommand = QStringLiteral(" $createArgs += @('obj=', $account); ");
    if (serviceAccountNeedsPassword(service.user) && !password.isEmpty()) {
        accountCommand += QStringLiteral("$password=%1; $createArgs += @('password=', $password); ")
                              .arg(AppUtils::powershellSingleQuoted(password));
    }
    const QString command = QStringLiteral(
                                "$ErrorActionPreference='Stop'; "
                                "$resultFile=%1; "
                                "Remove-Item -Path $resultFile -Force -ErrorAction SilentlyContinue; "
                                "$serviceName=%2; "
                                "$daemon=%3; "
                                "$addr=%4; "
                                "$account=%5; "
                                "$rootPath=%6; "
                                "$dataFile=%7; "
                                "$logDir=%8; "
                                "$binPath='\"' + $daemon + '\" -addr ' + $addr + ' -data \"' + $dataFile + '\" -log-dir \"' + $logDir + '\"'; "
                                "try { "
                                "$existing=Get-Service -Name $serviceName -ErrorAction SilentlyContinue; "
                                "if ($existing) { throw ('Service already exists: ' + $serviceName); } "
                                "New-Item -ItemType Directory -Force -Path $rootPath,$logDir | Out-Null; "
                                "$createArgs=@('create', $serviceName, 'binPath=', $binPath, 'start=', 'auto', 'DisplayName=', $serviceName); "
                                "%9"
                                "$createOutput = & sc.exe @createArgs 2>&1; "
                                "if ($LASTEXITCODE -ne 0) { throw ($createOutput -join [Environment]::NewLine); } "
                                "$descriptionOutput = sc.exe description $serviceName 'RunBay daemon service' 2>&1; "
                                "if ($LASTEXITCODE -ne 0) { throw ($descriptionOutput -join [Environment]::NewLine); } "
                                "$failureOutput = sc.exe failure $serviceName reset= 60 actions= restart/5000/restart/5000/none/0 2>&1; "
                                "if ($LASTEXITCODE -ne 0) { throw ($failureOutput -join [Environment]::NewLine); } "
                                "'SUCCESS' | Out-File -FilePath $resultFile -Encoding utf8; "
                                "} catch { "
                                "('ERROR: ' + $_.Exception.Message) | Out-File -FilePath $resultFile -Encoding utf8; "
                                "}")
                                .arg(quotedResultFile, quotedServiceName, quotedDaemon, quotedAddr, quotedAccount, quotedRootPath,
                                     quotedDataFile, quotedLogDir, accountCommand);
    return command;
#endif
}

bool ServiceManagerWindow::beginElevatedOperation(ServiceOperationType type, const QString &serviceName, const QString &command,
                                                  const QString &resultFile, const QString &promptFailureMessage) {
    cancelCurrentOperation();

    if (!runElevatedPowerShell(command)) {
        showSelectableWarning(this, QStringLiteral("RunBay"), promptFailureMessage);
        return false;
    }

    startOperation(type, serviceName, resultFile);
    return true;
}

bool ServiceManagerWindow::runElevatedPowerShell(const QString &command) {
#ifndef Q_OS_WIN
    Q_UNUSED(command)
    return false;
#else
    const QString elevateCommand = QStringLiteral(
                                       "Start-Process -FilePath 'powershell.exe' "
                                       "-ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-Command',%1) "
                                       "-Verb RunAs")
                                       .arg(AppUtils::powershellSingleQuoted(command));

    return QProcess::startDetached(QStringLiteral("powershell.exe"),
                                   {QStringLiteral("-NoProfile"),
                                    QStringLiteral("-ExecutionPolicy"),
                                    QStringLiteral("Bypass"),
                                    QStringLiteral("-Command"),
                                    elevateCommand});
#endif
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

QString ServiceManagerWindow::serviceCommand(const QString &serviceName, const QString &operation, const QString &resultFile) const {
    const QString quotedServiceName = AppUtils::powershellSingleQuoted(serviceName);
    const bool useResultFile = !resultFile.isEmpty();
    const QString quotedResultFile = useResultFile ? AppUtils::powershellSingleQuoted(resultFile) : QString();

    if (operation == QStringLiteral("start")) {
        if (useResultFile) {
            return QStringLiteral(
                       "$ErrorActionPreference='Stop'; "
                       "$serviceName=%1; "
                       "$resultFile=%2; "
                       "Remove-Item -Path $resultFile -Force -ErrorAction SilentlyContinue; "
                       "$existing=Get-Service -Name $serviceName -ErrorAction SilentlyContinue; "
                       "if (-not $existing) { 'ERROR: Service is not installed: ' + $serviceName | Out-File -FilePath $resultFile -Encoding utf8; exit; } "
                       "if ($existing.Status -eq 'Running') { 'SUCCESS' | Out-File -FilePath $resultFile -Encoding utf8; exit; } "
                       "$output = sc.exe start $serviceName 2>&1; "
                       "if ($LASTEXITCODE -ne 0) { 'ERROR: ' + ($output -join [Environment]::NewLine) | Out-File -FilePath $resultFile -Encoding utf8; } "
                       "else { 'SUCCESS' | Out-File -FilePath $resultFile -Encoding utf8; }")
                .arg(quotedServiceName, quotedResultFile);
        }
        return QStringLiteral(
                   "$ErrorActionPreference='Stop'; "
                   "$serviceName=%1; "
                   "$existing=Get-Service -Name $serviceName -ErrorAction SilentlyContinue; "
                   "if (-not $existing) { throw ('Service is not installed: ' + $serviceName); } "
                   "if ($existing.Status -ne 'Running') { sc.exe start $serviceName | Out-Null; }")
            .arg(quotedServiceName);
    }
    if (operation == QStringLiteral("stop")) {
        if (useResultFile) {
            return QStringLiteral(
                       "$ErrorActionPreference='Stop'; "
                       "$serviceName=%1; "
                       "$resultFile=%2; "
                       "Remove-Item -Path $resultFile -Force -ErrorAction SilentlyContinue; "
                       "$existing=Get-Service -Name $serviceName -ErrorAction SilentlyContinue; "
                       "if (-not $existing) { 'ERROR: Service is not installed: ' + $serviceName | Out-File -FilePath $resultFile -Encoding utf8; exit; } "
                       "if ($existing.Status -eq 'Stopped') { 'SUCCESS' | Out-File -FilePath $resultFile -Encoding utf8; exit; } "
                       "$output = sc.exe stop $serviceName 2>&1; "
                       "if ($LASTEXITCODE -ne 0) { 'ERROR: ' + ($output -join [Environment]::NewLine) | Out-File -FilePath $resultFile -Encoding utf8; } "
                       "else { 'SUCCESS' | Out-File -FilePath $resultFile -Encoding utf8; }")
                .arg(quotedServiceName, quotedResultFile);
        }
        return QStringLiteral(
                   "$ErrorActionPreference='Stop'; "
                   "$serviceName=%1; "
                   "$existing=Get-Service -Name $serviceName -ErrorAction SilentlyContinue; "
                   "if (-not $existing) { throw ('Service is not installed: ' + $serviceName); } "
                   "if ($existing.Status -ne 'Stopped') { sc.exe stop $serviceName | Out-Null; }")
            .arg(quotedServiceName);
    }
    if (useResultFile) {
        return QStringLiteral(
                   "$ErrorActionPreference='Stop'; "
                   "$serviceName=%1; "
                   "$resultFile=%2; "
                   "Remove-Item -Path $resultFile -Force -ErrorAction SilentlyContinue; "
                   "try { "
                   "$existing=Get-Service -Name $serviceName -ErrorAction SilentlyContinue; "
                   "if (-not $existing) { 'SUCCESS' | Out-File -FilePath $resultFile -Encoding utf8; exit; } "
                   "if ($existing -and $existing.Status -ne 'Stopped') { "
                   "  $stopOutput = sc.exe stop $serviceName 2>&1; "
                   "  if ($LASTEXITCODE -ne 0) { throw ($stopOutput -join [Environment]::NewLine); } "
                   "  Start-Sleep -Milliseconds 700; "
                   "} "
                   "$deleteOutput = sc.exe delete $serviceName 2>&1; "
                   "if ($LASTEXITCODE -ne 0) { throw ($deleteOutput -join [Environment]::NewLine); } "
                   "'SUCCESS' | Out-File -FilePath $resultFile -Encoding utf8; "
                   "} catch { "
                   "('ERROR: ' + $_.Exception.Message) | Out-File -FilePath $resultFile -Encoding utf8; "
                   "}")
            .arg(quotedServiceName, quotedResultFile);
    }

    return QStringLiteral(
               "$ErrorActionPreference='Stop'; "
               "$serviceName=%1; "
               "$existing=Get-Service -Name $serviceName -ErrorAction SilentlyContinue; "
               "if (-not $existing) { exit 0; } "
               "if ($existing.Status -ne 'Stopped') { sc.exe stop $serviceName | Out-Null; Start-Sleep -Milliseconds 700; } "
               "sc.exe delete $serviceName | Out-Null;")
        .arg(quotedServiceName);
}

QStringList ServiceManagerWindow::localServiceUsers() const {
    QStringList users = {
        QStringLiteral("LocalSystem"),
        QStringLiteral("NT AUTHORITY\\LocalService"),
        QStringLiteral("NT AUTHORITY\\NetworkService"),
    };

#ifdef Q_OS_WIN
    QProcess query;
    query.start(QStringLiteral("powershell.exe"),
                {QStringLiteral("-NoProfile"),
                 QStringLiteral("-ExecutionPolicy"),
                 QStringLiteral("Bypass"),
                 QStringLiteral("-Command"),
                 QStringLiteral("Get-LocalUser | Where-Object { $_.Enabled } | Select-Object -ExpandProperty Name")});
    if (query.waitForFinished(1200) && query.exitCode() == 0) {
        const QString output = QString::fromLocal8Bit(query.readAllStandardOutput());
        for (const QString &line : output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
            const QString user = line.trimmed();
            if (!user.isEmpty() && !users.contains(user, Qt::CaseInsensitive)) {
                users.append(user);
            }
        }
    } else {
        query.kill();
        const QString currentUser = qEnvironmentVariable("USERNAME").trimmed();
        if (!currentUser.isEmpty() && !users.contains(currentUser, Qt::CaseInsensitive)) {
            users.append(currentUser);
        }
    }
#endif

    return users;
}

QString ServiceManagerWindow::serviceAccountName(const QString &user) const {
    if (user.compare(QStringLiteral("LocalSystem"), Qt::CaseInsensitive) == 0 ||
        user.startsWith(QStringLiteral("NT AUTHORITY\\"), Qt::CaseInsensitive) ||
        user.contains(QLatin1Char('\\'))) {
        return user;
    }
    return QStringLiteral(".\\%1").arg(user);
}

bool ServiceManagerWindow::serviceAccountNeedsPassword(const QString &user) const {
    return user.compare(QStringLiteral("LocalSystem"), Qt::CaseInsensitive) != 0 &&
           !user.startsWith(QStringLiteral("NT AUTHORITY\\"), Qt::CaseInsensitive);
}

bool ServiceManagerWindow::verifyAccountPassword(const QString &account, const QString &password) const {
#ifndef Q_OS_WIN
    Q_UNUSED(account)
    Q_UNUSED(password)
    return true;
#else
    // Parse account: could be ".\username", "DOMAIN\username", or just "username"
    QString domain;
    QString username = account;

    const int slashPos = account.indexOf(QLatin1Char('\\'));
    if (slashPos >= 0) {
        domain = account.left(slashPos);
        username = account.mid(slashPos + 1);
    }
    // If domain is "." or empty, use local computer name
    if (domain.isEmpty() || domain == QStringLiteral(".")) {
        domain = qEnvironmentVariable("COMPUTERNAME");
    }

    // Use PowerShell to verify credentials via Windows API LogonUser
    const QString quotedDomain = AppUtils::powershellSingleQuoted(domain);
    const QString quotedUsername = AppUtils::powershellSingleQuoted(username);
    const QString quotedPassword = AppUtils::powershellSingleQuoted(password);

    const QString command = QStringLiteral(
        "$ErrorActionPreference='Stop'; "
        "$domain=%1; $username=%2; $password=%3; "
        "$signature='[DllImport(\"advapi32.dll\", SetLastError=true)] public static extern bool LogonUser(string lpszUsername, string lpszDomain, string lpszPassword, int dwLogonType, int dwLogonProvider, ref IntPtr phToken);'; "
        "$type=Add-Type -MemberDefinition $signature -Name 'Win32Logon' -Namespace 'Win32' -PassThru; "
        "$token=[IntPtr]::Zero; "
        "$result=$type::LogonUser($username, $domain, $password, 2, 0, [ref]$token); "
        "if ($result -and $token -ne [IntPtr]::Zero) { "
        "  $closeSig='[DllImport(\"kernel32.dll\", SetLastError=true)] public static extern bool CloseHandle(IntPtr hObject);'; "
        "  $closeType=Add-Type -MemberDefinition $closeSig -Name 'Win32Close' -Namespace 'Win32' -PassThru; "
        "  $closeType::CloseHandle($token); "
        "  exit 0; "
        "} else { "
        "  $err=[System.Runtime.InteropServices.Marshal]::GetLastWin32Error(); "
        "  Write-Error ('LogonUser failed with error code: ' + $err); "
        "  exit 1; "
        "}"
    ).arg(quotedDomain, quotedUsername, quotedPassword);

    QProcess process;
    process.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"),
                   QStringLiteral("-ExecutionPolicy"),
                   QStringLiteral("Bypass"),
                   QStringLiteral("-Command"),
                   command});

    if (!process.waitForFinished(5000)) {
        process.kill();
        return false;
    }

    return process.exitCode() == 0;
#endif
}

QString ServiceManagerWindow::defaultServicePath(const QString &user) const {
#ifdef Q_OS_WIN
    if (!serviceAccountNeedsPassword(user)) {
        const QString programData = qEnvironmentVariable("ProgramData");
        return QDir::toNativeSeparators(QDir(programData.isEmpty() ? QStringLiteral("C:/ProgramData") : programData)
                                            .filePath(QStringLiteral("RunBayd")));
    }

    QString userName = user;
    const int slash = userName.lastIndexOf(QLatin1Char('\\'));
    if (slash >= 0) {
        userName = userName.mid(slash + 1);
    }
    return QDir::toNativeSeparators(QStringLiteral("C:/Users/%1/AppData/Roaming/RunBayd").arg(userName));
#else
    Q_UNUSED(user)
    return AppUtils::appDataDirectoryPath();
#endif
}

ServiceManagerWindow::ServiceStatus ServiceManagerWindow::queryServiceStatus(const QString &serviceName) const {
#ifndef Q_OS_WIN
    Q_UNUSED(serviceName)
    return ServiceStatus::NotRegistered;
#else
    QProcess query;
    query.start(QStringLiteral("sc.exe"), {QStringLiteral("query"), serviceName});
    if (!query.waitForFinished(700)) {
        query.kill();
        return ServiceStatus::NotRegistered;
    }

    const QString output = QString::fromLocal8Bit(query.readAllStandardOutput() + query.readAllStandardError());
    if (query.exitCode() != 0) {
        return ServiceStatus::NotRegistered;
    }
    if (output.contains(QStringLiteral("RUNNING"), Qt::CaseInsensitive)) {
        return ServiceStatus::Running;
    }
    return ServiceStatus::Stopped;
#endif
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
