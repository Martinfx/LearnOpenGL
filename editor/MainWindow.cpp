#include "MainWindow.h"
#include "HierarchyPanel.h"
#include "AssetBrowser.h"

#include <engine/SceneGraph.h>
#include "AssetPaths.h"
#include "MaterialEditor.h"
#include "ScriptEditor.h"
#include "TextureEditor.h"
#include "Vec3Editor.h"
#include "Viewport.h"
#include "WeaponPanel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDesktopServices>
#include <QDockWidget>
#include <QHeaderView>
#include <QScrollArea>
#include <QTableWidget>
#include <QUrl>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QShortcut>
#include <QTabBar>
#include <QTabWidget>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>

#include <cmath>
#include <filesystem>
#include <glm/geometric.hpp>

namespace {
const char* kFileFilter = "Levels (*.level);;All files (*)";

QString colorStyle(glm::vec3 c)
{
    const QColor q = QColor::fromRgbF(c.r, c.g, c.b);
    return QString("background-color: %1; min-height: 22px;").arg(q.name());
}
} // namespace

MainWindow::MainWindow()
{
    scene_ = engine::makeDefaultScene();

    viewport_ = new Viewport(&scene_, this);
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->addTab(viewport_, tr("Level"));
    tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr); // the 3D view cannot be closed
    tabs_->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { closeScriptTab(index); });
    setCentralWidget(tabs_);
    connect(viewport_, &Viewport::boxPicked, this, &MainWindow::select);
    connect(viewport_, &Viewport::assetDropped, this, [this](const QString& path, glm::vec3 point, int object) {
        switch (assetTypeOf(path)) {
        case AssetType::Model: placeModel(path, point); break;
        case AssetType::Material:
            if (!assignMaterial(path, object))
                statusBar()->showMessage(tr("Drop a material onto a box or a model."), 5000);
            break;
        default: openAsset(path, static_cast<int>(assetTypeOf(path))); break;
        }
    });
    connect(viewport_, &Viewport::rendererReady, this,
            [this](const QString& info) { statusBar()->showMessage(info, 8000); });
    connect(viewport_, &Viewport::rendererFailed, this, [this](const QString& error) {
        QMessageBox::critical(this, tr("OpenGL"), tr("Renderer could not start:\n%1").arg(error));
    });

    buildMenus();
    buildDocks();
    refreshOutliner();
    refreshLevelProperties();
    select(-1);
    updateTitle();
    statusBar()->showMessage(tr("Right mouse + WASD/QE: fly · Left click: select · F: focus · F5: play"));
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newLevel);
    file->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openLevel);
    // Ctrl+S saves what is in front: the open script, or the level.
    file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::saveCurrent);
    file->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    QAction* add = edit->addAction(tr("&Add box"), QKeySequence(Qt::CTRL | Qt::Key_B), this, &MainWindow::addBox);
    QAction* import = edit->addAction(tr("&Import glTF model..."), QKeySequence(Qt::CTRL | Qt::Key_I), this, &MainWindow::importModel);
    QAction* enemy = edit->addAction(tr("Add &enemy"), QKeySequence(Qt::CTRL | Qt::Key_E), this, [this] { addEntity("enemy"); });
    edit->addAction(tr("Add empty en&tity"), this, [this] { addEntity("empty"); });
    QAction* dup = edit->addAction(tr("&Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), this, &MainWindow::duplicateBox);
    QAction* del = edit->addAction(tr("De&lete"), QKeySequence::Delete, this, &MainWindow::deleteBox);

    QMenu* script = menuBar()->addMenu(tr("&Script"));
    script->addAction(tr("&New script..."), this, &MainWindow::newScript);
    script->addAction(tr("&Open script..."), this, &MainWindow::openScriptDialog);
    script->addAction(tr("Save &all scripts"), this, [this] { saveAllScripts(); });
    script->addSeparator();
    script->addAction(tr("&Find"), QKeySequence::Find, this, [this] {
        if (auto* se = currentScript())
            se->showFind(false);
    });
    script->addAction(tr("&Replace"), QKeySequence(Qt::CTRL | Qt::Key_H), this, [this] {
        if (auto* se = currentScript())
            se->showFind(true);
    });
    script->addAction(tr("&Go to line..."), QKeySequence(Qt::CTRL | Qt::Key_G), this, [this] {
        if (auto* se = currentScript())
            se->goToLinePrompt();
    });
    script->addAction(tr("Toggle &comment"), QKeySequence(Qt::CTRL | Qt::Key_Slash), this, [this] {
        if (auto* se = currentScript())
            se->editor()->toggleComment();
    });
    script->addAction(tr("&Close tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this, [this] {
        if (tabs_->currentIndex() > 0)
            closeScriptTab(tabs_->currentIndex());
    });
    script->addAction(tr("Open in e&xternal editor"), this, [this] {
        if (auto* se = currentScript())
            QDesktopServices::openUrl(QUrl::fromLocalFile(se->path()));
    });

    QMenu* assets = menuBar()->addMenu(tr("&Assets"));
    assets->addAction(tr("&Material editor"), QKeySequence(Qt::CTRL | Qt::Key_M), this, [this] { openMaterialEditor(); });
    assets->addAction(tr("Edit &texture..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Edit texture"), assetPath("textures"),
                                                          tr("Images (*.png *.jpg *.jpeg)"));
        if (!path.isEmpty())
            openTexture(path);
    });

    QMenu* game = menuBar()->addMenu(tr("&Game"));
    QAction* play = game->addAction(tr("&Play level"), QKeySequence(Qt::Key_F5), this, &MainWindow::playLevel);

    QToolBar* tools = addToolBar(tr("Tools"));
    tools->setObjectName("tools");
    tools->addAction(add);
    tools->addAction(import);
    tools->addAction(enemy);
    tools->addAction(dup);
    tools->addAction(del);
    tools->addSeparator();
    tools->addAction(play);
}

