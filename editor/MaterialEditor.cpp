#include "MaterialEditor.h"
#include "AssetPaths.h"
#include <engine/Surfaces.h>

#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace {

engine::GlProc qtGetProc(const char* name)
{
    return QOpenGLContext::currentContext()->getProcAddress(name);
}

QString colorStyle(glm::vec3 c)
{
    return QString("background-color: %1; min-height: 22px;").arg(QColor::fromRgbF(c.r, c.g, c.b).name());
}

std::filesystem::path toPath(const QString& s)
{
    return std::filesystem::path(s.toStdU16String());
}

QString fromPath(const std::filesystem::path& p)
{
    return QString::fromStdU16String(p.u16string());
}

} // namespace

// UV sphere with texture coordinates (2 x 1 repeats around / top to bottom).
engine::MeshPart makePreviewSphere(float radius)
{
    engine::MeshPart part;
    const int rings = 32, segments = 48;
    for (int r = 0; r <= rings; ++r) {
        const float v = static_cast<float>(r) / rings;
        const float phi = v * glm::pi<float>();
        for (int s = 0; s <= segments; ++s) {
            const float u = static_cast<float>(s) / segments;
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            part.positions.push_back(n * radius);
            part.normals.push_back(n);
            part.texcoords.push_back({u * 2.0f, v});
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segments; ++s) {
            const std::uint32_t a = static_cast<std::uint32_t>(r * (segments + 1) + s);
            const std::uint32_t b = a + static_cast<std::uint32_t>(segments + 1);
            part.indices.insert(part.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    part.id = engine::newResourceId();
    part.materialName = "preview";
    return part;
}

// ---------------------------------------------------------------- preview

MaterialPreview::MaterialPreview(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(260, 220);
    engine::Model sphere;
    sphere.name = "Sphere";
    sphere.parts.push_back(makePreviewSphere(0.9f));
    sphere.position = {-1.1f, 1.0f, 0.0f};
    scene_.models.push_back(std::move(sphere));
    scene_.boxes.emplace_back("Cube", glm::vec3(1.1f, 1.0f, 0.0f), glm::vec3(1.5f), glm::vec3(1.0f));
    scene_.boxes.emplace_back("Ground", glm::vec3(0.0f, -0.05f, 0.0f), glm::vec3(8.0f, 0.1f, 8.0f), glm::vec3(0.35f));
    scene_.sunDirection = {-0.5f, -0.8f, -0.6f};
    connect(&timer_, &QTimer::timeout, this, [this] { update(); });
    timer_.start(33);
    clock_.start();
}

MaterialPreview::~MaterialPreview()
{
    cleanup();
}

void MaterialPreview::cleanup()
{
    if (!ready_)
        return;
    makeCurrent();
    renderer_.shutdown();
    doneCurrent();
    ready_ = false;
}

void MaterialPreview::setMaterial(std::shared_ptr<engine::Material> material)
{
    scene_.models[0].overrideMaterials["preview"] = material;
    scene_.boxes[0].material = std::move(material);
    update();
}

void MaterialPreview::initializeGL()
{
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &MaterialPreview::cleanup, Qt::UniqueConnection);
    ready_ = renderer_.init(qtGetProc);
}

void MaterialPreview::paintGL()
{
    if (!ready_ || !isVisible())
        return;
    // The camera orbits slowly so highlights and tiling are visible from all sides.
    const float t = static_cast<float>(clock_.elapsed()) / 1000.0f * 0.35f;
    const glm::vec3 target(0.0f, 0.9f, 0.0f);
    const glm::vec3 eye = target + glm::vec3(std::sin(t) * 5.5f, 2.0f, std::cos(t) * 5.5f);
    engine::RenderView view;
    view.view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const float dpr = static_cast<float>(devicePixelRatioF());
    view.viewportWidth = static_cast<int>(width() * dpr);
    view.viewportHeight = static_cast<int>(height() * dpr);
    view.projection = glm::perspective(glm::radians(45.0f), static_cast<float>(width()) / std::max(1, height()), 0.1f, 50.0f);
    view.cameraPosition = eye;
    renderer_.render(scene_, view);
}

// ---------------------------------------------------------------- editor

MaterialEditor::MaterialEditor(QWidget* parent)
    : QWidget(parent)
{
    // Left: material files.
    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    list_ = new QListWidget(left);
    auto* buttons = new QHBoxLayout();
    auto* newButton = new QPushButton(tr("New"), left);
    auto* dupButton = new QPushButton(tr("Duplicate"), left);
    auto* openButton = new QPushButton(tr("Open..."), left);
    auto* instanceButton = new QPushButton(tr("New instance"), left);
    instanceButton->setToolTip(tr("A material that takes every value from the selected one except those you change on it "
                                  "(rust variants of one metal, colours of one paint)"));
    buttons->addWidget(newButton);
    buttons->addWidget(dupButton);
    buttons->addWidget(instanceButton);
    buttons->addWidget(openButton);
    leftLayout->addWidget(new QLabel(tr("Materials (%1)").arg(QDir::toNativeSeparators(assetPath("materials"))), left));
    leftLayout->addWidget(list_, 1);
    leftLayout->addLayout(buttons);

    // Right: values + preview.
    form_ = new QWidget(this);
    auto* form = new QFormLayout(form_);
    nameEdit_ = new QLineEdit(form_);
    colorButton_ = new QPushButton(form_);
    // One row per texture slot: file name, browse, clear, open in the texture editor.
    std::vector<QWidget*> mapRows;
    for (int i = 0; i < engine::kMapCount; ++i) {
        const auto kind = static_cast<engine::MapKind>(i);
        auto* row = new QWidget(form_);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(row);
        edit->setReadOnly(true);
        edit->setPlaceholderText(tr("none"));
        auto* browse = new QPushButton(tr("..."), row);
        auto* clear = new QPushButton(tr("x"), row);
        auto* editButton = new QPushButton(tr("Edit"), row);
        browse->setFixedWidth(32);
        clear->setFixedWidth(32);
        rowLayout->addWidget(edit, 1);
        rowLayout->addWidget(browse);
        rowLayout->addWidget(clear);
        rowLayout->addWidget(editButton);
        mapEdits_[i] = edit;
        mapEditButtons_[i] = editButton;
        mapRows.push_back(row);
        connect(browse, &QPushButton::clicked, this, [this, kind] {
            if (!material_)
                return;
            const auto& current = material_->mapPath(kind);
            const QString start = current.empty() ? assetPath("textures") : fromPath(current);
            const QString path = QFileDialog::getOpenFileName(this, tr(engine::mapLabel(kind)), start,
                                                              tr("Images (*.png *.jpg *.jpeg)"));
            if (path.isEmpty())
                return;
            std::string error;
            const engine::Material before = *material_;
            if (!engine::setMaterialMap(*material_, kind, toPath(path), &error)) {
                QMessageBox::warning(this, tr("Texture"), QString::fromStdString(error));
                return;
            }
            afterEdit(before);
        });
        connect(clear, &QPushButton::clicked, this, [this, kind] {
            if (!material_ || material_->mapPath(kind).empty())
                return;
            const engine::Material before = *material_;
            engine::setMaterialMap(*material_, kind, {});
            afterEdit(before);
        });
        connect(editButton, &QPushButton::clicked, this, [this, kind] {
            if (material_ && !material_->mapPath(kind).empty())
                emit editTexture(fromPath(material_->mapPath(kind)));
        });
    }
    auto makeSpin = [this](double min, double max, double step, int decimals) {
        auto* s = new QDoubleSpinBox(form_);
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(decimals);
        s->setKeyboardTracking(false);
        connect(s, &QDoubleSpinBox::valueChanged, this, &MaterialEditor::edited);
        return s;
    };
    auto* tilingRow = new QWidget(form_);
    auto* tilingLayout = new QHBoxLayout(tilingRow);
    tilingLayout->setContentsMargins(0, 0, 0, 0);
    tilingX_ = makeSpin(0.01, 100.0, 0.05, 3);
    tilingY_ = makeSpin(0.01, 100.0, 0.05, 3);
    tilingX_->setPrefix("U ");
    tilingY_->setPrefix("V ");
    tilingLayout->addWidget(tilingX_);
    tilingLayout->addWidget(tilingY_);
    roughness_ = makeSpin(0.0, 1.0, 0.05, 2);
    metallic_ = makeSpin(0.0, 1.0, 0.05, 2);
    normalStrength_ = makeSpin(0.0, 4.0, 0.1, 2);
    detailTiling_ = makeSpin(0.5, 64.0, 0.5, 2);
    detailStrength_ = makeSpin(0.0, 1.0, 0.05, 2);
    detailDistance_ = makeSpin(1.0, 200.0, 1.0, 1);
    detailDistance_->setSuffix(" m");
    heightDepth_ = makeSpin(0.0, 0.2, 0.005, 3);
    heightDepth_->setSuffix(" m");
    heightDepth_->setToolTip(tr("How deep the black of the height map lies under the white (bricks ~0.02 m, cobbles ~0.03 m). "
                                "Shifts the texture with the view angle up close (parallax); 0 = off"));
    detailTiling_->setToolTip(tr("Detail map repeats per repeat of the base maps (close-up grain, scratches, pores)"));
    detailStrength_->setToolTip(tr("How strongly the detail maps show (grey 0.5 = no change)"));
    detailDistance_->setToolTip(tr("The detail fades out towards this distance from the camera"));
    emissiveButton_ = new QPushButton(form_);
    worldUv_ = new QCheckBox(tr("Project texture in world space (ignores model UVs)"), form_);
    alphaMode_ = new QComboBox(form_);
    alphaMode_->addItems({tr("Opaque"), tr("Mask (cut-out: leaves, fences, grates)"), tr("Blend (see-through: glass)")});
    alphaMode_->setToolTip(tr("How the alpha of the color map (times Opacity) is used"));
    alphaCutoff_ = makeSpin(0.0, 1.0, 0.05, 2);
    alphaCutoff_->setToolTip(tr("Mask: pixels with alpha below this are cut away"));
    opacity_ = makeSpin(0.0, 1.0, 0.05, 2);
    opacity_->setToolTip(tr("Multiplies the alpha of the color map (Blend: 0 = invisible, 1 = as the map)"));
    doubleSided_ = new QCheckBox(tr("Double-sided (draw back faces: leaves, thin fences, cloth)"), form_);
    surface_ = new QComboBox(form_);
    surface_->setToolTip(tr("What the surface is made of: footstep and bullet impact sounds, bullet holes (Tools > Surfaces)"));
    surface_->addItem(tr("Auto"), QString());
    for (const std::string& s : engine::standardSurfaceNames())
        surface_->addItem(QString::fromStdString(s), QString::fromStdString(s));
    pathLabel_ = new QLabel(form_);
    pathLabel_->setWordWrap(true);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* saveButton = new QPushButton(tr("Save"), form_);
    auto* revertButton = new QPushButton(tr("Revert"), form_);
    auto* saveRow = new QHBoxLayout();
    saveRow->addWidget(saveButton);
    saveRow->addWidget(revertButton);

    // Instance: the parent material, and a way back to its values.
    auto* parentRow = new QWidget(form_);
    auto* parentLayout = new QHBoxLayout(parentRow);
    parentLayout->setContentsMargins(0, 0, 0, 0);
    parentEdit_ = new QLineEdit(parentRow);
    parentEdit_->setReadOnly(true);
    parentEdit_->setPlaceholderText(tr("none (standalone material)"));
    auto* parentBrowse = new QPushButton(tr("..."), parentRow);
    auto* parentClear = new QPushButton(tr("x"), parentRow);
    parentBrowse->setFixedWidth(32);
    parentClear->setFixedWidth(32);
    parentBrowse->setToolTip(tr("Make this an instance of another material (its values are replaced by the parent's)"));
    parentClear->setToolTip(tr("Detach from the parent: keeps the current values as its own"));
    resetOverrides_ = new QPushButton(tr("Reset to parent"), parentRow);
    resetOverrides_->setToolTip(tr("Drop every value changed on this instance (bold labels)"));
    parentLayout->addWidget(parentEdit_, 1);
    parentLayout->addWidget(parentBrowse);
    parentLayout->addWidget(parentClear);
    parentLayout->addWidget(resetOverrides_);
    connect(parentBrowse, &QPushButton::clicked, this, [this] {
        if (!material_)
            return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Parent material"), assetPath("materials"), tr("Materials (*.material)"));
        if (path.isEmpty())
            return;
        std::string error;
        if (!engine::setMaterialParent(*material_, toPath(path), &error)) {
            QMessageBox::warning(this, tr("Parent material"), QString::fromStdString(error));
            return;
        }
        modified_.insert(currentPath());
        refreshForm();
        emit materialChanged();
    });
    connect(parentClear, &QPushButton::clicked, this, [this] {
        if (!material_ || !material_->parent)
            return;
        engine::setMaterialParent(*material_, {});
        modified_.insert(currentPath());
        refreshForm();
        emit materialChanged();
    });
    connect(resetOverrides_, &QPushButton::clicked, this, [this] {
        if (!material_ || !material_->parent || material_->overrides.empty())
            return;
        material_->overrides.clear();
        engine::applyMaterialParent(*material_);
        engine::refreshMaterialInstances();
        modified_.insert(currentPath());
        refreshForm();
        emit materialChanged();
    });
    connect(instanceButton, &QPushButton::clicked, this, &MaterialEditor::newInstance);

    // Blend layers: materials painted onto models in Mesh paint mode (red = layer 1, green = layer 2).
    QWidget* layerRows[2] = {};
    for (int i = 0; i < 2; ++i) {
        auto* row = new QWidget(form_);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        layerEdits_[i] = new QLineEdit(row);
        layerEdits_[i]->setReadOnly(true);
        layerEdits_[i]->setPlaceholderText(tr("none"));
        auto* browse = new QPushButton(tr("..."), row);
        auto* clear = new QPushButton(tr("x"), row);
        browse->setFixedWidth(32);
        clear->setFixedWidth(32);
        browse->setToolTip(tr("Material painted over this one where models are painted %1 (Mesh paint mode)")
                               .arg(i == 0 ? tr("red (layer 1)") : tr("green (layer 2)")));
        clear->setToolTip(tr("No blend layer"));
        rowLayout->addWidget(layerEdits_[i], 1);
        rowLayout->addWidget(browse);
        rowLayout->addWidget(clear);
        layerRows[i] = row;
        connect(browse, &QPushButton::clicked, this, [this, i] {
            if (!material_)
                return;
            const QString path = QFileDialog::getOpenFileName(this, tr("Blend layer material"), assetPath("materials"), tr("Materials (*.material)"));
            if (path.isEmpty())
                return;
            const engine::Material before = *material_;
            std::string error;
            if (!engine::setMaterialLayer(*material_, i, toPath(path), &error)) {
                QMessageBox::warning(this, tr("Blend layer"), QString::fromStdString(error));
                return;
            }
            afterEdit(before);
            refreshForm();
        });
        connect(clear, &QPushButton::clicked, this, [this, i] {
            if (!material_ || material_->layerPaths[static_cast<std::size_t>(i)].empty())
                return;
            const engine::Material before = *material_;
            engine::setMaterialLayer(*material_, i, {});
            afterEdit(before);
            refreshForm();
        });
    }
    blendSharpness_ = makeSpin(1.0, 32.0, 0.5, 1);
    blendSharpness_->setToolTip(tr("Edge of the painted layers: 1 = soft gradient, 16 = hard edge following the layer's texture"));

    formLayout_ = form;
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Parent"), parentRow);
    form->addRow(tr("Color"), colorButton_);
    form->addRow(tr("Roughness"), roughness_);
    form->addRow(tr("Metallic"), metallic_);
    for (int i = 0; i < engine::kMapCount; ++i)
        form->addRow(tr(engine::mapLabel(static_cast<engine::MapKind>(i))), mapRows[static_cast<std::size_t>(i)]);
    form->addRow(tr("Normal strength"), normalStrength_);
    form->addRow(tr("Height depth"), heightDepth_);
    form->addRow(tr("Tiling"), tilingRow);
    form->addRow(tr("Detail tiling"), detailTiling_);
    form->addRow(tr("Detail strength"), detailStrength_);
    form->addRow(tr("Detail distance"), detailDistance_);
    form->addRow(tr("Emissive"), emissiveButton_);
    form->addRow(QString(), worldUv_);
    form->addRow(tr("Alpha"), alphaMode_);
    form->addRow(tr("Alpha cutoff"), alphaCutoff_);
    form->addRow(tr("Opacity"), opacity_);
    form->addRow(QString(), doubleSided_);
    form->addRow(tr("Surface"), surface_);
    form->addRow(tr("Blend layer 1 (red)"), layerRows[0]);
    form->addRow(tr("Blend layer 2 (green)"), layerRows[1]);
    form->addRow(tr("Blend sharpness"), blendSharpness_);
    form->addRow(saveRow);
    form->addRow(pathLabel_);
    keyWidgets_ = {{"color", colorButton_},       {"roughness", roughness_},       {"metallic", metallic_},
                   {"normal_strength", normalStrength_}, {"tiling", tilingRow},   {"emissive", emissiveButton_},
                   {"world_uv", worldUv_},          {"alpha_mode", alphaMode_},     {"alpha_cutoff", alphaCutoff_},
                   {"opacity", opacity_},           {"double_sided", doubleSided_}, {"surface", surface_},
                   {"layer1_material", layerRows[0]}, {"layer2_material", layerRows[1]}, {"blend_sharpness", blendSharpness_},
                   {"detail_tiling", detailTiling_}, {"detail_strength", detailStrength_}, {"detail_distance", detailDistance_},
                   {"height_depth", heightDepth_}};
    for (int i = 0; i < engine::kMapCount; ++i)
        keyWidgets_[engine::mapKey(static_cast<engine::MapKind>(i))] = mapRows[static_cast<std::size_t>(i)];
    tilingX_->setToolTip(tr("Repeats per UV unit; with world mapping (boxes) per metre: 0.25 = one image every 4 m"));
    roughness_->setToolTip(tr("0 = mirror-like, 1 = completely matte (multiplies the roughness map)"));
    metallic_->setToolTip(tr("0 = paint, stone, wood...; 1 = bare metal (multiplies the metallic map)"));
    normalStrength_->setToolTip(tr("How strongly the normal map bends the light (OpenGL / +Y green convention)"));
    emissiveButton_->setToolTip(tr("Glow added without light (black = none)"));

    preview_ = new MaterialPreview(this);
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(form_);
    rightLayout->addWidget(preview_, 1);

    auto* split = new QSplitter(this);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({260, 700});
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(split);

    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item)
            select(item->data(Qt::UserRole).toString());
    });
    connect(newButton, &QPushButton::clicked, this, &MaterialEditor::newMaterial);
    connect(dupButton, &QPushButton::clicked, this, &MaterialEditor::duplicateMaterial);
    connect(openButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open material"), assetPath("materials"),
                                                          tr("Materials (*.material)"));
        if (!path.isEmpty())
            open(path);
    });
    connect(nameEdit_, &QLineEdit::editingFinished, this, &MaterialEditor::edited);
    connect(worldUv_, &QCheckBox::toggled, this, &MaterialEditor::edited);
    connect(doubleSided_, &QCheckBox::toggled, this, &MaterialEditor::edited);
    connect(surface_, &QComboBox::activated, this, &MaterialEditor::edited);
    connect(alphaMode_, &QComboBox::currentIndexChanged, this, &MaterialEditor::edited);
    auto pickColor = [this](glm::vec3 engine::Material::*member, const QString& title) {
        if (!material_)
            return;
        glm::vec3& c = (*material_).*member;
        const QColor picked = QColorDialog::getColor(QColor::fromRgbF(c.r, c.g, c.b), this, title);
        if (!picked.isValid())
            return;
        const engine::Material before = *material_;
        c = {static_cast<float>(picked.redF()), static_cast<float>(picked.greenF()), static_cast<float>(picked.blueF())};
        afterEdit(before);
    };
    connect(colorButton_, &QPushButton::clicked, this, [=] { pickColor(&engine::Material::color, tr("Color")); });
    connect(emissiveButton_, &QPushButton::clicked, this, [=] { pickColor(&engine::Material::emissive, tr("Emissive")); });
    connect(saveButton, &QPushButton::clicked, this, &MaterialEditor::saveCurrent);
    connect(revertButton, &QPushButton::clicked, this, &MaterialEditor::revertCurrent);

    refreshList();
}

