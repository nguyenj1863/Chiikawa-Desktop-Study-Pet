#include "PetWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ChiikawaStudyPet"));
    app.setApplicationName(QStringLiteral("DesktopPet"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    app.setQuitOnLastWindowClosed(false);

    PetWindow pet;
    pet.show();

    return app.exec();
}