void MainWindow::buildDocks()
{
    // Hierarchy: groups and objects as a tree (drag and drop to re-parent).
    outliner_ = new HierarchyTree(&scene_, this);
    connect(outliner_, &HierarchyTree::objectSelected, this, [this](int object) {
        if (object != selected_)
            select(object);
    });
    connect(outliner_, &HierarchyTree::hierarchyChanged, this, [this] {
        dependencies_->showObject(selected_);
        markDirty();
    });
    connect(outliner_, &HierarchyTree::renamed, this, [this] { refreshProperties(); });
    auto* outlinerDock = new QDockWidget(tr("Hierarchy"), this);
    outlinerDock->setObjectName("outliner");
    outlinerDock->setWidget(outliner_);
    addDockWidget(Qt::LeftDockWidgetArea, outlinerDock);

    // What the selected object uses and what else uses the same files.
    dependencies_ = new DependencyPanel(&scene_, this);
    connect(dependencies_, &DependencyPanel::objectActivated, this, [this](int object) { select(object); });
    connect(dependencies_, &DependencyPanel::assetActivated, this, [this](const QString& path, int kind) {
        switch (static_cast<engine::AssetKind>(kind)) {
        case engine::AssetKind::Material: openMaterialEditor(path); break;
        case engine::AssetKind::Texture: openTexture(path); break;
        case engine::AssetKind::Script: openScript(path); break;
        default:
            statusBar()->showMessage(path, 5000);
            break;
        }
    });
    auto* depsDock = new QDockWidget(tr("Relations"), this);
    depsDock->setObjectName("relations");
    depsDock->setWidget(dependencies_);
    addDockWidget(Qt::LeftDockWidgetArea, depsDock);
    splitDockWidget(outlinerDock, depsDock, Qt::Vertical);

    // Properties of the selected box.
    boxPanel_ = new QWidget(this);
    auto* form = new QFormLayout(boxPanel_);
    nameEdit_ = new QLineEdit(boxPanel_);
    kindLabel_ = new QLabel(boxPanel_);
    form->addRow(tr("Type"), kindLabel_);
    positionEdit_ = new Vec3Editor(-10000.0, 10000.0, 0.25, boxPanel_);
    sizeEdit_ = new Vec3Editor(0.01, 10000.0, 0.25, boxPanel_);
    colorButton_ = new QPushButton(boxPanel_);
    typeEdit_ = new QLineEdit(boxPanel_);
    typeEdit_->setPlaceholderText(tr("enemy, pickup, trigger..."));
    yawSpin_ = new QDoubleSpinBox(boxPanel_);
    yawSpin_->setRange(-360.0, 360.0);
    yawSpin_->setSingleStep(15.0);
    yawSpin_->setSuffix(QStringLiteral(" °"));
    yawSpin_->setKeyboardTracking(false);
    scriptRow_ = new QWidget(boxPanel_);
    auto* scriptLayout = new QHBoxLayout(scriptRow_);
    scriptLayout->setContentsMargins(0, 0, 0, 0);
    scriptEdit_ = new QLineEdit(scriptRow_);
    scriptEdit_->setReadOnly(true);
    scriptEdit_->setPlaceholderText(tr("no script"));
    auto* browse = new QPushButton(tr("..."), scriptRow_);
    auto* clear = new QPushButton(tr("x"), scriptRow_);
    browse->setFixedWidth(32);
    clear->setFixedWidth(32);
    scriptLayout->addWidget(scriptEdit_);
    scriptLayout->addWidget(browse);
    scriptLayout->addWidget(clear);
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Entity type"), typeEdit_);
    form->addRow(tr("Position"), positionEdit_);
    form->addRow(tr("Yaw"), yawSpin_);
    form->addRow(tr("Size"), sizeEdit_);
    form->addRow(tr("Color"), colorButton_);

    // Box material (.material), and per-slot overrides of glTF materials for models.
    materialRow_ = new QWidget(boxPanel_);
    auto* materialLayout = new QHBoxLayout(materialRow_);
    materialLayout->setContentsMargins(0, 0, 0, 0);
    materialEdit_ = new QLineEdit(materialRow_);
    materialEdit_->setReadOnly(true);
    materialEdit_->setPlaceholderText(tr("plain color"));
    auto* browseMaterial = new QPushButton(tr("..."), materialRow_);
    auto* clearMaterial = new QPushButton(tr("x"), materialRow_);
    auto* editMaterial = new QPushButton(tr("Edit"), materialRow_);
    browseMaterial->setFixedWidth(32);
    clearMaterial->setFixedWidth(32);
    materialLayout->addWidget(materialEdit_, 1);
    materialLayout->addWidget(browseMaterial);
    materialLayout->addWidget(clearMaterial);
    materialLayout->addWidget(editMaterial);
    form->addRow(tr("Material"), materialRow_);
    connect(browseMaterial, &QPushButton::clicked, this, &MainWindow::browseBoxMaterial);
    connect(clearMaterial, &QPushButton::clicked, this, [this] {
        if (auto* b = selectedBox(); b && !b->materialPath.empty()) {
            b->materialPath.clear();
            b->material.reset();
            refreshProperties();
            markDirty();
        }
    });
    connect(editMaterial, &QPushButton::clicked, this, [this] {
        if (auto* b = selectedBox(); b && !b->materialPath.empty())
            openMaterialEditor(QString::fromStdU16String(b->materialPath.u16string()));
        else
            openMaterialEditor();
    });
    materialSlots_ = new QTableWidget(0, 2, boxPanel_);
    materialSlots_->setHorizontalHeaderLabels({tr("glTF material"), tr("Override (double-click)")});
    materialSlots_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    materialSlots_->verticalHeader()->hide();
    materialSlots_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    materialSlots_->setMinimumHeight(110);
    materialSlots_->setToolTip(tr("Double-click: choose a .material for this slot. Delete: back to the glTF material."));
    form->addRow(materialSlots_);
    connect(materialSlots_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* m = selectedModel();
        if (!m || row < 0)
            return;
        const std::string slot = materialSlots_->item(row, 0)->text().toStdString();
        const QString path = QFileDialog::getOpenFileName(this, tr("Material for %1").arg(QString::fromStdString(slot)),
                                                          assetPath("materials"), tr("Materials (*.material)"));
        if (path.isEmpty())
            return;
        std::string error;
        auto material = engine::loadMaterial(std::filesystem::path(path.toStdU16String()), &error);
        if (!material) {
            QMessageBox::warning(this, tr("Material"), QString::fromStdString(error));
            return;
        }
        m->materialOverrides[slot] = material->sourcePath;
        m->overrideMaterials[slot] = std::move(material);
        refreshMaterialSlots();
        markDirty();
    });
    auto* clearSlot = new QShortcut(QKeySequence::Delete, materialSlots_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(clearSlot, &QShortcut::activated, this, [this] {
        auto* m = selectedModel();
        const int row = materialSlots_->currentRow();
        if (!m || row < 0)
            return;
        const std::string slot = materialSlots_->item(row, 0)->text().toStdString();
        m->materialOverrides.erase(slot);
        m->overrideMaterials.erase(slot);
        refreshMaterialSlots();
        markDirty();
    });
    form->addRow(tr("Lua script"), scriptRow_);

    // Animated character model (glTF with skeleton) drawn instead of the box.
    characterRow_ = new QWidget(boxPanel_);
    auto* characterLayout = new QHBoxLayout(characterRow_);
    characterLayout->setContentsMargins(0, 0, 0, 0);
    characterEdit_ = new QLineEdit(characterRow_);
    characterEdit_->setReadOnly(true);
    characterEdit_->setPlaceholderText(tr("box (no model)"));
    auto* browseCharacter = new QPushButton(tr("..."), characterRow_);
    auto* clearCharacter = new QPushButton(tr("x"), characterRow_);
    browseCharacter->setFixedWidth(32);
    clearCharacter->setFixedWidth(32);
    characterLayout->addWidget(characterEdit_);
    characterLayout->addWidget(browseCharacter);
    characterLayout->addWidget(clearCharacter);
    modelScaleSpin_ = new QDoubleSpinBox(boxPanel_);
    modelScaleSpin_->setRange(0.05, 20.0);
    modelScaleSpin_->setSingleStep(0.05);
    modelScaleSpin_->setDecimals(2);
    modelScaleSpin_->setPrefix(QStringLiteral("x "));
    modelScaleSpin_->setToolTip(tr("The model is fitted to the entity height (Size Y); this multiplies it."));
    modelScaleSpin_->setKeyboardTracking(false);
    modelYawSpin_ = new QDoubleSpinBox(boxPanel_);
    modelYawSpin_->setRange(-360.0, 360.0);
    modelYawSpin_->setSingleStep(90.0);
    modelYawSpin_->setSuffix(QStringLiteral(" °"));
    modelYawSpin_->setToolTip(tr("Extra rotation for models that do not face +Z"));
    modelYawSpin_->setKeyboardTracking(false);
    animationCombo_ = new QComboBox(boxPanel_);
    animationCombo_->setToolTip(tr("Clip shown in the editor and played when the script does not choose one"));
    form->addRow(tr("Character model"), characterRow_);
    form->addRow(tr("Model scale"), modelScaleSpin_);
    form->addRow(tr("Model yaw"), modelYawSpin_);
    form->addRow(tr("Animation"), animationCombo_);
    connect(browseCharacter, &QPushButton::clicked, this, &MainWindow::browseCharacter);
    connect(clearCharacter, &QPushButton::clicked, this, [this] {
        if (auto* e = selectedEntity(); e && !e->modelPath.empty()) {
            e->modelPath.clear();
            e->model.reset();
            e->animation.clear();
            e->animator.stop();
            refreshProperties();
            markDirty();
        }
    });
    connect(modelScaleSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (auto* e = selectedEntity(); e && e->modelScale != static_cast<float>(v)) {
            e->modelScale = static_cast<float>(v);
            markDirty();
        }
    });
    connect(modelYawSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (auto* e = selectedEntity(); e && e->modelYaw != static_cast<float>(v)) {
            e->modelYaw = static_cast<float>(v);
            markDirty();
        }
    });
    connect(animationCombo_, &QComboBox::currentTextChanged, this, [this](const QString& clip) {
        auto* e = selectedEntity();
        if (!e || !e->model || !e->model->skeleton || clip.toStdString() == e->animation)
            return;
        e->animation = clip.toStdString();
        e->animator.play(*e->model->skeleton, e->animation, true, 1.0f, 0.2f, true);
        markDirty();
    });

    // Tunables the script declares in `properties`; values are saved per entity.
    scriptParams_ = new QTableWidget(0, 2, boxPanel_);
    scriptParams_->setHorizontalHeaderLabels({tr("Script parameter"), tr("Value")});
    scriptParams_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    scriptParams_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    scriptParams_->verticalHeader()->hide();
    scriptParams_->setMinimumHeight(200);
    auto* editScript = new QPushButton(tr("Edit script"), boxPanel_);
    form->addRow(scriptParams_);
    form->addRow(editScript);
    connect(editScript, &QPushButton::clicked, this, [this] {
        if (auto* e = selectedEntity(); e && !e->scriptPath.empty())
            openScript(QString::fromStdU16String(e->scriptPath.u16string()));
    });
    connect(scriptParams_, &QTableWidget::cellChanged, this, [this](int row, int column) {
        auto* e = selectedEntity();
        if (!e || column != 1 || row < 0 || row >= static_cast<int>(scriptProps_.size()))
            return;
        const engine::ScriptProperty& prop = scriptProps_[static_cast<std::size_t>(row)];
        const std::string value = scriptParams_->item(row, 1)->text().trimmed().toStdString();
        if (value.empty() || value == prop.defaultValue)
            e->properties.erase(prop.name); // back to the script default
        else
            e->properties[prop.name] = value;
        markDirty();
        refreshScriptParams();
    });

    connect(typeEdit_, &QLineEdit::editingFinished, this, [this] {
        if (auto* e = selectedEntity(); e && e->type != typeEdit_->text().toStdString()) {
            e->type = typeEdit_->text().toStdString();
            refreshOutliner();
            markDirty();
        }
    });
    connect(yawSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (auto* e = selectedEntity(); e && e->yawDegrees != static_cast<float>(v)) {
            e->yawDegrees = static_cast<float>(v);
            markDirty();
        }
    });
    connect(browse, &QPushButton::clicked, this, &MainWindow::browseScript);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (auto* e = selectedEntity(); e && !e->scriptPath.empty()) {
            e->scriptPath.clear();
            refreshProperties();
            markDirty();
        }
    });

    connect(nameEdit_, &QLineEdit::editingFinished, this, [this] {
        if (selected_ < 0)
            return;
        const std::string oldName = engine::objectName(scene_, selected_);
        const std::string newName = nameEdit_->text().trimmed().toStdString();
        if (newName == oldName)
            return;
        // Names are unique so children can refer to their parent; renaming updates the children.
        if (!engine::renameNode(scene_, oldName, newName)) {
            QMessageBox::warning(this, tr("Rename"), tr("The name \"%1\" is empty or already used.")
                                                         .arg(QString::fromStdString(newName)));
            nameEdit_->setText(QString::fromStdString(oldName));
            return;
        }
        refreshOutliner();
        markDirty();
    });
    connect(positionEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        // Children move with their parent.
        if (selected_ >= 0) {
            const glm::vec3 delta = v - engine::objectPosition(scene_, selected_);
            for (int child : engine::descendantObjects(scene_, engine::objectName(scene_, selected_)))
                engine::translateObject(scene_, child, delta);
        }
        if (auto* b = selectedBox())
            b->position = v;
        else if (auto* m = selectedModel())
            m->position = v;
        else if (auto* e = selectedEntity())
            e->position = v;
        else
            return;
        markDirty();
    });
    connect(sizeEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        if (auto* b = selectedBox())
            b->size = v;
        else if (auto* e = selectedEntity())
            e->size = v;
        else
            return;
        markDirty();
    });
    connect(colorButton_, &QPushButton::clicked, this, [this] {
        glm::vec3* color = selectedBox() ? &selectedBox()->color : (selectedEntity() ? &selectedEntity()->color : nullptr);
        if (!color)
            return;
        const QColor initial = QColor::fromRgbF(color->r, color->g, color->b);
        const QColor c = QColorDialog::getColor(initial, this, tr("Color"));
        if (!c.isValid())
            return;
        *color = {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF())};
        colorButton_->setStyleSheet(colorStyle(*color));
        markDirty();
    });

    // Level-wide settings.
    auto* levelPanel = new QWidget(this);
    auto* levelForm = new QFormLayout(levelPanel);
    playerStartEdit_ = new Vec3Editor(-10000.0, 10000.0, 0.25, levelPanel);
    sunEdit_ = new Vec3Editor(-1.0, 1.0, 0.05, levelPanel);
    auto* fromCamera = new QPushButton(tr("Player start = camera position"), levelPanel);
    levelForm->addRow(tr("Player start"), playerStartEdit_);
    levelForm->addRow(QString(), fromCamera);
    levelForm->addRow(tr("Sun direction"), sunEdit_);

    // Player movement and health for this level.
    struct PlayerField {
        const char* label;
        float engine::PlayerSettings::*member;
        double min, max, step;
        const char* suffix;
    };
    const PlayerField playerFields[] = {
        {"Walk speed", &engine::PlayerSettings::walkSpeed, 0.1, 50, 0.5, " m/s"},
        {"Sprint speed", &engine::PlayerSettings::sprintSpeed, 0.1, 50, 0.5, " m/s"},
        {"Jump speed", &engine::PlayerSettings::jumpSpeed, 0, 50, 0.5, " m/s"},
        {"Gravity", &engine::PlayerSettings::gravity, 0, 100, 1, " m/s²"},
        {"Air control", &engine::PlayerSettings::airControl, 0, 1, 0.05, ""},
        {"Height", &engine::PlayerSettings::height, 0.5, 4, 0.05, " m"},
        {"Eye height", &engine::PlayerSettings::eyeHeight, 0.3, 4, 0.05, " m"},
        {"Step height", &engine::PlayerSettings::stepHeight, 0, 2, 0.05, " m"},
        {"Max slope", &engine::PlayerSettings::maxSlopeDegrees, 0, 89, 1, " °"},
        {"Health", &engine::PlayerSettings::maxHealth, 1, 10000, 10, ""},
    };
    auto* playerTitle = new QLabel(tr("<b>Player</b>"), levelPanel);
    levelForm->addRow(playerTitle);
    for (const PlayerField& f : playerFields) {
        auto* spin = new QDoubleSpinBox(levelPanel);
        spin->setRange(f.min, f.max);
        spin->setSingleStep(f.step);
        spin->setDecimals(2);
        spin->setSuffix(QString::fromUtf8(f.suffix));
        spin->setKeyboardTracking(false);
        levelForm->addRow(tr(f.label), spin);
        const auto member = f.member;
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double v) {
            if (scene_.player.*member != static_cast<float>(v)) {
                scene_.player.*member = static_cast<float>(v);
                markDirty();
            }
        });
        playerSpins_.emplace_back(spin, member);
    }
    connect(playerStartEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        scene_.playerStart = v;
        markDirty();
    });
    connect(sunEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        if (glm::length(v) > 0.001f) {
            scene_.sunDirection = v;
            markDirty();
        }
    });
    connect(fromCamera, &QPushButton::clicked, this, [this] {
        // Camera is the eye; the player start is the feet position.
        scene_.playerStart = viewport_->camera().position - glm::vec3(0.0f, 1.65f, 0.0f);
        refreshLevelProperties();
        markDirty();
    });

    auto* propsHost = new QWidget(this);
    auto* propsLayout = new QVBoxLayout(propsHost);
    auto* boxTitle = new QLabel(tr("<b>Selected object</b>"), propsHost);
    auto* levelTitle = new QLabel(tr("<b>Level</b>"), propsHost);
    propsLayout->addWidget(boxTitle);
    propsLayout->addWidget(boxPanel_);
    propsLayout->addSpacing(12);
    propsLayout->addWidget(levelTitle);
    propsLayout->addWidget(levelPanel);
    propsLayout->addStretch(1);

    auto* propsDock = new QDockWidget(tr("Properties"), this);
    propsDock->setObjectName("properties");
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(propsHost);
    scroll->setWidgetResizable(true);
    propsDock->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);

    // Asset browser: thumbnails of everything under assets/.
    assets_ = new AssetBrowser(this);
    connect(assets_, &AssetBrowser::assetActivated, this, [this](const QString& path, AssetType type) {
        openAsset(path, static_cast<int>(type));
    });
    auto* assetsDock = new QDockWidget(tr("Assets"), this);
    assetsDock->setObjectName("assets");
    assetsDock->setWidget(assets_);
    addDockWidget(Qt::BottomDockWidgetArea, assetsDock);

    // Weapons: the level's player loadout and the .weapon files it uses.
    weapons_ = new WeaponPanel(&scene_, this);
    connect(weapons_, &WeaponPanel::loadoutChanged, this, &MainWindow::markDirty);
    auto* weaponsDock = new QDockWidget(tr("Weapons"), this);
    weaponsDock->setObjectName("weapons");
    auto* weaponsScroll = new QScrollArea(weaponsDock);
    weaponsScroll->setWidget(weapons_);
    weaponsScroll->setWidgetResizable(true);
    weaponsDock->setWidget(weaponsScroll);
    addDockWidget(Qt::RightDockWidgetArea, weaponsDock);
    tabifyDockWidget(propsDock, weaponsDock);
    propsDock->raise();
}

