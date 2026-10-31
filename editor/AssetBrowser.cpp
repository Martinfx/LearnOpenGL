#include "AssetBrowser.h"
#include "AssetPaths.h"
#include "MaterialEditor.h"

#include <engine/Material.h>

#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace {
constexpr int kThumbSize = 160;
constexpr int kPathRole = Qt::UserRole;
constexpr int kTypeRole = Qt::UserRole + 1;

engine::GlProc offscreenProc(const char* name)
{
    return QOpenGLContext::currentContext()->getProcAddress(name);
}

std::filesystem::path toPath(const QString& s) { return std::filesystem::path(s.toStdU16String()); }

const char* typeName(AssetType t)
{
    switch (t) {
    case AssetType::Model: return "Model";
    case AssetType::Material: return "Material";
    case AssetType::Texture: return "Texture";
    case AssetType::Script: return "Script";
    case AssetType::Weapon: return "Weapon";
    case AssetType::Level: return "Level";
    case AssetType::Other: break;
    }
    return "File";
}
} // namespace

AssetType assetTypeOf(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "gltf" || ext == "glb")
        return AssetType::Model;
    if (ext == "material")
        return AssetType::Material;
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp")
        return AssetType::Texture;
    if (ext == "lua")
        return AssetType::Script;
    if (ext == "weapon")
        return AssetType::Weapon;
    if (ext == "level")
        return AssetType::Level;
    return AssetType::Other;
}

// ------------------------------------------------------------ thumbnails

ThumbnailRenderer::ThumbnailRenderer() = default;

ThumbnailRenderer::~ThumbnailRenderer()
{
    if (ready_ && context_ && context_->makeCurrent(surface_.get())) {
        fbo_.reset();
        renderer_.shutdown();
        context_->doneCurrent();
    }
}

bool ThumbnailRenderer::begin(int size)
{
    if (failed_)
        return false;
    if (!context_) {
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        context_ = std::make_unique<QOpenGLContext>();
        context_->setFormat(format);
        surface_ = std::make_unique<QOffscreenSurface>();
        surface_->setFormat(format);
        surface_->create();
        if (!context_->create() || !context_->makeCurrent(surface_.get())) {
            failed_ = true;
            return false;
        }
        ready_ = renderer_.init(offscreenProc);
        if (!ready_) {
            failed_ = true;
            return false;
        }
    } else if (!context_->makeCurrent(surface_.get())) {
        return false;
    }
    if (!fbo_ || fbo_->width() != size) {
        QOpenGLFramebufferObjectFormat f;
        f.setAttachment(QOpenGLFramebufferObject::Depth);
        fbo_ = std::make_unique<QOpenGLFramebufferObject>(size, size, f);
    }
    fbo_->bind();
    return true;
}

QImage ThumbnailRenderer::finish(const engine::Scene& scene, glm::vec3 target, float radius)
{
    // Three-quarter view from the front right, slightly above.
    const glm::vec3 dir = glm::normalize(glm::vec3(0.75f, 0.55f, 1.0f));
    const float distance = radius / std::sin(glm::radians(20.0f));
    const glm::vec3 eye = target + dir * distance;
    engine::RenderView view;
    view.viewportWidth = fbo_->width();
    view.viewportHeight = fbo_->height();
    view.view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
    view.projection = glm::perspective(glm::radians(40.0f), 1.0f, distance * 0.05f, distance * 4.0f);
    view.cameraPosition = eye;
    renderer_.render(scene, view);
    QImage image = fbo_->toImage();
    fbo_->release();
    context_->doneCurrent();
    return image;
}

QImage ThumbnailRenderer::renderModel(const QString& path, int size)
{
    engine::Model model;
    std::string error;
    if (!engine::loadModelFile(model, toPath(path), &error))
        return {};
    if (!begin(size))
        return {};
    engine::Scene scene;
    scene.sunDirection = {-0.5f, -0.8f, -0.6f};
    const glm::vec3 center = (model.localBounds.min + model.localBounds.max) * 0.5f;
    const float radius = std::max(0.05f, glm::length(model.localBounds.max - model.localBounds.min) * 0.5f);
    scene.models.push_back(std::move(model));
    return finish(scene, center, radius);
}

