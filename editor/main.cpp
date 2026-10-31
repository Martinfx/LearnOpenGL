#include "MainWindow.h"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char** argv)
{
    // Must be set before QApplication so every GL widget gets a 4.5+ core context.
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setApplicationName("FPS Engine Editor");
    QApplication::setOrganizationName("FpsEngine");

    MainWindow window;
    window.resize(1500, 900);
    window.show();
    if (argc > 1)
        window.openFile(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
