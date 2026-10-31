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
