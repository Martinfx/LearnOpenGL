// Landscape and Foliage modes: create the terrain, sculpt it, paint its four
// texture layers (road, grass, mud...) and paint trees, bushes and grass on it.
#include "MainWindow.h"

#include "AssetBrowser.h"
#include "AssetPaths.h"
#include "EditorUi.h"
#include "Viewport.h"

#include <engine/JobSystem.h>
#include <engine/Terrain.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>
#include <random>

namespace {

QString toQ(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }
std::filesystem::path fromQ(const QString& s) { return std::filesystem::path(s.toStdU16String()); }

QDoubleSpinBox* spin(QWidget* parent, double lo, double hi, double step, double value, int decimals, const QString& suffix = {})
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setValue(value);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}

// Layer colours of the brush ring (match the default layer tints in the shader).
const glm::vec3 kLayerColors[engine::Terrain::kLayers] = {
    {0.45f, 0.8f, 0.3f}, {0.85f, 0.65f, 0.35f}, {0.6f, 0.6f, 0.65f}, {0.9f, 0.9f, 0.4f}};

} // namespace

struct TerrainUi : QObject {
    using QObject::QObject;
    // Landscape
    QWidget* create = nullptr;
    QWidget* edit = nullptr;
    QDoubleSpinBox* size = nullptr;
    QDoubleSpinBox* originX = nullptr;
    QDoubleSpinBox* originZ = nullptr;
    QComboBox* resolution = nullptr;
    QLabel* info = nullptr;
    QButtonGroup* tools = nullptr;
    QDoubleSpinBox* radius = nullptr;
    QDoubleSpinBox* strength = nullptr;
    QComboBox* layerMaterial[engine::Terrain::kLayers] = {};
    QDoubleSpinBox* layerTiling[engine::Terrain::kLayers] = {};
    QRadioButton* layerPick[engine::Terrain::kLayers] = {};
    // Foliage
    QWidget* foliageNeedsTerrain = nullptr;
    QWidget* foliageEdit = nullptr;
    QListWidget* types = nullptr;
    QWidget* typeProps = nullptr;
    QDoubleSpinBox* density = nullptr;
    QDoubleSpinBox* minScale = nullptr;
    QDoubleSpinBox* maxScale = nullptr;
    QDoubleSpinBox* cull = nullptr;
    QCheckBox* collision = nullptr;
    QCheckBox* align = nullptr;
    QCheckBox* shadows = nullptr;
    QDoubleSpinBox* foliageRadius = nullptr;
    QRadioButton* erase = nullptr;
    QLabel* foliageInfo = nullptr;
    QString lastModel; // last model picked in the Content Browser

    float flattenHeight = 0.0f;
    bool sculpted = false;
    bool stroke = false;
    std::mt19937 rng{1234u};
    QElapsedTimer clock; // strokes act per second, not per repaint (slow GPUs)
    bool refreshing = false;
};

engine::Terrain* MainWindow::editTerrain()
{
    // Copy on write: undo snapshots keep the terrain as it was before this edit.
    if (!scene_.terrain)
        return nullptr;
    scene_.terrain = std::make_shared<engine::Terrain>(*scene_.terrain);
    return scene_.terrain.get();
}