QImage ThumbnailRenderer::renderMaterial(const QString& path, int size)
{
    std::string error;
    auto material = engine::loadMaterial(toPath(path), &error);
    if (!material || !begin(size))
        return {};
    engine::Scene scene;
    scene.sunDirection = {-0.5f, -0.8f, -0.6f};
    engine::Model sphere;
    sphere.parts.push_back(makePreviewSphere(1.0f));
    sphere.localBounds = {glm::vec3(-1.0f), glm::vec3(1.0f)};
    sphere.overrideMaterials["preview"] = material;
    scene.models.push_back(std::move(sphere));
    return finish(scene, glm::vec3(0.0f), 1.05f);
}

// ------------------------------------------------------------ list

QStringList AssetList::mimeTypes() const
{
    return {"text/uri-list"};
}

QMimeData* AssetList::mimeData(const QList<QListWidgetItem*>& items) const
{
    auto* data = new QMimeData;
    QList<QUrl> urls;
    for (const QListWidgetItem* item : items)
        urls << QUrl::fromLocalFile(item->data(kPathRole).toString());
    data->setUrls(urls);
    return data;
}

// ------------------------------------------------------------ browser

AssetBrowser::AssetBrowser(QWidget* parent) : QWidget(parent)
{
    category_ = new QComboBox(this);
    category_->addItem(tr("All"), -1);
    for (AssetType t : {AssetType::Model, AssetType::Material, AssetType::Texture, AssetType::Script, AssetType::Weapon,
                        AssetType::Level})
        category_->addItem(tr(typeName(t)) + "s", static_cast<int>(t));
    category_->setCurrentIndex(1);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search (name or folder)..."));
    search_->setClearButtonEnabled(true);
    size_ = new QSlider(Qt::Horizontal, this);
    size_->setRange(48, kThumbSize);
    size_->setValue(96);
    size_->setMaximumWidth(120);
    status_ = new QLabel(this);
    auto* rescan = new QPushButton(tr("Rescan"), this);

    list_ = new AssetList(this);
    list_->setViewMode(QListView::IconMode);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setUniformItemSizes(true);
    list_->setWordWrap(true);
    list_->setDragEnabled(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* bar = new QHBoxLayout();
    bar->addWidget(category_);
    bar->addWidget(search_, 1);
    bar->addWidget(new QLabel(tr("Size"), this));
    bar->addWidget(size_);
    bar->addWidget(rescan);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(bar);
    layout->addWidget(list_, 1);
    layout->addWidget(status_);

    auto applySize = [this] {
        const int s = size_->value();
        list_->setIconSize(QSize(s, s));
        list_->setGridSize(QSize(s + 28, s + 38));
    };
    applySize();
    connect(size_, &QSlider::valueChanged, this, applySize);
    connect(category_, &QComboBox::currentIndexChanged, this, &AssetBrowser::applyFilter);
    connect(search_, &QLineEdit::textChanged, this, &AssetBrowser::applyFilter);
    connect(rescan, &QPushButton::clicked, this, &AssetBrowser::rescan);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit assetActivated(item->data(kPathRole).toString(), static_cast<AssetType>(item->data(kTypeRole).toInt()));
    });
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!item)
            return;
        const QString path = item->data(kPathRole).toString();
        status_->setText(QDir::toNativeSeparators(QDir(assetRoot()).relativeFilePath(path)));
        emit assetSelected(path, static_cast<AssetType>(item->data(kTypeRole).toInt()));
    });

    cacheDir_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    QDir().mkpath(cacheDir_);
    connect(&thumbTimer_, &QTimer::timeout, this, &AssetBrowser::makeNextThumbnail);
    QTimer::singleShot(0, this, &AssetBrowser::rescan);
}

AssetBrowser::~AssetBrowser() = default;

QIcon AssetBrowser::placeholderIcon(AssetType type) const
{
    QPixmap pix(kThumbSize, kThumbSize);
    const QColor colors[] = {QColor(70, 120, 80), QColor(120, 90, 50), QColor(70, 90, 130), QColor(110, 70, 110),
                             QColor(130, 60, 60), QColor(60, 110, 120), QColor(90, 90, 90)};
    pix.fill(colors[static_cast<int>(type)]);
    QPainter p(&pix);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(22);
    f.setBold(true);
    p.setFont(f);
    p.drawText(pix.rect(), Qt::AlignCenter, tr(typeName(type)));
    return QIcon(pix);
}