QString MaterialEditor::currentPath() const
{
    return material_ ? fromPath(material_->sourcePath) : QString();
}

void MaterialEditor::refreshList()
{
    const QString current = currentPath();
    list_->blockSignals(true);
    list_->clear();
    QDirIterator it(assetPath("materials"), {"*.material"}, QDir::Files, QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext())
        files << QFileInfo(it.next()).absoluteFilePath();
    if (!current.isEmpty() && !files.contains(current))
        files << current; // opened from elsewhere
    files.sort();
    for (const QString& f : files) {
        auto* item = new QListWidgetItem(QFileInfo(f).completeBaseName() + (modified_.contains(f) ? "*" : ""), list_);
        item->setData(Qt::UserRole, f);
        item->setToolTip(f);
        if (f == current)
            list_->setCurrentItem(item);
    }
    list_->blockSignals(false);
    if (!material_ && list_->count() > 0)
        list_->setCurrentRow(0);
    else if (!material_)
        refreshForm();
}

void MaterialEditor::open(const QString& path)
{
    select(QFileInfo(path).absoluteFilePath());
    refreshList();
}

void MaterialEditor::select(const QString& path)
{
    std::string error;
    auto m = engine::loadMaterial(toPath(path), &error);
    if (!m) {
        QMessageBox::warning(this, tr("Material"), QString::fromStdString(error));
        return;
    }
    material_ = std::move(m);
    preview_->setMaterial(material_);
    refreshForm();
}