engine::Box* MainWindow::selectedBox()
{
    return scene_.isBox(selected_) ? &scene_.boxes[static_cast<std::size_t>(selected_)] : nullptr;
}

engine::Model* MainWindow::selectedModel()
{
    return scene_.isModel(selected_) ? &scene_.model(selected_) : nullptr;
}

engine::Entity* MainWindow::selectedEntity()
{
    return scene_.isEntity(selected_) ? &scene_.entity(selected_) : nullptr;
}

void MainWindow::select(int index)
{
    selected_ = (index >= 0 && index < scene_.objectCount()) ? index : -1;
    outliner_->selectObject(selected_);
    dependencies_->showObject(selected_);
    viewport_->setSelected(selected_);
    refreshProperties();
}

void MainWindow::refreshOutliner()
{
    outliner_->rebuild();
    outliner_->selectObject(selected_);
    dependencies_->showObject(selected_);
}

void MainWindow::refreshProperties()
{
    const engine::Box* b = selectedBox();
    const engine::Model* m = selectedModel();
    const engine::Entity* e = selectedEntity();
    boxPanel_->setEnabled(b || m || e);
    sizeEdit_->setEnabled(b || e);
    materialRow_->setEnabled(b != nullptr);
    materialEdit_->setText(b && !b->materialPath.empty() ? QString::fromStdU16String(b->materialPath.stem().u16string()) : QString());
    materialEdit_->setToolTip(b ? QString::fromStdU16String(b->materialPath.u16string()) : QString());
    refreshMaterialSlots();
    colorButton_->setEnabled(b || e);
    typeEdit_->setEnabled(e != nullptr);
    yawSpin_->setEnabled(e != nullptr);
    scriptRow_->setEnabled(e != nullptr);
    refreshCharacter();

    yawSpin_->blockSignals(true);
    yawSpin_->setValue(e ? e->yawDegrees : 0.0);
    yawSpin_->blockSignals(false);
    typeEdit_->setText(e ? QString::fromStdString(e->type) : QString());
    scriptEdit_->setText(e && !e->scriptPath.empty() ? QString::fromStdU16String(e->scriptPath.filename().u16string()) : QString());
    scriptEdit_->setToolTip(e && !e->scriptPath.empty() ? QString::fromStdU16String(e->scriptPath.u16string()) : QString());

    refreshScriptParams();
    if (b) {
        kindLabel_->setText(tr("Box"));
        nameEdit_->setText(QString::fromStdString(b->name));
        positionEdit_->setValue(b->position);
        sizeEdit_->setValue(b->size);
        colorButton_->setStyleSheet(colorStyle(b->color));
    } else if (m) {
        kindLabel_->setText(tr("glTF model: %1").arg(QString::fromStdU16String(m->sourcePath.filename().u16string())));
        nameEdit_->setText(QString::fromStdString(m->name));
        positionEdit_->setValue(m->position);
        colorButton_->setStyleSheet(QString());
    } else if (e) {
        kindLabel_->setText(tr("Entity"));
        nameEdit_->setText(QString::fromStdString(e->name));
        positionEdit_->setValue(e->position);
        sizeEdit_->setValue(e->size);
        colorButton_->setStyleSheet(colorStyle(e->color));
    } else {
        kindLabel_->clear();
        nameEdit_->clear();
        colorButton_->setStyleSheet(QString());
    }
}

