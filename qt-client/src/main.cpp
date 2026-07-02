#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RunBay"));
    QApplication::setOrganizationName(QStringLiteral("RunBay"));

    MainWindow window;
    window.show();

    return app.exec();
}

