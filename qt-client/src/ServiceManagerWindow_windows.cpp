#include "ServiceManagerWindow.h"

#include "AppUtils.h"
#include "ServiceRuntimeConfig.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QVarLengthArray>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>

namespace {
void showSelectableWarning(QWidget *parent, const QString &title, const QString &text) {
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    for (QLabel *label : box.findChildren<QLabel *>()) {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    }
    box.exec();
}

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
} // namespace

QString ServiceManagerWindow::registerServiceCommand(const ServiceConfig &service, const QString &daemonPath, const QString &resultFile) {
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
    const QString quotedAccount = AppUtils::powershellSingleQuoted(account);
    const QString dataFile = QDir(service.path).filePath(QStringLiteral("tasks.json"));
    const QString logDir = QDir(service.path).filePath(QStringLiteral("logs"));
    const QString configTemp = resultFile + QStringLiteral(".service.json");
    const QString configDestination = QDir(service.path).filePath(QStringLiteral("service.json"));
    QString configError;
    if (!ServiceRuntimeConfig::write(configTemp, ServiceRuntimeConfig::identifier(service.name), service.name,
                                     serviceAddress(service), QDir::toNativeSeparators(dataFile),
                                     QDir::toNativeSeparators(logDir), account, &configError)) {
        showSelectableWarning(this, QStringLiteral("RunBay"),
                              QStringLiteral("Failed to create service configuration: %1").arg(configError));
        return {};
    }
    const QString quotedRootPath = AppUtils::powershellSingleQuoted(service.path);
    const QString quotedLogDir = AppUtils::powershellSingleQuoted(QDir::toNativeSeparators(logDir));
    const QString quotedConfigTemp = AppUtils::powershellSingleQuoted(QDir::toNativeSeparators(configTemp));
    const QString quotedConfigDestination = AppUtils::powershellSingleQuoted(QDir::toNativeSeparators(configDestination));
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
                                "$account=%4; "
                                "$rootPath=%5; "
                                "$logDir=%6; "
                                "$configSource=%7; "
                                "$configFile=%8; "
                                "$binPath='\"' + $daemon + '\" -service-config \"' + $configFile + '\"'; "
                                "try { "
                                "$existing=Get-Service -Name $serviceName -ErrorAction SilentlyContinue; "
                                "if ($existing) { throw ('Service already exists: ' + $serviceName); } "
                                "New-Item -ItemType Directory -Force -Path $rootPath,$logDir | Out-Null; "
                                "Copy-Item -LiteralPath $configSource -Destination $configFile -Force; "
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
                                .arg(quotedResultFile, quotedServiceName, quotedDaemon, quotedAccount, quotedRootPath,
                                     quotedLogDir, quotedConfigTemp, quotedConfigDestination, accountCommand);
    return command;
}

bool ServiceManagerWindow::runElevatedCommand(const QString &command, const QString &resultFile) {
    Q_UNUSED(resultFile)
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
}
