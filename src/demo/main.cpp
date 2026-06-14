#include "demo/mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Qt UDP Frame Reassembler"));
    application.setOrganizationName(QStringLiteral("Open Source Reference"));

    MainWindow window;
    window.show();
    return application.exec();
}