void MaterialEditor::refreshForm()
{
    loading_ = true;
    form_->setEnabled(material_ != nullptr);
    if (material_) {
        const engine::Material& m = *material_;
        nameEdit_->setText(QString::fromStdString(m.name));
        colorButton_->setStyleSheet(colorStyle(m.color));
        emissiveButton_->setStyleSheet(colorStyle(m.emissive));
        for (int i = 0; i < engine::kMapCount; ++i) {
            const auto& p = m.mapPaths[static_cast<std::size_t>(i)];
            mapEdits_[i]->setText(p.empty() ? QString() : fromPath(p.filename()));
            mapEdits_[i]->setToolTip(fromPath(p));
            mapEditButtons_[i]->setEnabled(!p.empty());
        }
        tilingX_->setValue(m.tiling.x);
        tilingY_->setValue(m.tiling.y);
        roughness_->setValue(m.roughness);
        metallic_->setValue(m.metallic);
        normalStrength_->setValue(m.normalStrength);
        heightDepth_->setValue(m.heightDepth);
        detailTiling_->setValue(m.detailTiling);
        detailStrength_->setValue(m.detailStrength);
        detailDistance_->setValue(m.detailDistance);
        worldUv_->setChecked(m.worldUv);
        alphaMode_->setCurrentIndex(static_cast<int>(m.alphaMode));
        alphaCutoff_->setValue(m.alphaCutoff);
        alphaCutoff_->setEnabled(m.alphaMode == engine::AlphaMode::Mask);
        opacity_->setValue(m.opacity);
        doubleSided_->setChecked(m.doubleSided);
        {
            engine::Material guess = m;
            guess.surface.clear();
            const std::string g = engine::surfaceOf(&guess);
            surface_->setItemText(0, g.empty() ? tr("Auto (default)") : tr("Auto (%1)").arg(QString::fromStdString(g)));
            int index = surface_->findData(QString::fromStdString(m.surface));
            if (index < 0) {
                surface_->addItem(QString::fromStdString(m.surface), QString::fromStdString(m.surface));
                index = surface_->count() - 1;
            }
            surface_->setCurrentIndex(index);
        }
        for (int i = 0; i < 2; ++i) {
            const auto& p = m.layerPaths[static_cast<std::size_t>(i)];
            layerEdits_[i]->setText(p.empty() ? QString() : fromPath(p.filename()));
            layerEdits_[i]->setToolTip(fromPath(p));
        }
        blendSharpness_->setValue(m.blendSharpness);
        parentEdit_->setText(m.parentPath.empty() ? QString() : fromPath(m.parentPath.filename()));
        parentEdit_->setToolTip(fromPath(m.parentPath));
        resetOverrides_->setEnabled(m.parent && !m.overrides.empty());
        for (const auto& [key, widget] : keyWidgets_) {
            // Bold: set on this instance; normal: the parent's value.
            const bool own = m.parent && m.overrides.count(key);
            QWidget* label = formLayout_->labelForField(widget);
            for (QWidget* w : {label, widget}) {
                if (!w || (w == widget && !qobject_cast<QCheckBox*>(w)))
                    continue;
                QFont f = w->font();
                f.setBold(own);
                w->setFont(f);
            }
        }
        pathLabel_->setText(QDir::toNativeSeparators(currentPath()) + (modified_.contains(currentPath()) ? tr("  (unsaved)") : QString()));
    }
    loading_ = false;
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        const QString f = item->data(Qt::UserRole).toString();
        item->setText(QFileInfo(f).completeBaseName() + (modified_.contains(f) ? "*" : ""));
    }
}

