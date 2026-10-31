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

    // Splash with progress until the editor is up and the first level has loaded.
    EditorSplash splash(assetPath("textures/ui/loading_zone.jpg"));
    splash.show();
    splash.setProgress(2, QObject::tr("Starting the editor"));

    MainWindow window;
    window.resize(1680, 980);
    splash.setProgress(8, QObject::tr("Starting the renderer"));
    window.show();
    splash.raise();
    QString level;
    if (argc > 1)
        level = QString::fromLocal8Bit(argv[1]);
    else if (QFileInfo::exists(assetPath("levels/test.level")))
        level = assetPath("levels/test.level");
    if (level.isEmpty()) {
        splash.finish(&window);
    } else {
        // Level progress fills the rest of the bar.
        const auto progress = QObject::connect(&window, &MainWindow::loadingProgress, &splash, [&splash, &level](int percent, const QString& step) {
            splash.setProgress(10 + percent * 90 / 100,
                               step.isEmpty() ? QObject::tr("Opening %1").arg(QFileInfo(level).fileName())
                                              : QObject::tr("Loading %1").arg(step));
        });
        QObject::connect(&window, &MainWindow::loadingFinished, &splash, [&splash, &window, progress] {
            QObject::disconnect(progress);
            splash.finish(&window);
        },
                         Qt::SingleShotConnection);
        window.openFile(level);
    }
    return app.exec();
}