void MainWindow::buildTerrainPages()
{
    terrainUi_ = new TerrainUi(this);
    TerrainUi& ui = *terrainUi_;
    const int layers = engine::Terrain::kLayers;

    // ---- Landscape -------------------------------------------------------
    {
        auto* page = new QWidget(modePages_);
        auto* layout = new QVBoxLayout(page);

        ui.create = new QWidget(page);
        {
            auto* form = new QFormLayout(ui.create);
            form->setContentsMargins(0, 0, 0, 0);
            auto* title = new QLabel(tr("<b>New terrain</b><br>Height field with four paintable texture layers."), ui.create);
            title->setWordWrap(true);
            form->addRow(title);
            ui.size = spin(ui.create, 16, 4096, 32, 256, 0, " m");
            ui.originX = spin(ui.create, -100000, 100000, 16, -128, 0, " m");
            ui.originZ = spin(ui.create, -100000, 100000, 16, -128, 0, " m");
            ui.resolution = new QComboBox(ui.create);
            for (int r : {129, 257, 513, 1025})
                ui.resolution->addItem(tr("%1 × %1 points").arg(r), r);
            ui.resolution->setCurrentIndex(1);
            ui.resolution->setToolTip(tr("Height samples per side; 257 on 256 m = one point per metre"));
            form->addRow(tr("Size"), ui.size);
            form->addRow(tr("Min X"), ui.originX);
            form->addRow(tr("Min Z"), ui.originZ);
            form->addRow(tr("Detail"), ui.resolution);
            auto* center = new QPushButton(tr("Center on the player start"), ui.create);
            auto* make = new QPushButton(editorIcon("add"), tr("Create terrain"), ui.create);
            make->setObjectName("primary");
            form->addRow(center);
            form->addRow(make);
            connect(center, &QPushButton::clicked, this, [this] {
                const double half = terrainUi_->size->value() * 0.5;
                terrainUi_->originX->setValue(std::round(scene_.playerStart.x - half));
                terrainUi_->originZ->setValue(std::round(scene_.playerStart.z - half));
            });
            connect(make, &QPushButton::clicked, this, [this] {
                TerrainUi& u = *terrainUi_;
                auto t = std::make_shared<engine::Terrain>();
                t->create({static_cast<float>(u.originX->value()), static_cast<float>(u.originZ->value())},
                          static_cast<float>(u.size->value()), u.resolution->currentData().toInt());
                scene_.terrain = std::move(t);
                markDirty();
                refreshTerrainPanel();
                notify(tr("Terrain created. Pick a material for layer 1 and start sculpting."), 5000);
            });
        }
        layout->addWidget(ui.create);

        ui.edit = new QWidget(page);
        {
            auto* v = new QVBoxLayout(ui.edit);
            v->setContentsMargins(0, 0, 0, 0);
            ui.info = new QLabel(ui.edit);
            ui.info->setObjectName("dim");
            ui.info->setWordWrap(true);
            v->addWidget(ui.info);

            auto* toolSection = new CollapsibleSection(tr("Brush"), ui.edit);
            auto* toolLayout = new QVBoxLayout;
            auto* grid = new QGridLayout;
            ui.tools = new QButtonGroup(ui.edit);
            const QString names[] = {tr("Raise"), tr("Lower"), tr("Smooth"), tr("Flatten"), tr("Paint")};
            const QString tips[] = {tr("Raise the ground (Shift: lower)"), tr("Lower the ground"),
                                    tr("Even out bumps"), tr("Level to the height where the stroke started"),
                                    tr("Paint the selected texture layer (roads, grass, mud)")};
            for (int i = 0; i < 5; ++i) {
                auto* b = new QPushButton(names[i], ui.edit);
                b->setCheckable(true);
                b->setToolTip(tips[i]);
                b->setObjectName("tool");
                ui.tools->addButton(b, i);
                grid->addWidget(b, i / 3, i % 3);
            }
            ui.tools->button(4)->setChecked(true);
            toolLayout->addLayout(grid);
            auto* form = new QFormLayout;
            ui.radius = spin(ui.edit, 0.5, 200, 1, 6, 1, " m");
            ui.strength = spin(ui.edit, 0.01, 1, 0.05, 0.5, 2);
            form->addRow(tr("Radius"), ui.radius);
            form->addRow(tr("Strength"), ui.strength);
            toolLayout->addLayout(form);
            auto* help = new QLabel(tr("Left drag on the terrain: apply · Shift: opposite (lower / erase layer) · "
                                       "right drag + WASD: fly"),
                                    ui.edit);
            help->setObjectName("dim");
            help->setWordWrap(true);
            toolLayout->addWidget(help);
            toolSection->setBodyLayout(toolLayout);
            v->addWidget(toolSection);

            auto* layerSection = new CollapsibleSection(tr("Texture layers"), ui.edit);
            auto* layerGrid = new QGridLayout;
            layerGrid->addWidget(new QLabel(tr("Paint"), ui.edit), 0, 0);
            layerGrid->addWidget(new QLabel(tr("Material"), ui.edit), 0, 1);
            layerGrid->addWidget(new QLabel(tr("Tiling"), ui.edit), 0, 2);
            auto* layerGroup = new QButtonGroup(ui.edit);
            for (int i = 0; i < layers; ++i) {
                ui.layerPick[i] = new QRadioButton(QString::number(i + 1), ui.edit);
                ui.layerPick[i]->setToolTip(tr("Layer painted by the Paint brush"));
                layerGroup->addButton(ui.layerPick[i], i);
                ui.layerMaterial[i] = new QComboBox(ui.edit);
                ui.layerMaterial[i]->setMinimumWidth(80);
                ui.layerMaterial[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
                ui.layerTiling[i] = spin(ui.edit, 0.01, 10, 0.05, 0.25, 2);
                ui.layerTiling[i]->setToolTip(tr("Repeats per metre (0.25 = the image covers 4 m)"));
                layerGrid->addWidget(ui.layerPick[i], i + 1, 0);
                layerGrid->addWidget(ui.layerMaterial[i], i + 1, 1);
                layerGrid->addWidget(ui.layerTiling[i], i + 1, 2);
                connect(ui.layerMaterial[i], &QComboBox::currentIndexChanged, this, [this, i] {
                    if (terrainUi_->refreshing || !scene_.terrain)
                        return;
                    const QString path = terrainUi_->layerMaterial[i]->currentData().toString();
                    engine::Terrain* t = editTerrain();
                    engine::TerrainLayer& l = t->layers[static_cast<std::size_t>(i)];
                    l.materialPath = path.isEmpty() ? std::filesystem::path() : fromQ(path).lexically_normal();
                    std::string error;
                    l.material = path.isEmpty() ? nullptr : engine::loadMaterial(l.materialPath, &error);
                    if (!path.isEmpty() && !l.material)
                        notify(tr("Material: %1").arg(QString::fromStdString(error)), 5000);
                    t->splatVersion = engine::newResourceId();
                    markDirty();
                });
                connect(ui.layerTiling[i], &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
                    if (terrainUi_->refreshing || !scene_.terrain)
                        return;
                    editTerrain()->layers[static_cast<std::size_t>(i)].tiling = static_cast<float>(v);
                    markDirty();
                });
            }
            ui.layerPick[0]->setChecked(true);
            connect(layerGroup, &QButtonGroup::idClicked, this, [this] { updateTerrainBrush(); });
            auto* layerHelp = new QLabel(tr("Layer 1 covers new terrain. Paint the others on top: e.g. 2 road, 3 mud, 4 gravel."), ui.edit);
            layerHelp->setObjectName("dim");
            layerHelp->setWordWrap(true);
            layerGrid->addWidget(layerHelp, layers + 1, 0, 1, 3);
            layerGrid->setColumnStretch(1, 1);
            layerSection->setBodyLayout(layerGrid);
            v->addWidget(layerSection);

            auto* remove = new QPushButton(editorIcon("delete"), tr("Delete terrain"), ui.edit);
            v->addWidget(remove);
            connect(remove, &QPushButton::clicked, this, [this] {
                scene_.terrain.reset();
                markDirty();
                refreshTerrainPanel();
            });
        }
        layout->addWidget(ui.edit);
        layout->addStretch(1);
        connect(ui.tools, &QButtonGroup::idClicked, this, [this] { updateTerrainBrush(); });
        connect(ui.radius, &QDoubleSpinBox::valueChanged, this, [this] { updateTerrainBrush(); });

        auto* scroll = new QScrollArea(modePages_);
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        modePages_->addWidget(scroll);
    }

    // ---- Foliage ---------------------------------------------------------
    {
        auto* page = new QWidget(modePages_);
        auto* layout = new QVBoxLayout(page);
        auto* needs = new QLabel(tr("Foliage is painted on the terrain: create one in Landscape mode first."), page);
        needs->setWordWrap(true);
        ui.foliageNeedsTerrain = needs;
        layout->addWidget(needs);

        ui.foliageEdit = new QWidget(page);
        auto* v = new QVBoxLayout(ui.foliageEdit);
        v->setContentsMargins(0, 0, 0, 0);

        auto* brushSection = new CollapsibleSection(tr("Brush"), ui.foliageEdit);
        auto* brushForm = new QFormLayout;
        auto* modeRow = new QHBoxLayout;
        auto* paint = new QRadioButton(tr("Paint"), ui.foliageEdit);
        ui.erase = new QRadioButton(tr("Erase"), ui.foliageEdit);
        paint->setChecked(true);
        modeRow->addWidget(paint);
        modeRow->addWidget(ui.erase);
        modeRow->addStretch();
        brushForm->addRow(tr("Mode"), modeRow);
        ui.foliageRadius = spin(ui.foliageEdit, 0.5, 200, 1, 8, 1, " m");
        brushForm->addRow(tr("Radius"), ui.foliageRadius);
        auto* help = new QLabel(tr("Left drag: paint the selected type · Shift: erase it · Ctrl+Shift: erase every type"),
                                ui.foliageEdit);
        help->setObjectName("dim");
        help->setWordWrap(true);
        brushForm->addRow(help);
        brushSection->setBodyLayout(brushForm);
        v->addWidget(brushSection);
        connect(ui.erase, &QRadioButton::toggled, this, [this] { updateTerrainBrush(); });
        connect(ui.foliageRadius, &QDoubleSpinBox::valueChanged, this, [this] { updateTerrainBrush(); });

        auto* typeSection = new CollapsibleSection(tr("Foliage types"), ui.foliageEdit);
        auto* typeLayout = new QVBoxLayout;
        ui.types = new QListWidget(ui.foliageEdit);
        ui.types->setMinimumHeight(110);
        ui.types->setIconSize(QSize(32, 32));
        typeLayout->addWidget(ui.types);
        auto* buttons = new QHBoxLayout;
        auto* add = new QPushButton(editorIcon("add"), tr("Add model"), ui.foliageEdit);
        add->setToolTip(tr("Adds the model selected in the Content Browser (or pick a file)"));
        auto* remove = new QPushButton(editorIcon("delete"), tr("Remove"), ui.foliageEdit);
        remove->setToolTip(tr("Removes the type and every painted instance of it"));
        buttons->addWidget(add);
        buttons->addWidget(remove);
        typeLayout->addLayout(buttons);

        ui.typeProps = new QWidget(ui.foliageEdit);
        auto* props = new QFormLayout(ui.typeProps);
        props->setContentsMargins(0, 4, 0, 0);
        ui.density = spin(ui.typeProps, 0.001, 20, 0.05, 0.5, 3, tr(" /m²"));
        ui.density->setToolTip(tr("Instances per square metre the brush fills to (trees ~0.02, bushes ~0.2, grass 1-4)"));
        ui.minScale = spin(ui.typeProps, 0.05, 20, 0.05, 0.8, 2);
        ui.maxScale = spin(ui.typeProps, 0.05, 20, 0.05, 1.2, 2);
        ui.cull = spin(ui.typeProps, 0, 10000, 10, 60, 0, " m");
        ui.cull->setSpecialValueText(tr("always drawn"));
        ui.collision = new QCheckBox(tr("Collision (trees)"), ui.typeProps);
        ui.align = new QCheckBox(tr("Tilt with the slope (grass)"), ui.typeProps);
        ui.shadows = new QCheckBox(tr("Cast shadows"), ui.typeProps);
        ui.shadows->setToolTip(tr("Off for grass: shadow maps are the most expensive pass"));
        props->addRow(tr("Density"), ui.density);
        props->addRow(tr("Scale min"), ui.minScale);
        props->addRow(tr("Scale max"), ui.maxScale);
        props->addRow(tr("Cull distance"), ui.cull);
        props->addRow(ui.collision);
        props->addRow(ui.align);
        props->addRow(ui.shadows);
        typeLayout->addWidget(ui.typeProps);
        ui.foliageInfo = new QLabel(ui.foliageEdit);
        ui.foliageInfo->setObjectName("dim");
        ui.foliageInfo->setWordWrap(true);
        typeLayout->addWidget(ui.foliageInfo);
        typeSection->setBodyLayout(typeLayout);
        v->addWidget(typeSection);
        layout->addWidget(ui.foliageEdit);
        layout->addStretch(1);

        // Property edits of the selected type.
        auto editType = [this](const std::function<void(engine::FoliageType&)>& f) {
            const int row = terrainUi_->types->currentRow();
            if (terrainUi_->refreshing || !scene_.terrain || row < 0 || row >= static_cast<int>(scene_.terrain->foliageTypes.size()))
                return;
            engine::Terrain* t = editTerrain();
            f(t->foliageTypes[static_cast<std::size_t>(row)]);
            t->foliageVersion = engine::newResourceId();
            markDirty();
        };
        connect(ui.density, &QDoubleSpinBox::valueChanged, this, [editType](double x) { editType([x](auto& ft) { ft.density = static_cast<float>(x); }); });
        connect(ui.minScale, &QDoubleSpinBox::valueChanged, this, [editType](double x) { editType([x](auto& ft) { ft.minScale = static_cast<float>(x); }); });
        connect(ui.maxScale, &QDoubleSpinBox::valueChanged, this, [editType](double x) { editType([x](auto& ft) { ft.maxScale = static_cast<float>(x); }); });
        connect(ui.cull, &QDoubleSpinBox::valueChanged, this, [editType](double x) { editType([x](auto& ft) { ft.cullDistance = static_cast<float>(x); }); });
        connect(ui.collision, &QCheckBox::toggled, this, [editType](bool on) { editType([on](auto& ft) { ft.collision = on; }); });
        connect(ui.align, &QCheckBox::toggled, this, [editType](bool on) { editType([on](auto& ft) { ft.alignToSlope = on; }); });
        connect(ui.shadows, &QCheckBox::toggled, this, [editType](bool on) { editType([on](auto& ft) { ft.castShadows = on; }); });
        connect(ui.types, &QListWidget::currentRowChanged, this, [this] {
            if (!terrainUi_->refreshing)
                refreshTerrainPanel();
        });
        connect(assets_, &AssetBrowser::assetSelected, this, [this](const QString& path, AssetType type) {
            if (type == AssetType::Model)
                terrainUi_->lastModel = path;
        });
        connect(add, &QPushButton::clicked, this, [this] {
            if (!scene_.terrain)
                return;
            QString path = terrainUi_->lastModel;
            if (path.isEmpty())
                path = QFileDialog::getOpenFileName(this, tr("Foliage model"), assetPath("models"), tr("glTF models (*.glb *.gltf)"));
            if (path.isEmpty())
                return;
            addFoliageType(path);
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int row = terrainUi_->types->currentRow();
            if (!scene_.terrain || row < 0)
                return;
            editTerrain()->removeFoliageType(row);
            markDirty();
            refreshTerrainPanel();
        });

        auto* scroll = new QScrollArea(modePages_);
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        modePages_->addWidget(scroll);
    }

    connect(viewport_, &Viewport::brushStroke, this, &MainWindow::terrainStroke);
    connect(viewport_, &Viewport::brushFinished, this, &MainWindow::terrainStrokeFinished);
}

