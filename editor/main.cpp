#include "AssetPaths.h"
#include "EditorUi.h"
#include "MainWindow.h"

#include <QFileInfo>

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

    applyEditorTheme();

    MainWindow window;
    window.resize(1680, 980);
    window.show();
    if (argc > 1)
        window.openFile(QString::fromLocal8Bit(argv[1]));
    else if (QFileInfo::exists(assetPath("levels/test.level")))
        window.openFile(assetPath("levels/test.level"));
    return app.exec();
}