void MaterialEditor::afterEdit(const engine::Material& before)
{
    if (material_->parent)
        for (const std::string& key : engine::materialDifferences(before, *material_))
            material_->overrides.insert(key); // changed here: no longer follows the parent
    engine::refreshMaterialInstances();       // instances of this material follow it
    modified_.insert(currentPath());
    refreshForm();
    emit materialChanged();
}

void MaterialEditor::edited()
{
    if (loading_ || !material_)
        return;
    const engine::Material before = *material_;
    engine::Material& m = *material_;
    m.name = nameEdit_->text().toStdString();
    m.tiling = {static_cast<float>(tilingX_->value()), static_cast<float>(tilingY_->value())};
    m.roughness = static_cast<float>(roughness_->value());
    m.metallic = static_cast<float>(metallic_->value());
    m.normalStrength = static_cast<float>(normalStrength_->value());
    m.heightDepth = static_cast<float>(heightDepth_->value());
    m.detailTiling = static_cast<float>(detailTiling_->value());
    m.detailStrength = static_cast<float>(detailStrength_->value());
    m.detailDistance = static_cast<float>(detailDistance_->value());
    m.blendSharpness = static_cast<float>(blendSharpness_->value());
    m.worldUv = worldUv_->isChecked();
    m.alphaMode = static_cast<engine::AlphaMode>(alphaMode_->currentIndex());
    m.alphaCutoff = static_cast<float>(alphaCutoff_->value());
    m.opacity = static_cast<float>(opacity_->value());
    m.doubleSided = doubleSided_->isChecked();
    m.surface = surface_->currentData().toString().toStdString();
    afterEdit(before);
}