void MainWindow::addFoliageType(const QString& path)
{
    engine::FoliageType ft;
    ft.modelPath = fromQ(path).lexically_normal();
    // Sensible defaults from the file name; everything stays editable below.
    const QString name = QFileInfo(path).completeBaseName().toLower();
    if (name.contains("tree") || name.contains("pine") || name.contains("spruce") || name.contains("birch") || name.contains("oak")) {
        ft.density = 0.02f;
        ft.collision = true;
        ft.cullDistance = 250.0f;
    } else if (name.contains("grass") || name.contains("weed") || name.contains("fern") || name.contains("nettle") ||
               name.contains("flower") || name.contains("clover")) {
        ft.density = 1.5f;
        ft.cullDistance = 45.0f;
        ft.alignToSlope = true;
        ft.castShadows = false;
    } else {
        ft.density = 0.15f;
        ft.cullDistance = 120.0f;
    }
    engine::Terrain* t = editTerrain();
    t->foliageTypes.push_back(ft);
    t->foliageVersion = engine::newResourceId();
    markDirty();
    refreshTerrainPanel();
    terrainUi_->types->setCurrentRow(static_cast<int>(t->foliageTypes.size()) - 1);
    // The model loads on a worker; painting starts once it is there.
    QPointer<MainWindow> self(this);
    const std::filesystem::path file = ft.modelPath;
    engine::JobSystem::global().run([self, file] {
        auto model = std::make_shared<engine::Model>();
        std::string error;
        if (!engine::loadModelFile(*model, file, &error))
            model.reset();
        QMetaObject::invokeMethod(qApp, [self, file, model, error] {
            if (!self)
                return;
            if (!model) {
                self->notify(tr("Foliage model: %1").arg(QString::fromStdString(error)), 6000);
                return;
            }
            // Runtime data only: every terrain copy (undo steps) with this type gets the mesh.
            if (self->scene_.terrain)
                for (engine::FoliageType& type : self->scene_.terrain->foliageTypes)
                    if (type.modelPath == file && !type.model)
                        type.model = model;
            if (self->scene_.terrain)
                self->scene_.terrain->foliageVersion = engine::newResourceId();
            self->refreshTerrainPanel();
            self->viewport_->update();
        });
    });
}

