#include "AssetBrowser.h"
#include "AssetPaths.h"
#include "MaterialEditor.h"

#include <engine/Material.h>

#include "EditorUi.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
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
#include <QScrollBar>
#include <QSlider>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <functional>

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
    case AssetType::Sound: return "Sound";
    case AssetType::Audio: return "Audio";
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
    if (ext == "sound")
        return AssetType::Sound;
    if (ext == "wav" || ext == "ogg" || ext == "mp3" || ext == "flac")
        return AssetType::Audio;
    return AssetType::Other;
}

// ------------------------------------------------------------ thumbnails

ThumbnailRenderer::ThumbnailRenderer(QOffscreenSurface* surface) : surface_(surface) {}
ThumbnailRenderer::ThumbnailRenderer() = default;

ThumbnailRenderer::~ThumbnailRenderer()
{
    if (ready_ && context_ && context_->makeCurrent(surface_)) {
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
        if (!surface_) {
            ownSurface_ = std::make_unique<QOffscreenSurface>();
            ownSurface_->setFormat(format);
            ownSurface_->create();
            surface_ = ownSurface_.get();
        }
        if (!context_->create() || !context_->makeCurrent(surface_)) {
            failed_ = true;
            return false;
        }
        ready_ = renderer_.init(offscreenProc);
        if (ready_) {
            engine::RenderQuality quality = renderer_.quality();
            quality.textureUploadsPerFrame = 0; // a thumbnail is a single frame: upload everything
            renderer_.setQuality(quality);
        }
        if (!ready_) {
            failed_ = true;
            return false;
        }
    } else if (!context_->makeCurrent(surface_)) {
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

// ------------------------------------------------------------ worker thread

ThumbnailWorker::~ThumbnailWorker() = default; // GL objects are freed with the renderer on this thread

void ThumbnailWorker::make(const QString& path, int typeValue, const QString& cacheFile)
{
    const auto type = static_cast<AssetType>(typeValue);
    QImage image;
    if (QFileInfo::exists(cacheFile))
        image.load(cacheFile);
    if (image.isNull()) {
        if (type == AssetType::Texture) {
            QImageReader reader(path);
            const QSize s = reader.size();
            if (s.isValid())
                reader.setScaledSize(s.scaled(kThumbSize, kThumbSize, Qt::KeepAspectRatio));
            image = reader.read();
        } else if (type == AssetType::Model || type == AssetType::Material) {
            if (!renderer_)
                renderer_ = std::make_unique<ThumbnailRenderer>(surface_);
            image = type == AssetType::Model ? renderer_->renderModel(path, kThumbSize)
                                             : renderer_->renderMaterial(path, kThumbSize);
        }
        if (!image.isNull())
            image.save(cacheFile);
    }
    emit ready(path, image);
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
    category_->addItem(tr("All types"), -1);
    for (AssetType t : {AssetType::Model, AssetType::Material, AssetType::Texture, AssetType::Script, AssetType::Weapon,
                        AssetType::Level, AssetType::Sound, AssetType::Audio})
        category_->addItem(tr(typeName(t)) + "s", static_cast<int>(t));
    category_->setCurrentIndex(0);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search %1").arg(tr("assets")));
    search_->setClearButtonEnabled(true);
    size_ = new QSlider(Qt::Horizontal, this);
    size_->setRange(48, kThumbSize);
    size_->setValue(88);
    size_->setMaximumWidth(110);
    size_->setToolTip(tr("Thumbnail size"));
    status_ = new QLabel(this);
    status_->setObjectName("dim");

    list_ = new AssetList(this);
    list_->setViewMode(QListView::IconMode);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setUniformItemSizes(true);
    list_->setWordWrap(true);
    list_->setDragEnabled(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setSpacing(4);

    // Tool row: Add, Import, Save All, then the folder path (breadcrumb).
    auto* addButton = new QToolButton(this);
    addButton->setText(tr("Add"));
    addButton->setIcon(editorIcon("add"));
    addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addButton->setPopupMode(QToolButton::InstantPopup);
    auto* addMenu = new QMenu(addButton);
    addMenu->addAction(editorIcon("folder"), tr("New folder..."), this, &AssetBrowser::newFolder);
    addMenu->addSeparator();
    addMenu->addAction(tr("New material"), this, [this] { emit newAssetRequested(QStringLiteral("material"), currentFolderPath()); });
    addMenu->addAction(tr("New Lua script"), this, [this] { emit newAssetRequested(QStringLiteral("script"), currentFolderPath()); });
    addMenu->addAction(tr("New sound"), this, [this] { emit newAssetRequested(QStringLiteral("sound"), currentFolderPath()); });
    addMenu->addAction(tr("New level"), this, [this] { emit newAssetRequested(QStringLiteral("level"), currentFolderPath()); });
    addButton->setMenu(addMenu);
    auto* importButton = new QToolButton(this);
    importButton->setText(tr("Import"));
    importButton->setIcon(editorIcon("import"));
    importButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    importButton->setToolTip(tr("Copy files (glTF, images, scripts...) into the current folder"));
    auto* saveAll = new QToolButton(this);
    saveAll->setText(tr("Save All"));
    saveAll->setIcon(editorIcon("save"));
    saveAll->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    saveAll->setToolTip(tr("Save the level, scripts, weapons and materials"));
    auto* rescanButton = new QToolButton(this);
    rescanButton->setText(tr("Rescan"));
    breadcrumb_ = new QWidget(this);
    auto* crumbs = new QHBoxLayout(breadcrumb_);
    crumbs->setContentsMargins(8, 0, 0, 0);
    crumbs->setSpacing(0);

    auto* tools = new QHBoxLayout();
    tools->setContentsMargins(4, 4, 4, 2);
    tools->addWidget(addButton);
    tools->addWidget(importButton);
    tools->addWidget(saveAll);
    tools->addWidget(breadcrumb_, 1);
    tools->addWidget(rescanButton);

    // Folder tree: favorites, then the asset folders.
    folders_ = new QTreeWidget(this);
    folders_->setHeaderHidden(true);
    folders_->setMinimumWidth(170);
    folders_->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* filterRow = new QHBoxLayout();
    filterRow->setContentsMargins(4, 2, 4, 2);
    filterRow->addWidget(category_);
    filterRow->addWidget(search_, 1);
    filterRow->addWidget(size_);
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addLayout(filterRow);
    rightLayout->addWidget(list_, 1);
    auto* footer = new QHBoxLayout();
    footer->setContentsMargins(6, 2, 6, 2);
    footer->addWidget(status_);
    rightLayout->addLayout(footer);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(folders_);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({200, 900});

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(tools);
    layout->addWidget(split, 1);

    connect(importButton, &QToolButton::clicked, this, &AssetBrowser::importFiles);
    connect(saveAll, &QToolButton::clicked, this, &AssetBrowser::saveAllRequested);
    connect(folders_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!item || rebuildingFolders_)
            return;
        setFolder(item->data(0, Qt::UserRole).toString());
    });
    connect(folders_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = folders_->itemAt(pos);
        if (!item || item->data(0, Qt::UserRole + 1).toBool())
            return;
        const QString folder = item->data(0, Qt::UserRole).toString();
        QMenu menu;
        const bool favorite = favorites_.contains(folder);
        menu.addAction(editorIcon("star"), favorite ? tr("Remove from Favorites") : tr("Add to Favorites"), this, [this, folder, favorite] {
            if (favorite)
                favorites_.removeAll(folder);
            else
                favorites_ << folder;
            QSettings().setValue("content/favorites", favorites_);
            rebuildFolders();
        });
        menu.addAction(editorIcon("folder"), tr("Show in file manager"), this,
                       [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(assetRoot()).filePath(folder))); });
        menu.exec(folders_->viewport()->mapToGlobal(pos));
    });
    connect(list_, &QListWidget::itemSelectionChanged, this, &AssetBrowser::updateStatus);
    favorites_ = QSettings().value("content/favorites").toStringList();

    auto applySize = [this] {
        const int s = size_->value();
        list_->setIconSize(QSize(s, s));
        list_->setGridSize(QSize(s + 28, s + 38));
    };
    applySize();
    connect(size_, &QSlider::valueChanged, this, applySize);
    connect(category_, &QComboBox::currentIndexChanged, this, &AssetBrowser::applyFilter);
    connect(search_, &QLineEdit::textChanged, this, &AssetBrowser::applyFilter);
    connect(rescanButton, &QToolButton::clicked, this, &AssetBrowser::rescan);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit assetActivated(item->data(kPathRole).toString(), static_cast<AssetType>(item->data(kTypeRole).toInt()));
    });
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!item)
            return;
        const QString path = item->data(kPathRole).toString();
        emit assetSelected(path, static_cast<AssetType>(item->data(kTypeRole).toInt()));
    });

    cacheDir_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    QDir().mkpath(cacheDir_);
    // Thumbnails are made on a worker thread; the offscreen surface must be created here (GUI thread).
    surface_ = std::make_unique<QOffscreenSurface>();
    surface_->setFormat(QSurfaceFormat::defaultFormat());
    surface_->create();
    thread_ = new QThread(this);
    thread_->setObjectName("Thumbnails");
    worker_ = new ThumbnailWorker(surface_.get());
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &ThumbnailWorker::ready, this, [this](const QString& path, const QImage& image) {
        requested_.remove(path);
        for (int i = 0; i < list_->count(); ++i)
            if (QListWidgetItem* item = list_->item(i); item->data(kPathRole).toString() == path) {
                const QIcon icon = image.isNull() ? placeholderIcon(static_cast<AssetType>(item->data(kTypeRole).toInt()))
                                                  : QIcon(QPixmap::fromImage(image));
                icons_.insert(path, icon);
                item->setIcon(icon);
                break;
            }
        requestThumbnails();
    });
    thread_->start(QThread::LowPriority);
    connect(&thumbTimer_, &QTimer::timeout, this, &AssetBrowser::requestThumbnails);
    connect(list_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { thumbTimer_.start(50); });
    thumbTimer_.setSingleShot(true);
    QTimer::singleShot(0, this, &AssetBrowser::rescan);
}

