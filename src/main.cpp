#include "PetWindow.h"
#include "WindowsIdentity.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Chiikawa"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Chiikawa"));
    QCoreApplication::setOrganizationName(QStringLiteral("John Nguyen"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/chiikawa.ico")));
    app.setQuitOnLastWindowClosed(false);

    WindowsIdentity::initialize();

    PetWindow pet;
    pet.startApplication();

    return app.exec();
}
