#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DucQ"));
    QApplication::setOrganizationName(QStringLiteral("DucQ"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    ducq::MainWindow window;
    window.show();
    return QApplication::exec();
}