void MainWindow::refreshScriptParams()
{
    const engine::Entity* e = selectedEntity();
    scriptProps_.clear();
    std::string error;
    if (e && !e->scriptPath.empty() && !engine::readScriptProperties(e->scriptPath, scriptProps_, &error))
        statusBar()->showMessage(tr("Script error: %1").arg(QString::fromStdString(error)), 8000);

    scriptParams_->blockSignals(true);
    scriptParams_->setRowCount(static_cast<int>(scriptProps_.size()));
    for (int row = 0; row < static_cast<int>(scriptProps_.size()); ++row) {
        const engine::ScriptProperty& p = scriptProps_[static_cast<std::size_t>(row)];
        auto* name = new QTableWidgetItem(QString::fromStdString(p.name));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        name->setToolTip(QString::fromStdString(p.description));
        auto* value = new QTableWidgetItem();
        const auto it = e->properties.find(p.name);
        if (it != e->properties.end()) {
            value->setText(QString::fromStdString(it->second));
            QFont f = value->font();
            f.setBold(true); // changed from the script default
            value->setFont(f);
        } else {
            value->setText(QString::fromStdString(p.defaultValue));
            value->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
        value->setToolTip(tr("Default: %1\n%2").arg(QString::fromStdString(p.defaultValue), QString::fromStdString(p.description)));
        scriptParams_->setItem(row, 0, name);
        scriptParams_->setItem(row, 1, value);
        scriptParams_->removeCellWidget(row, 1);
        // Clip names: pick from the character's animations.
        if (p.name.rfind("anim_", 0) == 0 && e->model && e->model->skeleton && !e->model->skeleton->clips.empty()) {
            auto* combo = new QComboBox(scriptParams_);
            for (const engine::Skeleton::Clip& clip : e->model->skeleton->clips)
                combo->addItem(QString::fromStdString(clip.name));
            if (combo->findText(value->text()) < 0)
                combo->addItem(value->text()); // clip the model does not have: keep it visible
            combo->setCurrentText(value->text());
            if (value->font().bold()) {
                QFont f = combo->font();
                f.setBold(true);
                combo->setFont(f);
            }
            combo->setToolTip(value->toolTip());
            connect(combo, &QComboBox::textActivated, this, [this, row](const QString& text) {
                // Deferred: cellChanged rebuilds the table, which deletes this combo.
                QMetaObject::invokeMethod(this, [this, row, text] {
                    if (auto* item = scriptParams_->item(row, 1); item && item->text() != text)
                        item->setText(text); // goes through cellChanged like a typed value
                }, Qt::QueuedConnection);
            });
            scriptParams_->setCellWidget(row, 1, combo);
        }
    }
    scriptParams_->blockSignals(false);
    scriptParams_->setEnabled(!scriptProps_.empty());
}

void MainWindow::refreshLevelProperties()
{
    playerStartEdit_->setValue(scene_.playerStart);
    sunEdit_->setValue(scene_.sunDirection);
    for (auto& [spin, member] : playerSpins_) {
        spin->blockSignals(true);
        spin->setValue(scene_.player.*member);
        spin->blockSignals(false);
    }
}

void MainWindow::markDirty()
{
    dirty_ = true;
    viewport_->update();
    updateTitle();
}

void MainWindow::updateTitle()
{
    const QString name = currentFile_.isEmpty() ? tr("Untitled") : QFileInfo(currentFile_).fileName();
    setWindowTitle(QString("%1%2 - FPS Engine Editor").arg(name, dirty_ ? "*" : ""));
}

void MainWindow::addBox()
{
    // Drop the new box 6 m in front of the camera, snapped to a 0.5 m grid.
    const auto& cam = viewport_->camera();
    glm::vec3 p = cam.position + cam.forward() * 6.0f;
    p = glm::vec3(std::round(p.x * 2.0f), std::round(p.y * 2.0f), std::round(p.z * 2.0f)) * 0.5f;
    engine::Box box;
    box.name = engine::uniqueNodeName(scene_, "Box " + std::to_string(scene_.boxes.size() + 1));
    box.position = p;
    scene_.boxes.push_back(box);
    refreshOutliner();
    select(static_cast<int>(scene_.boxes.size()) - 1);
    markDirty();
}

void MainWindow::importModel()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import glTF model"), QString(),
                                                      tr("glTF 2.0 (*.gltf *.glb);;All files (*)"));
    if (path.isEmpty())
        return;
    // Place it on the ground in front of the camera.
    const auto& cam = viewport_->camera();
    const glm::vec3 p = cam.position + cam.flatForward() * 8.0f;
    placeModel(path, {std::round(p.x), 0.0f, std::round(p.z)});
}