QString AssetBrowser::cacheFile(const QString& path) const
{
    const QFileInfo info(path);
    const QByteArray key = (path + "|" + QString::number(info.lastModified().toMSecsSinceEpoch()) + "|" +
                            QString::number(info.size()) + "|v1")
                               .toUtf8();
    return cacheDir_ + "/" + QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex() + ".png";
}

void AssetBrowser::rescan()
{
    list_->clear();
    const QString root = assetRoot();
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext())
        files << it.next();
    files.sort(Qt::CaseInsensitive);
    for (const QString& file : files) {
        const AssetType type = assetTypeOf(file);
        if (type == AssetType::Other)
            continue;
        const QString rel = QDir(root).relativeFilePath(file);
        if (rel.startsWith("shaders/"))
            continue;
        // Texture sets: "pbr/red_brick/albedo.jpg" reads better as "red_brick albedo".
        const QFileInfo info(file);
        QString label = info.completeBaseName();
        if (type == AssetType::Texture || (type == AssetType::Model && label == info.dir().dirName()))
            label = type == AssetType::Texture ? info.dir().dirName() + " " + label : label;
        auto* item = new QListWidgetItem(label, list_);
        item->setData(kPathRole, file);
        item->setData(kTypeRole, static_cast<int>(type));
        item->setToolTip(QString("%1\n%2").arg(tr(typeName(type)), QDir::toNativeSeparators(rel)));
        item->setIcon(icons_.contains(file) ? icons_[file] : placeholderIcon(type));
    }
    applyFilter();
    thumbTimer_.start(1);
}

void AssetBrowser::applyFilter()
{
    const int type = category_->currentData().toInt();
    const QStringList words = search_->text().toLower().split(' ', Qt::SkipEmptyParts);
    int shown = 0;
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        const QString rel = QDir(assetRoot()).relativeFilePath(item->data(kPathRole).toString()).toLower();
        bool visible = type < 0 || item->data(kTypeRole).toInt() == type;
        for (const QString& w : words)
            visible = visible && rel.contains(w);
        item->setHidden(!visible);
        shown += visible ? 1 : 0;
    }
    status_->setText(tr("%n asset(s)", nullptr, shown));
    thumbTimer_.start(1);
}

// One thumbnail per timer tick, visible items first, so the editor stays responsive.
void AssetBrowser::makeNextThumbnail()
{
    QListWidgetItem* next = nullptr;
    const QRect viewRect = list_->viewport()->rect();
    for (int pass = 0; pass < 2 && !next; ++pass)
        for (int i = 0; i < list_->count(); ++i) {
            QListWidgetItem* item = list_->item(i);
            if (item->isHidden() || icons_.contains(item->data(kPathRole).toString()))
                continue;
            if (pass == 0 && !viewRect.intersects(list_->visualItemRect(item)))
                continue;
            next = item;
            break;
        }
    if (!next || !isVisible()) {
        thumbTimer_.stop();
        return;
    }
    const QString path = next->data(kPathRole).toString();
    const auto type = static_cast<AssetType>(next->data(kTypeRole).toInt());
    QImage image;
    const QString cached = cacheFile(path);
    if (type == AssetType::Model || type == AssetType::Material || type == AssetType::Texture) {
        if (QFileInfo::exists(cached))
            image.load(cached);
        if (image.isNull()) {
            if (!thumbs_)
                thumbs_ = std::make_unique<ThumbnailRenderer>();
            if (type == AssetType::Model)
                image = thumbs_->renderModel(path, kThumbSize);
            else if (type == AssetType::Material)
                image = thumbs_->renderMaterial(path, kThumbSize);
            else {
                QImageReader reader(path);
                const QSize s = reader.size();
                if (s.isValid())
                    reader.setScaledSize(s.scaled(kThumbSize, kThumbSize, Qt::KeepAspectRatio));
                image = reader.read();
            }
            if (!image.isNull())
                image.save(cached);
        }
    }
    const QIcon icon = image.isNull() ? placeholderIcon(type) : QIcon(QPixmap::fromImage(image));
    icons_.insert(path, icon);
    next->setIcon(icon);
}
