#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

// Where the editor reads and writes assets. In a development build that is the
// source tree's assets/ folder (so edits end up in the repository and are not
// overwritten by the build's copy); otherwise the assets/ folder next to the exe.
inline QString assetRoot()
{
#ifdef FPS_SOURCE_ASSETS
    if (QFileInfo(QStringLiteral(FPS_SOURCE_ASSETS)).isDir())
        return QStringLiteral(FPS_SOURCE_ASSETS);
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets"));
}

inline QString assetPath(const QString& relative)
{
    return QDir(assetRoot()).filePath(relative);
}

// Shaders the editor edits: engine/shaders in a development build (embedded into
// the engine on the next build), otherwise shaders/ next to the assets. Files there
// replace the built-in ones of the same name.
inline QString shaderRoot()
{
#ifdef FPS_SOURCE_SHADERS
    if (QFileInfo(QStringLiteral(FPS_SOURCE_SHADERS)).isDir())
        return QStringLiteral(FPS_SOURCE_SHADERS);
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("shaders"));
}