bool MainWindow::placeModel(const QString& path, glm::vec3 point)
{
    engine::Model model;
    std::string error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = engine::loadModelFile(model, std::filesystem::path(path.toStdU16String()), &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Place model"), QString::fromStdString(error));
        return false;
    }
    QString name = QFileInfo(path).completeBaseName();
    if (name == "scene" || name == "model")
        name = QFileInfo(path).dir().dirName();
    model.name = engine::uniqueNodeName(scene_, name.toStdString());
    // Snap to 0.25 m and stand the model on the point (its lowest vertex touches it).
    const glm::vec3 snapped = glm::round(point * 4.0f) / 4.0f;
    model.position = {snapped.x, point.y - model.localBounds.min.y, snapped.z};
    scene_.models.push_back(std::move(model));
    refreshOutliner();
    select(scene_.firstEntity() - 1);
    markDirty();
    return true;
}

bool MainWindow::assignMaterial(const QString& path, int object)
{
    std::string error;
    const std::filesystem::path file(path.toStdU16String());
    auto material = engine::loadMaterial(file, &error);
    if (!material) {
        QMessageBox::warning(this, tr("Assign material"), QString::fromStdString(error));
        return false;
    }
    if (scene_.isBox(object)) {
        engine::Box& b = scene_.boxes[static_cast<std::size_t>(object)];
        b.materialPath = file;
        b.material = material;
    } else if (scene_.isModel(object)) {
        // Every glTF material slot of the model gets the new material.
        engine::Model& m = scene_.model(object);
        for (const std::string& slot : m.materialSlots()) {
            m.materialOverrides[slot] = file;
            m.overrideMaterials[slot] = material;
        }
    } else {
        return false;
    }
    select(object);
    markDirty();
    statusBar()->showMessage(tr("Material %1 assigned to %2").arg(QFileInfo(path).completeBaseName(),
                                                                  QString::fromStdString(engine::objectName(scene_, object))), 5000);
    return true;
}

