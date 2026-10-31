#include "AssetPaths.h"
#include "MainWindow.h"

#include <QFileInfo>

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
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

    // Dark UI with a blue accent (similar to modern level editors), same on every platform.
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QPalette pal;
    const QColor windowColor(43, 45, 48), base(30, 31, 34), text(220, 222, 226), accent(47, 123, 216);
    pal.setColor(QPalette::Window, windowColor);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, QColor(38, 40, 43));
    pal.setColor(QPalette::ToolTipBase, QColor(55, 57, 61));
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, QColor(54, 57, 61));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::BrightText, Qt::white);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Link, accent.lighter(130));
    pal.setColor(QPalette::PlaceholderText, QColor(130, 133, 138));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, QColor(120, 122, 126));
    pal.setColor(QPalette::Disabled, QPalette::Base, QColor(36, 37, 40));
    QApplication::setPalette(pal);
    app.setStyleSheet(
        "QDockWidget::title { background: #232427; padding: 4px 6px; font-weight: bold; }"
        "QToolBar { background: #2b2d30; border: none; spacing: 3px; padding: 2px; }"
        "QToolButton { padding: 4px 7px; border-radius: 3px; }"
        "QToolButton:checked { background: #2f7bd8; color: white; }"
        "QToolButton:hover { background: #3c3f44; }"
        "QTabBar::tab { background: #2b2d30; padding: 5px 12px; border: none; }"
        "QTabBar::tab:selected { background: #1e1f22; border-bottom: 2px solid #2f7bd8; }"
        "QHeaderView::section { background: #2b2d30; border: none; padding: 3px; }"
        "QTreeView, QListView { border: none; }");

    MainWindow window;
    window.resize(1500, 900);
    window.show();
    if (argc > 1)
        window.openFile(QString::fromLocal8Bit(argv[1]));
    else if (QFileInfo::exists(assetPath("levels/test.level")))
        window.openFile(assetPath("levels/test.level"));
    return app.exec();
}