AssetBrowser::~AssetBrowser()
{
    thread_->quit();
    thread_->wait();
}

QIcon AssetBrowser::placeholderIcon(AssetType type) const
{
    QPixmap pix(kThumbSize, kThumbSize);
    const QColor colors[] = {QColor(70, 120, 80), QColor(120, 90, 50), QColor(70, 90, 130), QColor(110, 70, 110),
                             QColor(130, 60, 60), QColor(60, 110, 120), QColor(45, 95, 150), QColor(40, 75, 115),
                             QColor(90, 90, 90)};
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
    rebuildFolders();
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
    thumbTimer_.start(0);
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
        // The folder filter includes subfolders; a search looks everywhere.
        if (!folder_.isEmpty() && words.isEmpty())
            visible = visible && rel.startsWith(folder_.toLower() + '/');
        for (const QString& w : words)
            visible = visible && rel.contains(w);
        item->setHidden(!visible);
        shown += visible ? 1 : 0;
    }
    shown_ = shown;
    updateStatus();
    thumbTimer_.start(0);
}

// Keeps a few thumbnail requests in flight, visible items first.
void AssetBrowser::requestThumbnails()
{
    constexpr int kInFlight = 3;
    const QRect viewRect = list_->viewport()->rect();
    for (int pass = 0; pass < 2 && requested_.size() < kInFlight; ++pass)
        for (int i = 0; i < list_->count() && requested_.size() < kInFlight; ++i) {
            QListWidgetItem* item = list_->item(i);
            const QString path = item->data(kPathRole).toString();
            const auto type = static_cast<AssetType>(item->data(kTypeRole).toInt());
            if (item->isHidden() || icons_.contains(path) || requested_.contains(path))
                continue;
            if (pass == 0 && !viewRect.intersects(list_->visualItemRect(item)))
                continue;
            if (type != AssetType::Model && type != AssetType::Material && type != AssetType::Texture) {
                icons_.insert(path, placeholderIcon(type));
                continue;
            }
            requested_.insert(path);
            QMetaObject::invokeMethod(worker_, "make", Qt::QueuedConnection, Q_ARG(QString, path),
                                      Q_ARG(int, static_cast<int>(type)), Q_ARG(QString, cacheFile(path)));
        }
}