bool MaterialEditor::saveCurrent()
{
    if (!material_)
        return true;
    std::string error;
    if (!engine::saveMaterial(*material_, material_->sourcePath, &error)) {
        QMessageBox::warning(this, tr("Save material"), QString::fromStdString(error));
        return false;
    }
    modified_.remove(currentPath());
    refreshForm();
    return true;
}

bool MaterialEditor::saveModified(QString* error)
{
    for (const QString& path : QStringList(modified_.values())) {
        auto m = engine::loadMaterial(toPath(path)); // cached: the edited object
        std::string err;
        if (m && !engine::saveMaterial(*m, m->sourcePath, &err)) {
            if (error)
                *error = QString::fromStdString(err);
            return false;
        }
        modified_.remove(path);
    }
    refreshForm();
    return true;
}

void MaterialEditor::revertCurrent()
{
    if (!material_)
        return;
    std::string error;
    if (!engine::reloadMaterial(*material_, &error)) {
        QMessageBox::warning(this, tr("Revert"), QString::fromStdString(error));
        return;
    }
    modified_.remove(currentPath());
    refreshForm();
    emit materialChanged();
}

void MaterialEditor::newMaterial()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("New material"), assetPath("materials/new.material"),
                                                      tr("Materials (*.material)"));
    if (path.isEmpty())
        return;
    engine::Material m;
    m.name = QFileInfo(path).completeBaseName().toStdString();
    m.tiling = {0.5f, 0.5f};
    m.roughness = 0.7f;
    std::string error;
    if (!engine::saveMaterial(m, toPath(path), &error)) {
        QMessageBox::warning(this, tr("New material"), QString::fromStdString(error));
        return;
    }
    open(path);
    emit createdMaterial(QFileInfo(path).absoluteFilePath());
}