void MainWindow::openAsset(const QString& path, int type)
{
    switch (static_cast<AssetType>(type)) {
    case AssetType::Model: {
        const auto& cam = viewport_->camera();
        const glm::vec3 p = cam.position + cam.flatForward() * 8.0f;
        placeModel(path, {p.x, 0.0f, p.z});
        break;
    }
    case AssetType::Material: openMaterialEditor(path); break;
    case AssetType::Texture: openTexture(path); break;
    case AssetType::Script: openScript(path); break;
    case AssetType::Level:
        if (confirmDiscard())
            openFile(path);
        break;
    default: statusBar()->showMessage(path, 5000); break;
    }
}

void MainWindow::addEntity(const QString& type)
{
    const auto& cam = viewport_->camera();
    const glm::vec3 p = cam.position + cam.flatForward() * 6.0f;
    engine::Entity e;
    e.type = type.toStdString();
    e.name = engine::uniqueNodeName(scene_, (type == "enemy" ? "Enemy " : "Entity ") + std::to_string(scene_.entities.size() + 1));
    e.position = {std::round(p.x * 2.0f) * 0.5f, 0.0f, std::round(p.z * 2.0f) * 0.5f};
    if (type == "enemy") {
        e.color = {0.55f, 0.5f, 0.5f};
        // Default AI script shipped next to the editor (copied by the game build).
        const QString script = assetPath("scripts/enemy.lua");
        if (QFileInfo::exists(script))
            e.scriptPath = std::filesystem::path(script.toStdU16String());
        // Default animated character shipped with the engine.
        const QString robot = assetPath("models/characters/robot.glb");
        if (QFileInfo::exists(robot)) {
            e.modelPath = std::filesystem::path(robot.toStdU16String());
            e.model = engine::loadCharacterModel(e.modelPath);
            if (e.model)
                e.animation = "Idle";
            else
                e.modelPath.clear();
        }
    } else {
        e.size = {0.5f, 0.5f, 0.5f};
        e.color = {0.3f, 0.6f, 0.9f};
    }
    scene_.entities.push_back(std::move(e));
    refreshOutliner();
    select(scene_.objectCount() - 1);
    markDirty();
}

void MainWindow::browseScript()
{
    auto* e = selectedEntity();
    if (!e)
        return;
    const QString start = e->scriptPath.empty() ? QString() : QString::fromStdU16String(e->scriptPath.u16string());
    const QString path = QFileDialog::getOpenFileName(this, tr("Lua script"), start, tr("Lua scripts (*.lua);;All files (*)"));
    if (path.isEmpty())
        return;
    e->scriptPath = std::filesystem::path(path.toStdU16String());
    refreshProperties();
    markDirty();
}

void MainWindow::browseCharacter()
{
    auto* e = selectedEntity();
    if (!e)
        return;
    QString start = e->modelPath.empty() ? assetPath("models/characters")
                                         : QString::fromStdU16String(e->modelPath.u16string());
    const QString path = QFileDialog::getOpenFileName(this, tr("Character model"), start,
                                                      tr("Animated glTF (*.glb *.gltf);;All files (*)"));
    if (path.isEmpty())
        return;
    std::string error;
    const std::filesystem::path file(path.toStdU16String());
    auto model = engine::loadCharacterModel(file, &error);
    if (!model) {
        QMessageBox::warning(this, tr("Character model"), QString::fromStdString(error));
        return;
    }
    e->modelPath = file;
    e->model = model;
    e->animator.stop();
    // Start with an idle-looking clip if there is one.
    e->animation.clear();
    for (const char* guess : {"Idle", "idle", "Idle_Loop", "Standing"})
        if (model->skeleton->findClip(guess) >= 0) {
            e->animation = guess;
            break;
        }
    if (e->animation.empty() && !model->skeleton->clips.empty())
        e->animation = model->skeleton->clips.front().name;
    refreshProperties();
    markDirty();
}

void MainWindow::refreshCharacter()
{
    const engine::Entity* e = selectedEntity();
    characterRow_->setEnabled(e != nullptr);
    const bool hasModel = e && e->model;
    modelScaleSpin_->setEnabled(hasModel);
    modelYawSpin_->setEnabled(hasModel);
    characterEdit_->setText(hasModel ? QString::fromStdU16String(e->modelPath.filename().u16string()) : QString());
    characterEdit_->setToolTip(hasModel ? QString::fromStdU16String(e->modelPath.u16string()) : QString());
    for (QDoubleSpinBox* spin : {modelScaleSpin_, modelYawSpin_})
        spin->blockSignals(true);
    modelScaleSpin_->setValue(e ? e->modelScale : 1.0);
    modelYawSpin_->setValue(e ? e->modelYaw : 0.0);
    for (QDoubleSpinBox* spin : {modelScaleSpin_, modelYawSpin_})
        spin->blockSignals(false);

    animationCombo_->blockSignals(true);
    animationCombo_->clear();
    if (hasModel && e->model->skeleton) {
        for (const engine::Skeleton::Clip& clip : e->model->skeleton->clips)
            animationCombo_->addItem(QString::fromStdString(clip.name));
        animationCombo_->setCurrentText(QString::fromStdString(e->animation));
    }
    animationCombo_->setEnabled(animationCombo_->count() > 0);
    animationCombo_->blockSignals(false);
}