void MainWindow::refreshTerrainPanel()
{
    if (!terrainUi_)
        return;
    TerrainUi& ui = *terrainUi_;
    ui.refreshing = true;
    const engine::Terrain* t = scene_.terrain && scene_.terrain->valid() ? scene_.terrain.get() : nullptr;
    ui.create->setVisible(!t);
    ui.edit->setVisible(t != nullptr);
    ui.foliageNeedsTerrain->setVisible(!t);
    ui.foliageEdit->setVisible(t != nullptr);
    if (t) {
        ui.info->setText(tr("%1 × %1 m from X %2, Z %3 · %4 points per side · %5 foliage instances")
                             .arg(static_cast<double>(t->size))
                             .arg(static_cast<double>(t->origin.x))
                             .arg(static_cast<double>(t->origin.y))
                             .arg(t->resolution)
                             .arg(t->foliage.size()));
        // Material choices: every .material under assets/materials.
        QDirIterator it(assetPath("materials"), {"*.material"}, QDir::Files, QDirIterator::Subdirectories);
        QStringList files;
        while (it.hasNext())
            files << it.next();
        files.sort(Qt::CaseInsensitive);
        for (int i = 0; i < engine::Terrain::kLayers; ++i) {
            const engine::TerrainLayer& l = t->layers[static_cast<std::size_t>(i)];
            QComboBox* c = ui.layerMaterial[i];
            c->clear();
            c->addItem(i == 0 ? tr("(plain ground)") : tr("(unused)"), QString());
            for (const QString& f : files) {
                QIcon icon = assets_->iconFor(f);
                c->addItem(icon.isNull() ? editorIcon("content") : icon, QFileInfo(f).completeBaseName(), QDir::cleanPath(f));
            }
            const QString current = l.materialPath.empty() ? QString() : QDir::cleanPath(toQ(l.materialPath));
            int index = c->findData(current);
            if (index < 0 && !current.isEmpty()) { // outside assets/materials
                c->addItem(QFileInfo(current).completeBaseName(), current);
                index = c->count() - 1;
            }
            c->setCurrentIndex(std::max(0, index));
            ui.layerTiling[i]->setValue(l.tiling);
        }
        // Foliage types.
        const int row = ui.types->currentRow();
        ui.types->clear();
        std::vector<int> counts(t->foliageTypes.size(), 0);
        for (const engine::FoliageInstance& f : t->foliage)
            if (f.type < counts.size())
                ++counts[f.type];
        for (std::size_t i = 0; i < t->foliageTypes.size(); ++i) {
            const engine::FoliageType& ft = t->foliageTypes[i];
            const QString path = toQ(ft.modelPath);
            QIcon icon = assets_->iconFor(path);
            auto* item = new QListWidgetItem(icon.isNull() ? editorIcon("foliage") : icon,
                                             tr("%1 (%2)%3").arg(QFileInfo(path).completeBaseName()).arg(counts[i])
                                                 .arg(ft.model ? QString() : tr(" - loading")));
            item->setToolTip(path);
            ui.types->addItem(item);
        }
        const int count = static_cast<int>(t->foliageTypes.size());
        ui.types->setCurrentRow(count == 0 ? -1 : std::clamp(row, 0, count - 1));
        const int sel = ui.types->currentRow();
        ui.typeProps->setEnabled(sel >= 0);
        if (sel >= 0) {
            const engine::FoliageType& ft = t->foliageTypes[static_cast<std::size_t>(sel)];
            ui.density->setValue(ft.density);
            ui.minScale->setValue(ft.minScale);
            ui.maxScale->setValue(ft.maxScale);
            ui.cull->setValue(ft.cullDistance);
            ui.collision->setChecked(ft.collision);
            ui.align->setChecked(ft.alignToSlope);
            ui.shadows->setChecked(ft.castShadows);
        }
        ui.foliageInfo->setText(count == 0 ? tr("Select a tree, bush or grass model in the Content Browser and press Add model.")
                                           : tr("%1 instances in total.").arg(t->foliage.size()));
    }
    ui.refreshing = false;
    updateTerrainBrush();
}

