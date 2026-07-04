#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QList>
#include <QPixmap>
#include <QSize>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QIcon runBayAppIcon() {
    const QPixmap source(QStringLiteral(":/icons/app-icon.png"));
    QIcon icon;
    if (source.isNull()) {
        return icon;
    }

    const QList<QSize> sizes = {
        QSize(16, 16),
        QSize(20, 20),
        QSize(24, 24),
        QSize(32, 32),
        QSize(40, 40),
        QSize(48, 48),
        QSize(64, 64),
        QSize(128, 128),
        QSize(256, 256),
    };
    for (const QSize &size : sizes) {
        icon.addPixmap(source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    return icon;
}

void applyNativeWindowsIcon(QWidget *window) {
#ifdef Q_OS_WIN
    if (!window) {
        return;
    }

    constexpr int kRunBayAppIconResourceId = 101;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!instance || !hwnd) {
        return;
    }

    HICON smallIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kRunBayAppIconResourceId), IMAGE_ICON,
                                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                    LR_DEFAULTCOLOR | LR_SHARED));
    HICON bigIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kRunBayAppIconResourceId), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                                  LR_DEFAULTCOLOR | LR_SHARED));
    if (smallIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
    if (bigIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    }
#else
    Q_UNUSED(window);
#endif
}
} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RunBay"));
    QApplication::setOrganizationName(QStringLiteral("RunBay"));
    const QIcon appIcon = runBayAppIcon();
    QApplication::setWindowIcon(appIcon);

    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();
    applyNativeWindowsIcon(&window);

    return app.exec();
}