void MainWindow::duplicateBox()
{
    if (const engine::Box* b = selectedBox()) {
        engine::Box copy = *b;
        copy.name = engine::uniqueNodeName(scene_, copy.name);
        copy.position.x += copy.size.x;
        scene_.boxes.push_back(copy);
        refreshOutliner();
        select(static_cast<int>(scene_.boxes.size()) - 1);
    } else if (const engine::Model* m = selectedModel()) {
        engine::Model copy = *m; // shares mesh ids, so the GPU data is shared too
        copy.name = engine::uniqueNodeName(scene_, copy.name);
        copy.position.x += copy.localBounds.max.x - copy.localBounds.min.x + 1.0f;
        scene_.models.push_back(std::move(copy));
        refreshOutliner();
        select(scene_.firstEntity() - 1);
    } else if (const engine::Entity* en = selectedEntity()) {
        engine::Entity copy = *en;
        copy.name = engine::uniqueNodeName(scene_, copy.name);
        copy.position.x += 1.0f;
        scene_.entities.push_back(std::move(copy));
        refreshOutliner();
        select(scene_.objectCount() - 1);
    } else {
        return;
    }
    markDirty();
}

void MainWindow::deleteBox()
{
    // Children of the deleted object move up to its parent.
    if (selected_ >= 0)
        engine::reparentChildren(scene_, engine::objectName(scene_, selected_), engine::objectParent(scene_, selected_));
    if (selectedBox())
        scene_.boxes.erase(scene_.boxes.begin() + selected_);
    else if (selectedModel())
        scene_.models.erase(scene_.models.begin() + (selected_ - scene_.firstModel()));
    else if (selectedEntity())
        scene_.entities.erase(scene_.entities.begin() + (selected_ - scene_.firstEntity()));
    else
        return;
    const int next = std::min(selected_, scene_.objectCount() - 1);
    selected_ = -1;
    refreshOutliner();
    select(next);
    markDirty();
}

bool MainWindow::confirmDiscard()
{
    if (!dirty_)
        return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to the level?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::newLevel()
{
    if (!confirmDiscard())
        return;
    scene_ = engine::Scene{};
    scene_.boxes.push_back({"Floor", {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f}, {0.62f, 0.60f, 0.56f}});
    currentFile_.clear();
    dirty_ = false;
    selected_ = -1;
    weapons_->refresh();
    refreshOutliner();
    refreshLevelProperties();
    select(-1);
    updateTitle();
}

void MainWindow::openLevel()
{
    if (!confirmDiscard())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open level"), QString(), tr(kFileFilter));
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    engine::Scene loaded;
    std::string error;
    if (!engine::loadScene(loaded, std::filesystem::path(path.toStdU16String()), &error)) {
        QMessageBox::warning(this, tr("Open level"), QString::fromStdString(error));
        return false;
    }
    scene_ = std::move(loaded);
    weapons_->refresh();
    currentFile_ = path;
    dirty_ = false;
    selected_ = -1;
    refreshOutliner();
    refreshLevelProperties();
    select(-1);
    updateTitle();
    return true;
}

bool MainWindow::save()
{
    return currentFile_.isEmpty() ? saveAs() : saveTo(currentFile_);
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save level"), currentFile_, tr(kFileFilter));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".level";
    return saveTo(path);
}

bool MainWindow::saveTo(const QString& path)
{
    if (!saveAllScripts())
        return false;
    QString weaponError;
    if (!weapons_->saveModified(&weaponError)) {
        QMessageBox::warning(this, tr("Save weapons"), weaponError);
        return false;
    }
    std::string error;
    if (!engine::saveScene(scene_, std::filesystem::path(path.toStdU16String()), &error)) {
        QMessageBox::warning(this, tr("Save level"), QString::fromStdString(error));
        return false;
    }
    currentFile_ = path;
    dirty_ = false;
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(path), 4000);
    return true;
}

void MainWindow::playLevel()
{
    // The game runs as a separate process on a snapshot of the current level,
    // so a crash in gameplay code never takes the editor down.
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString snapshot = QDir(tempDir).filePath("fps_engine_play.level");
    if (!saveAllScripts())
        return;
    QString weaponError;
    if (!weapons_->saveModified(&weaponError)) {
        QMessageBox::warning(this, tr("Play"), weaponError);
        return;
    }
    std::string error;
    if (!engine::saveScene(scene_, std::filesystem::path(snapshot.toStdU16String()), &error)) {
        QMessageBox::warning(this, tr("Play"), QString::fromStdString(error));
        return;
    }
#ifdef Q_OS_WIN
    const QString exe = QDir(QCoreApplication::applicationDirPath()).filePath("fps_game.exe");
#else
    const QString exe = QDir(QCoreApplication::applicationDirPath()).filePath("fps_game");
#endif
    if (!QFileInfo::exists(exe) || !QProcess::startDetached(exe, {snapshot})) {
        QMessageBox::warning(this, tr("Play"), tr("Could not start %1").arg(exe));
    }
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (confirmCloseScripts() && confirmDiscard())
        e->accept();
    else
        e->ignore();
}

ScriptEditor* MainWindow::currentScript() const
{
    return qobject_cast<ScriptEditor*>(tabs_->currentWidget());
}

ScriptEditor* MainWindow::openScript(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (int i = 1; i < tabs_->count(); ++i)
        if (auto* se = qobject_cast<ScriptEditor*>(tabs_->widget(i)); se && se->path() == absolute) {
            tabs_->setCurrentIndex(i);
            return se;
        }
    auto* se = new ScriptEditor(tabs_);
    QString error;
    if (!se->load(absolute, &error)) {
        delete se;
        QMessageBox::warning(this, tr("Open script"), tr("%1\n%2").arg(absolute, error));
        return nullptr;
    }
    const int index = tabs_->addTab(se, se->title());
    tabs_->setTabToolTip(index, absolute);
    tabs_->setCurrentIndex(index);
    connect(se, &ScriptEditor::modificationChanged, this, [this, se] {
        if (const int i = tabs_->indexOf(se); i >= 0)
            tabs_->setTabText(i, se->title());
    });
    connect(se, &ScriptEditor::saved, this, [this](const QString& saved) {
        // Parameters the script declares may have changed.
        if (auto* e = selectedEntity(); e && QFileInfo(QString::fromStdU16String(e->scriptPath.u16string())).absoluteFilePath() == saved)
            refreshScriptParams();
        statusBar()->showMessage(tr("Saved %1").arg(saved), 4000);
    });
    se->editor()->setFocus();
    return se;
}

void MainWindow::openScriptDialog()
{
    const QString start = assetPath("scripts");
    const QString path = QFileDialog::getOpenFileName(this, tr("Open script"), start, tr("Lua scripts (*.lua);;All files (*)"));
    if (!path.isEmpty())
        openScript(path);
}