void MainWindow::updateTerrainBrush()
{
    if (!terrainUi_ || !modePages_)
        return;
    const TerrainUi& ui = *terrainUi_;
    const int mode = modePages_->currentIndex();
    const bool on = (mode == 3 || mode == 4) && scene_.terrain && scene_.terrain->valid();
    if (mode == 4) {
        viewport_->setBrush(on, static_cast<float>(ui.foliageRadius->value()),
                            ui.erase->isChecked() ? glm::vec3(1.0f, 0.35f, 0.3f) : glm::vec3(0.35f, 1.0f, 0.4f));
        return;
    }
    const int tool = ui.tools->checkedId();
    int layer = 0;
    for (int i = 0; i < engine::Terrain::kLayers; ++i)
        if (ui.layerPick[i]->isChecked())
            layer = i;
    viewport_->setBrush(on, static_cast<float>(ui.radius->value()), tool == 4 ? kLayerColors[layer] : glm::vec3(0.3f, 0.75f, 1.0f));
}

void MainWindow::terrainStroke(glm::vec3 point, Qt::KeyboardModifiers modifiers, bool first)
{
    if (!scene_.terrain || !terrainUi_)
        return;
    TerrainUi& ui = *terrainUi_;
    // Seconds since the previous step of this stroke (bounded: a stalled frame is not a huge dab).
    const float dt = first || !ui.clock.isValid() ? 1.0f / 30.0f : std::clamp(static_cast<float>(ui.clock.restart()) / 1000.0f, 0.0f, 0.25f);
    if (first)
        ui.clock.restart();
    if (first) {
        editTerrain(); // one copy per stroke (undo)
        ui.flattenHeight = point.y;
        ui.sculpted = false;
        ui.stroke = true;
    }
    engine::Terrain& t = *scene_.terrain;
    const bool shift = modifiers & Qt::ShiftModifier;
    if (modePages_->currentIndex() == 4) {
        const int type = ui.types->currentRow();
        const float r = static_cast<float>(ui.foliageRadius->value());
        if (ui.erase->isChecked() || shift) {
            t.eraseFoliage(point, r, (modifiers & Qt::ControlModifier) || type < 0 ? -1 : type);
        } else if (type >= 0 && type < static_cast<int>(t.foliageTypes.size())) {
            if (!t.foliageTypes[static_cast<std::size_t>(type)].model) {
                if (first)
                    notify(tr("The foliage model is still loading."), 3000);
                return;
            }
            t.paintFoliage(point, r, type, ui.rng);
        } else if (first) {
            notify(tr("Add a foliage type first (a model from the Content Browser)."), 4000);
        }
        viewport_->update();
        return;
    }
    const float r = static_cast<float>(ui.radius->value());
    const float s = static_cast<float>(ui.strength->value());
    int layer = 0;
    for (int i = 0; i < engine::Terrain::kLayers; ++i)
        if (ui.layerPick[i]->isChecked())
            layer = i;
    // Rates at full strength: raise / lower 3 m per second in the centre (sculpt moves
    // 0.5 m per unit step), smooth / flatten / paint most of the way in about a second.
    const float raise = s * 6.0f * dt;
    const float blend = 1.0f - std::pow(1.0f - std::min(s, 0.99f), dt * 4.0f);
    switch (ui.tools->checkedId()) {
    case 0: t.sculpt(point, r, raise, shift ? engine::Terrain::Sculpt::Lower : engine::Terrain::Sculpt::Raise); break;
    case 1: t.sculpt(point, r, raise, shift ? engine::Terrain::Sculpt::Raise : engine::Terrain::Sculpt::Lower); break;
    case 2: t.sculpt(point, r, blend, engine::Terrain::Sculpt::Smooth); break;
    case 3: t.sculpt(point, r, blend, engine::Terrain::Sculpt::Flatten, ui.flattenHeight); break;
    default:
        // Shift paints layer 1 back (erases the others).
        t.paintLayer(point, r, blend, shift ? 0 : layer);
        viewport_->update();
        return;
    }
    ui.sculpted = true;
    viewport_->update();
}

void MainWindow::terrainStrokeFinished()
{
    if (!terrainUi_ || !terrainUi_->stroke)
        return;
    terrainUi_->stroke = false;
    if (scene_.terrain && terrainUi_->sculpted)
        scene_.terrain->snapFoliageToGround();
    markDirty();
    refreshTerrainPanel();
}