// ---------------------------------------------------------------------------
QString AssetBrowser::currentFolderPath() const
{
    return folder_.isEmpty() ? assetRoot() : QDir(assetRoot()).filePath(folder_);
}

void AssetBrowser::updateStatus()
{
    const int selected = static_cast<int>(list_->selectedItems().size());
    QString text = tr("%n item(s)", nullptr, shown_);
    if (selected > 0)
        text += tr(" (%1 selected)").arg(selected);
    if (selected == 1)
        text += QStringLiteral("   ") +
                QDir::toNativeSeparators(QDir(assetRoot()).relativeFilePath(list_->selectedItems().first()->data(kPathRole).toString()));
    status_->setText(text);
}

void AssetBrowser::rebuildFolders()
{
    rebuildingFolders_ = true;
    folders_->clear();
    auto* favorites = new QTreeWidgetItem(folders_, {tr("Favorites")});
    favorites->setIcon(0, editorIcon("star"));
    favorites->setData(0, Qt::UserRole + 1, true);
    favorites->setFlags(Qt::ItemIsEnabled);
    for (const QString& f : favorites_) {
        auto* item = new QTreeWidgetItem(favorites, {QFileInfo(f).fileName()});
        item->setIcon(0, editorIcon("folder"));
        item->setData(0, Qt::UserRole, f);
        item->setToolTip(0, f);
    }
    auto* all = new QTreeWidgetItem(folders_, {tr("All")});
    all->setIcon(0, editorIcon("content"));
    all->setData(0, Qt::UserRole, QString());
    std::function<void(QTreeWidgetItem*, const QString&)> add = [&](QTreeWidgetItem* parent, const QString& rel) {
        QDir dir(QDir(assetRoot()).filePath(rel));
        for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
            const QString child = rel.isEmpty() ? name : rel + '/' + name;
            if (child == "shaders")
                continue;
            auto* item = new QTreeWidgetItem(parent, {name});
            item->setIcon(0, editorIcon("folder"));
            item->setData(0, Qt::UserRole, child);
            add(item, child);
        }
    };
    add(all, QString());
    favorites->setExpanded(true);
    all->setExpanded(true);
    QTreeWidgetItem* current = all;
    for (QTreeWidgetItemIterator it(all); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == folder_)
            current = *it;
    folders_->setCurrentItem(current);
    rebuildingFolders_ = false;
    updateBreadcrumb();
}