void MaterialEditor::duplicateMaterial()
{
    if (!material_)
        return;
    const QString start = QFileInfo(currentPath()).dir().filePath(QFileInfo(currentPath()).completeBaseName() + "_copy.material");
    const QString path = QFileDialog::getSaveFileName(this, tr("Duplicate material"), start, tr("Materials (*.material)"));
    if (path.isEmpty())
        return;
    engine::Material copy = *material_;
    copy.name = QFileInfo(path).completeBaseName().toStdString();
    std::string error;
    if (!engine::saveMaterial(copy, toPath(path), &error)) {
        QMessageBox::warning(this, tr("Duplicate material"), QString::fromStdString(error));
        return;
    }
    open(path);
    emit createdMaterial(QFileInfo(path).absoluteFilePath());
}

void MaterialEditor::newInstance()
{
    if (!material_)
        return;
    const QString start = QFileInfo(currentPath()).dir().filePath(QFileInfo(currentPath()).completeBaseName() + "_variant.material");
    const QString path = QFileDialog::getSaveFileName(this, tr("New material instance"), start, tr("Materials (*.material)"));
    if (path.isEmpty())
        return;
    engine::Material instance;
    instance.name = QFileInfo(path).completeBaseName().toStdString();
    instance.sourcePath = toPath(path);
    std::string error;
    if (!engine::setMaterialParent(instance, material_->sourcePath, &error) || !engine::saveMaterial(instance, toPath(path), &error)) {
        QMessageBox::warning(this, tr("New material instance"), QString::fromStdString(error));
        return;
    }
    open(path);
    emit createdMaterial(QFileInfo(path).absoluteFilePath());
}
