#include <QApplication>
#include <QLabel>
#include <QMovie>

int main(int argc, char* argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ChiikawaStudyPet"));
    app.setApplicationName(QStringLiteral("DesktopPet"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    app.setQuitOnLastWindowClosed(true);

    QLabel label;
    label.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    label.setAttribute(Qt::WA_TranslucentBackground);
    label.resize(120, 120);

    auto* movie = new QMovie(QStringLiteral(":/assets/chiikawa_skip.gif"), QByteArray(), &label);
    movie->setCacheMode(QMovie::CacheAll);
    movie->setScaledSize(QSize(120, 120));
    label.setMovie(movie);
    movie->start();
    label.show();

    return app.exec();
}
