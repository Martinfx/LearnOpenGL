#include <engine/CpuFeatures.h>
#include <engine/Log.h>
#include <engine/Crash.h>
#include <cstdio>
#include <string>
#include "AssetPaths.h"
#include "EditorUi.h"
#include "MainWindow.h"

#include <QFileInfo>

#include <QApplication>
#include <QSurfaceFormat>

#include <cstdlib>
#include <QTimer>

int main(int argc, char** argv)
{
    if (std::string missing; !engine::checkCpuFeatures(&missing)) {
        std::fprintf(stderr, "This build needs CPU features this processor lacks: %s.\n"
                             "Rebuild on this machine (a clean build dir with the bundled Jolt, ENGINE_USE_SYSTEM_JOLT=OFF).\n",
                     missing.c_str());
        return 1;
    }
    // Must be set before QApplication so every GL widget gets a 4.5+ core context.
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    if (const char* dbg = std::getenv("FPS_ENGINE_GL_DEBUG"); dbg && *dbg && *dbg != '0')
        format.setOption(QSurfaceFormat::DebugContext); // synchronous driver messages in the log ("gl")
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    engine::openLogFile("editor");
    engine::installCrashHandler("editor");
    LOG_INFO("editor", "Version %s, log file: %s", engine::engineVersion(), engine::logFilePath().string().c_str());
    // Qt's own warnings (shader compiles, missing files) go to the engine log as well.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
        const engine::LogLevel level = type == QtDebugMsg || type == QtInfoMsg ? engine::LogLevel::Debug
                                       : type == QtWarningMsg                  ? engine::LogLevel::Warning
                                                                               : engine::LogLevel::Error;
        engine::logMessage(level, "qt", message.toStdString());
    });
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
    QStringList assetsToOpen; // other files on the command line open in their editors (e.g. a .particle)
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.endsWith(".level") && level.isEmpty())
            level = arg;
        else
            assetsToOpen << QFileInfo(arg).absoluteFilePath();
    }
    if (level.isEmpty() && QFileInfo::exists(assetPath("levels/overgrown_street.level")))
        level = assetPath("levels/overgrown_street.level");
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
    if (!assetsToOpen.isEmpty())
        QObject::connect(&window, &MainWindow::loadingFinished, &window, [&window, assetsToOpen] {
            for (const QString& f : assetsToOpen)
                window.openAssetFile(f);
        }, Qt::SingleShotConnection);
    // After a crash, offer the autosaved level once the start-up level is open.
    QObject::connect(&window, &MainWindow::loadingFinished, &window, [&window] { window.offerRecovery(); },
                     Qt::SingleShotConnection);
    if (level.isEmpty())
        QTimer::singleShot(0, &window, [&window] { window.offerRecovery(); });
    const int code = app.exec();
    engine::markCleanExit();
    return code;
}