void MainWindow::newScript()
{
    const QString start = assetPath("scripts/new_script.lua");
    const QString path = QFileDialog::getSaveFileName(this, tr("New script"), start, tr("Lua scripts (*.lua)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("New script"), f.errorString());
        return;
    }
    f.write(newScriptTemplate().toUtf8());
    f.close();
    // A selected entity without a script gets the new one right away.
    if (auto* e = selectedEntity(); e && e->scriptPath.empty()) {
        e->scriptPath = std::filesystem::path(path.toStdU16String());
        refreshProperties();
        markDirty();
    }
    openScript(path);
}

bool MainWindow::closeScriptTab(int index)
{
    QWidget* w = tabs_->widget(index);
    auto* se = qobject_cast<ScriptEditor*>(w);
    auto* te = qobject_cast<TextureEditor*>(w);
    auto* me = qobject_cast<MaterialEditor*>(w);
    if (!se && !te && !me)
        return false; // the 3D view stays
    const bool modified = se ? se->isModified() : te ? te->isModified() : me->hasUnsaved();
    if (modified) {
        tabs_->setCurrentIndex(index);
        const QString what = se ? se->path() : te ? te->path() + tr(" (texture settings)") : tr("the changed materials");
        const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to %1?").arg(what),
                                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel)
            return false;
        QString error;
        if (answer == QMessageBox::Save) {
            const bool ok = se ? se->save(&error) : te ? te->save(&error) : me->saveModified(&error);
            if (!ok) {
                QMessageBox::warning(this, tr("Save"), error);
                return false;
            }
        }
    }
    tabs_->removeTab(index);
    if (me)
        materialEditor_ = nullptr;
    w->deleteLater();
    return true;
}

bool MainWindow::saveAllScripts()
{
    // Scripts, texture settings and materials edited in tabs: everything the game reads.
    for (int i = 1; i < tabs_->count(); ++i) {
        QWidget* w = tabs_->widget(i);
        QString error;
        bool ok = true;
        QString path;
        if (auto* se = qobject_cast<ScriptEditor*>(w); se && se->isModified()) {
            ok = se->save(&error);
            path = se->path();
        } else if (auto* te = qobject_cast<TextureEditor*>(w); te && te->isModified()) {
            ok = te->save(&error);
            path = te->path();
        } else if (auto* me = qobject_cast<MaterialEditor*>(w); me && me->hasUnsaved()) {
            ok = me->saveModified(&error);
        }
        if (!ok) {
            QMessageBox::warning(this, tr("Save"), tr("%1\n%2").arg(path, error));
            return false;
        }
    }
    return true;
}

bool MainWindow::confirmCloseScripts()
{
    for (int i = tabs_->count() - 1; i >= 1; --i) {
        QWidget* w = tabs_->widget(i);
        bool modified = false;
        if (auto* se = qobject_cast<ScriptEditor*>(w))
            modified = se->isModified();
        else if (auto* te = qobject_cast<TextureEditor*>(w))
            modified = te->isModified();
        else if (auto* me = qobject_cast<MaterialEditor*>(w))
            modified = me->hasUnsaved();
        if (modified && !closeScriptTab(i))
            return false;
    }
    return true;
}

MaterialEditor* MainWindow::openMaterialEditor(const QString& material)
{
    if (!materialEditor_) {
        materialEditor_ = new MaterialEditor(tabs_);
        tabs_->addTab(materialEditor_, tr("Materials"));
        connect(materialEditor_, &MaterialEditor::materialChanged, this, [this] {
            viewport_->update();
            refreshProperties();
        });
        connect(materialEditor_, &MaterialEditor::editTexture, this, &MainWindow::openTexture);
    }
    if (!material.isEmpty())
        materialEditor_->open(material);
    tabs_->setCurrentWidget(materialEditor_);
    return materialEditor_;
}

TextureEditor* MainWindow::openTexture(const QString& image)
{
    const QString absolute = QFileInfo(image).absoluteFilePath();
    for (int i = 1; i < tabs_->count(); ++i)
        if (auto* te = qobject_cast<TextureEditor*>(tabs_->widget(i)); te && te->path() == absolute) {
            tabs_->setCurrentIndex(i);
            return te;
        }
    auto* te = new TextureEditor(tabs_);
    QString error;
    if (!te->load(absolute, &error)) {
        delete te;
        QMessageBox::warning(this, tr("Texture"), error);
        return nullptr;
    }
    const int index = tabs_->addTab(te, te->title());
    tabs_->setTabToolTip(index, absolute);
    tabs_->setCurrentIndex(index);
    connect(te, &TextureEditor::textureChanged, viewport_, qOverload<>(&QWidget::update));
    connect(te, &TextureEditor::modificationChanged, this, [this, te] {
        if (const int i = tabs_->indexOf(te); i >= 0)
            tabs_->setTabText(i, te->title());
    });
    return te;
}

void MainWindow::saveCurrent()
{
    QString error;
    bool ok = true;
    if (auto* se = currentScript())
        ok = se->save(&error);
    else if (auto* te = qobject_cast<TextureEditor*>(tabs_->currentWidget()))
        ok = te->save(&error);
    else if (auto* me = qobject_cast<MaterialEditor*>(tabs_->currentWidget()))
        ok = me->saveModified(&error);
    else {
        save();
        return;
    }
    if (!ok)
        QMessageBox::warning(this, tr("Save"), error);
}

void MainWindow::browseBoxMaterial()
{
    auto* b = selectedBox();
    if (!b)
        return;
    const QString start = b->materialPath.empty() ? assetPath("materials") : QString::fromStdU16String(b->materialPath.u16string());
    const QString path = QFileDialog::getOpenFileName(this, tr("Material"), start, tr("Materials (*.material)"));
    if (path.isEmpty())
        return;
    std::string error;
    auto material = engine::loadMaterial(std::filesystem::path(path.toStdU16String()), &error);
    if (!material) {
        QMessageBox::warning(this, tr("Material"), QString::fromStdString(error));
        return;
    }
    b->materialPath = material->sourcePath;
    b->material = std::move(material);
    refreshProperties();
    markDirty();
}

void MainWindow::refreshMaterialSlots()
{
    const engine::Model* m = selectedModel();
    const std::vector<std::string> slotNames = m ? m->materialSlots() : std::vector<std::string>{};
    materialSlots_->setVisible(m != nullptr);
    materialSlots_->setRowCount(static_cast<int>(slotNames.size()));
    for (int row = 0; row < static_cast<int>(slotNames.size()); ++row) {
        const std::string& slot = slotNames[static_cast<std::size_t>(row)];
        materialSlots_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(slot)));
        const auto it = m->materialOverrides.find(slot);
        auto* value = new QTableWidgetItem(it == m->materialOverrides.end() ? tr("(from glTF)")
                                                                            : QString::fromStdU16String(it->second.stem().u16string()));
        if (it == m->materialOverrides.end())
            value->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        else
            value->setToolTip(QString::fromStdU16String(it->second.u16string()));
        materialSlots_->setItem(row, 1, value);
    }
}
