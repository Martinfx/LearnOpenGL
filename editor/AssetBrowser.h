#pragma once
#include <engine/Renderer.h>
#include <engine/Scene.h>

#include <QHash>
#include <QIcon>
#include <QListWidget>
#include <QTimer>
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QSlider;

// Kinds of files the browser shows.
enum class AssetType { Model, Material, Texture, Script, Weapon, Level, Other };
AssetType assetTypeOf(const QString& path);

// Renders model and material thumbnails with the engine renderer in an
// offscreen OpenGL context (same look as the viewport).
class ThumbnailRenderer {
public:
    ThumbnailRenderer();
    ~ThumbnailRenderer();
    QImage renderModel(const QString& path, int size);
    QImage renderMaterial(const QString& path, int size);

private:
    bool begin(int size);
    QImage finish(const engine::Scene& scene, glm::vec3 target, float radius);

    std::unique_ptr<QOffscreenSurface> surface_;
    std::unique_ptr<QOpenGLContext> context_;
    std::unique_ptr<QOpenGLFramebufferObject> fbo_;
    engine::Renderer renderer_;
    bool ready_ = false;
    bool failed_ = false;
};

// Icon grid that drags files out as URLs (to the viewport, material fields...).
class AssetList : public QListWidget {
    Q_OBJECT
public:
    using QListWidget::QListWidget;

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
};

// Browser of everything under assets/: models, materials, textures, scripts,
// weapons and levels with thumbnails (cached on disk). Double-click opens the
// file in its editor or places a model; drag a model into the 3D view to place
// it where you drop it, or a material onto a box or model to assign it.
class AssetBrowser : public QWidget {
    Q_OBJECT
public:
    explicit AssetBrowser(QWidget* parent = nullptr);
    ~AssetBrowser() override;

    void rescan();

signals:
    void assetActivated(const QString& path, AssetType type);
    void assetSelected(const QString& path, AssetType type);

private:
    void applyFilter();
    void makeNextThumbnail();
    QIcon placeholderIcon(AssetType type) const;
    QString cacheFile(const QString& path) const;

    AssetList* list_;
    QComboBox* category_;
    QLineEdit* search_;
    QSlider* size_;
    QLabel* status_;
    QTimer thumbTimer_;
    std::unique_ptr<ThumbnailRenderer> thumbs_;
    QHash<QString, QIcon> icons_;   // path -> thumbnail
    QString cacheDir_;
};