void AssetBrowser::setFolder(const QString& folder)
{
    folder_ = folder;
    updateBreadcrumb();
    applyFilter();
    if (!rebuildingFolders_) {
        rebuildingFolders_ = true;
        for (QTreeWidgetItemIterator it(folders_); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toString() == folder && !(*it)->parent()->data(0, Qt::UserRole + 1).toBool()) {
                folders_->setCurrentItem(*it);
                break;
            }
        rebuildingFolders_ = false;
    }
}

void AssetBrowser::updateBreadcrumb()
{
    auto* layout = static_cast<QHBoxLayout*>(breadcrumb_->layout());
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    auto crumb = [&](const QString& label, const QString& folder) {
        auto* b = new QToolButton(breadcrumb_);
        b->setText(label);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, folder] { setFolder(folder); });
        layout->addWidget(b);
    };
    crumb(tr("All"), QString());
    QString path;
    for (const QString& part : folder_.split('/', Qt::SkipEmptyParts)) {
        auto* arrow = new QLabel(QStringLiteral("›"), breadcrumb_);
        arrow->setObjectName("dim");
        layout->addWidget(arrow);
        path = path.isEmpty() ? part : path + '/' + part;
        crumb(part, path);
    }
    layout->addStretch(1);
}

void AssetBrowser::newFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New folder"), tr("Folder name in %1:").arg(folder_.isEmpty() ? tr("assets") : folder_),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (!QDir(currentFolderPath()).mkpath(name)) {
        QMessageBox::warning(this, tr("New folder"), tr("Could not create %1").arg(name));
        return;
    }
    folder_ = folder_.isEmpty() ? name : folder_ + '/' + name;
    rescan();
}

void AssetBrowser::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import into %1").arg(folder_.isEmpty() ? tr("assets") : folder_), QString(),
        tr("Assets (*.glb *.gltf *.bin *.png *.jpg *.jpeg *.material *.lua *.weapon *.level *.sound *.wav *.ogg *.mp3 *.flac);;All files (*)"));
    if (files.isEmpty())
        return;
    QStringList failed;
    for (const QString& file : files) {
        const QString target = QDir(currentFolderPath()).filePath(QFileInfo(file).fileName());
        if (QFileInfo::exists(target) &&
            QMessageBox::question(this, tr("Import"), tr("%1 exists. Replace it?").arg(QFileInfo(target).fileName())) != QMessageBox::Yes)
            continue;
        QFile::remove(target);
        if (!QFile::copy(file, target))
            failed << QFileInfo(file).fileName();
    }
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Import"), tr("Could not copy:\n%1").arg(failed.join('\n')));
    rescan();
}
