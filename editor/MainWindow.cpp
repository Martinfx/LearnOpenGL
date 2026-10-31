#include "MainWindow.h"

#include <engine/Memory.h>
#include <QCheckBox>
#include <engine/TextureCook.h>
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
#include "EnvironmentPanel.h"
#include "ShaderEditor.h"
#include "SoundEditor.h"
#include "SurfacesEditor.h"
#include "ParticleEditor.h"
#include "EditorUi.h"
#include "ViewportPanel.h"

#include <QAction>
#include <QApplication>
#include <QActionGroup>
#include <QComboBox>
#include <QPointer>
#include <engine/JobSystem.h>
#include <engine/Log.h>
#include <QUndoStack>
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
#include <QInputDialog>
#include <QLocale>
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
#include <QRandomGenerator>
#include <QSettings>
#include <QShortcut>
#include <QTabBar>
#include <QTabWidget>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include <QCompleter>
#include <QDirIterator>
#include <QFontDatabase>
#include <QGridLayout>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QStackedWidget>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <array>
#include <QSignalBlocker>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#else
#include <csignal>
#include <sys/types.h>
#endif
#include <memory>

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

    viewportPanel_ = new ViewportPanel(&scene_, this);
    viewport_ = viewportPanel_->viewport();
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->addTab(viewportPanel_, tr("Level"));
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
                notify(tr("Drop a material onto a box or a model."), 5000);
            break;
        default: openAsset(path, static_cast<int>(assetTypeOf(path))); break;
        }
    });
    connect(viewport_, &Viewport::rendererReady, this,
            [this](const QString& info) {
                notify(info, 8000);
#ifndef NDEBUG
                appendLog(tr("Debug build (no optimisation): big levels render much slower. Build with -DCMAKE_BUILD_TYPE=Release."), 1);
#endif
            });
    connect(viewport_, &Viewport::renderWarning, this, [this](const QString& message) {
        qWarning("%s", qPrintable(message));
        notify(message, 15000);
    });
    connect(viewport_, &Viewport::rendererFailed, this, [this](const QString& error) {
        QMessageBox::critical(this, tr("OpenGL"), tr("Renderer could not start:\n%1").arg(error));
    });

    buildMenus();
    buildDocks();
    buildStatusBar();
    connect(viewportPanel_->transformBox(), &TransformBox::edited, this, &MainWindow::applyTransformBox);
    refreshOutliner();
    refreshLevelProperties();
    select(-1);
    resetHistory();
    updateTitle();
    notify(tr("Right mouse + WASD/QE: fly · Left click: select · F: focus · F5: play"));
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
    history_ = new QUndoStack(this);
    history_->setUndoLimit(200);
    QAction* undo = history_->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = history_->createRedoAction(this, tr("&Redo"));
    redo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    QAction* add = edit->addAction(tr("&Add box"), QKeySequence(Qt::CTRL | Qt::Key_B), this, &MainWindow::addBox);
    QAction* import = edit->addAction(tr("&Import glTF model..."), QKeySequence(Qt::CTRL | Qt::Key_I), this, &MainWindow::importModel);
    QAction* enemy = edit->addAction(tr("Add &enemy"), QKeySequence(Qt::CTRL | Qt::Key_E), this, [this] { addEntity("enemy"); });
    edit->addAction(tr("Add empty en&tity"), this, [this] { addEntity("empty"); });
    edit->addAction(tr("Add &sound source"), this, [this] { addEntity("sound"); });
    edit->addAction(tr("Add &particle effect"), this, [this] { addEntity("particle"); });
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
    assets->addAction(tr("S&urfaces (footsteps, impacts)"), this, [this] { openSurfacesEditor(); });
    assets->addAction(tr("&Particle effect..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open particle effect"), assetPath("particles"),
                                                          tr("Particle effects (*.particle)"));
        if (!path.isEmpty())
            openParticleEditor(path);
    });
    assets->addAction(tr("&New particle effect..."), this, [this] { newParticleEffect(QString()); });
    assets->addAction(tr("Edit &texture..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Edit texture"), assetPath("textures"),
                                                          tr("Images (*.png *.jpg *.jpeg)"));
        if (!path.isEmpty())
            openTexture(path);
    });

    QMenu* game = menuBar()->addMenu(tr("&Game"));
    QAction* play = game->addAction(tr("&Play level"), QKeySequence(Qt::Key_F5), this, &MainWindow::playLevel);
    playAction_ = play;

    // View: perspective or orthographic top / front / side, or all four at once.
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* viewGroup = new QActionGroup(this);
    const std::pair<QString, int> views[] = {{tr("&Perspective"), 0}, {tr("&Top"), 1}, {tr("&Front"), 2}, {tr("&Side"), 3}, {tr("&4 views"), 4}};
    for (const auto& [label, index] : views) {
        QAction* a = viewMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(index == 0);
        a->setShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + index)));
        viewGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, index] { viewportPanel_->setView(index); });
    }
    connect(viewportPanel_, &ViewportPanel::viewChanged, this, [viewGroup](int index) {
        if (QAction* a = viewGroup->actions().value(index))
            a->setChecked(true);
    });
    viewMenu->addSeparator();
    const char* shadingNames[4] = {"&Lit", "&Unlit", "Lighting &only", "&Wireframe"};
    auto* shadingGroup = new QActionGroup(this);
    for (int i = 0; i < 4; ++i) {
        QAction* a = viewMenu->addAction(tr(shadingNames[i]));
        a->setCheckable(true);
        a->setChecked(i == 0);
        a->setShortcut(QKeySequence(Qt::ALT | static_cast<Qt::Key>(Qt::Key_1 + i)));
        shadingGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] { viewportPanel_->setShading(i); });
    }
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Orthographic views: right or middle drag pans, wheel zooms"))->setEnabled(false);

    // Render: quality of this machine's viewport (saved in the editor settings) and shaders.
    QMenu* render = menuBar()->addMenu(tr("&Render"));
    auto* qualityGroup = new QActionGroup(this);
    const char* qualityNames[4] = {"Low", "Medium", "High", "Ultra"};
    QSettings settings;
    const int qualityLevel = settings.value("render/quality", 2).toInt();
    engine::RenderQuality quality = engine::RenderQuality::preset(qualityLevel);
    quality.ssao = settings.value("render/ssao", quality.ssao).toBool();
    quality.bloom = settings.value("render/bloom", quality.bloom).toBool();
    quality.fxaa = settings.value("render/fxaa", quality.fxaa).toBool();
    QMenu* qualityMenu = render->addMenu(tr("&Quality"));
    auto toggles = std::make_shared<std::array<QAction*, 3>>(); // SSAO, bloom, FXAA
    for (int i = 0; i < 4; ++i) {
        QAction* a = qualityMenu->addAction(tr(qualityNames[i]));
        a->setCheckable(true);
        a->setChecked(i == qualityLevel);
        qualityGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, i, toggles] {
            QSettings().setValue("render/quality", i);
            const engine::RenderQuality q = engine::RenderQuality::preset(i);
            for (QAction* t : *toggles)
                t->blockSignals(true);
            (*toggles)[0]->setChecked(q.ssao);
            (*toggles)[1]->setChecked(q.bloom);
            (*toggles)[2]->setChecked(q.fxaa);
            for (QAction* t : *toggles)
                t->blockSignals(false);
            setRenderQuality(q);
        });
    }
    const char* toggleNames[3] = {"Ambient occlusion (SSAO)", "Bloom", "Anti-aliasing (FXAA)"};
    const char* toggleKeys[3] = {"render/ssao", "render/bloom", "render/fxaa"};
    bool engine::RenderQuality::*toggleMembers[3] = {&engine::RenderQuality::ssao, &engine::RenderQuality::bloom,
                                                     &engine::RenderQuality::fxaa};
    for (int i = 0; i < 3; ++i) {
        (*toggles)[static_cast<std::size_t>(i)] = render->addAction(tr(toggleNames[i]));
        (*toggles)[static_cast<std::size_t>(i)]->setCheckable(true);
        (*toggles)[static_cast<std::size_t>(i)]->setChecked(quality.*toggleMembers[i]);
        connect((*toggles)[static_cast<std::size_t>(i)], &QAction::toggled, this, [this, i, toggleKeys, toggleMembers](bool on) {
            QSettings().setValue(toggleKeys[i], on);
            engine::RenderQuality q = viewport_->quality();
            q.*toggleMembers[i] = on;
            setRenderQuality(q);
        });
    }
    // Texture memory: size limit on top of the preset (drops top mip levels, no re-cook).
    QMenu* texMenu = render->addMenu(tr("&Texture size"));
    auto* texGroup = new QActionGroup(this);
    const int texMax = settings.value("render/texture_max", quality.textureMaxSize).toInt();
    quality.textureMaxSize = texMax;
    for (int size : {0, 4096, 2048, 1024, 512}) {
        QAction* a = texMenu->addAction(size ? tr("%1 px").arg(size) : tr("Full"));
        a->setCheckable(true);
        a->setChecked(size == texMax);
        texGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, size] {
            QSettings().setValue("render/texture_max", size);
            engine::RenderQuality q = viewport_->quality();
            q.textureMaxSize = size;
            setRenderQuality(q);
        });
    }
    render->addSeparator();
    render->addAction(tr("&Shader editor"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), this, &MainWindow::openShaderEditor);
    render->addAction(tr("&Reload shaders from disk"), QKeySequence(Qt::CTRL | Qt::Key_F5), this, &MainWindow::applyShaders);
    viewport_->setQuality(quality);

    // Main tool bar: save, history, add, play controls, quality.
    QToolBar* main = addToolBar(tr("Main"));
    main->setObjectName("main");
    main->setIconSize(QSize(18, 18));
    main->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    main->setMovable(false);
    QAction* saveAction = main->addAction(editorIcon("save"), QString(), this, &MainWindow::saveCurrent);
    saveAction->setToolTip(tr("Save (Ctrl+S)"));
    undo->setIcon(editorIcon("undo"));
    redo->setIcon(editorIcon("redo"));
    main->addAction(undo);
    main->addAction(redo);
    main->addSeparator();
    // Add menu, shared with the outliner's Add button.
    addMenu_ = new QMenu(tr("Add"), this);
    add->setIcon(editorIcon("cube"));
    enemy->setIcon(editorIcon("person"));
    import->setIcon(editorIcon("model"));
    addMenu_->addAction(add);
    addMenu_->addAction(enemy);
    addMenu_->addAction(tr("Empty entity"), this, [this] { addEntity("empty"); });
    addMenu_->addAction(tr("Map marker (point on the in-game map)"), this, [this] { addEntity("map_marker"); });
    addMenu_->addSeparator();
    addMenu_->addAction(import);
    auto* addButton = new QToolButton(main);
    addButton->setText(tr("Add"));
    addButton->setIcon(editorIcon("add"));
    addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addButton->setPopupMode(QToolButton::InstantPopup);
    addButton->setMenu(addMenu_);
    main->addWidget(addButton);
    main->addSeparator();
    play->setIcon(editorIcon("play"));
    play->setToolTip(tr("Play the level in the game (F5)"));
    main->addAction(play);
    pauseAction_ = main->addAction(editorIcon("pause"), QString(), this, &MainWindow::pauseGame);
    pauseAction_->setCheckable(true);
    pauseAction_->setToolTip(tr("Pause / resume the running game (F6)"));
    pauseAction_->setShortcut(QKeySequence(Qt::Key_F6));
    stopAction_ = main->addAction(editorIcon("stop"), QString(), this, &MainWindow::stopGame);
    stopAction_->setToolTip(tr("Stop the running game (Shift+F5)"));
    stopAction_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    game->addAction(pauseAction_);
    game->addAction(stopAction_);
    pauseAction_->setText(tr("Pause"));
    stopAction_->setText(tr("Stop"));
    main->addSeparator();
    auto* qualityLabel = new QLabel(tr("  Quality "), main);
    qualityLabel->setObjectName("dim");
    main->addWidget(qualityLabel);
    auto* qualityCombo = new QComboBox(main);
    for (const char* name : qualityNames)
        qualityCombo->addItem(tr(name));
    qualityCombo->setCurrentIndex(qualityLevel);
    qualityCombo->setToolTip(tr("Viewport quality of this machine (shadows, SSAO, bloom)"));
    main->addWidget(qualityCombo);
    connect(qualityCombo, &QComboBox::currentIndexChanged, this, [qualityGroup](int i) {
        if (QAction* a = qualityGroup->actions().value(i); a && !a->isChecked())
            a->trigger();
    });
    connect(qualityGroup, &QActionGroup::triggered, qualityCombo, [qualityGroup, qualityCombo](QAction* a) {
        qualityCombo->blockSignals(true);
        qualityCombo->setCurrentIndex(static_cast<int>(qualityGroup->actions().indexOf(a)));
        qualityCombo->blockSignals(false);
    });
    updatePlayButtons();

    // Second row: editor modes (they switch the tool panel on the left) and quick actions.
    addToolBarBreak();
    QToolBar* modes = addToolBar(tr("Modes"));
    modes->setObjectName("modes");
    modes->setIconSize(QSize(16, 16));
    modes->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    modes->setMovable(false);
    auto* modeGroup = new QActionGroup(this);
    const std::pair<const char*, const char*> modeList[] = {
        {"Select", "select"}, {"Place", "add"}, {"Model", "cube"}, {"Landscape", "landscape"}, {"Foliage", "foliage"}};
    for (int i = 0; i < 5; ++i) {
        QAction* a = modes->addAction(editorIcon(modeList[i].second), tr(modeList[i].first));
        a->setCheckable(true);
        a->setChecked(i == 2);
        a->setShortcut(QKeySequence(Qt::SHIFT | static_cast<Qt::Key>(Qt::Key_1 + i)));
        a->setToolTip(tr("%1 mode (Shift+%2)").arg(tr(modeList[i].first)).arg(i + 1));
        modeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] { setEditorMode(i); });
        modeActions_.push_back(a);
    }
    modes->addSeparator();
    QAction* focus = modes->addAction(editorIcon("focus"), tr("Focus"));
    focus->setToolTip(tr("Frame the selection (F)"));
    connect(focus, &QAction::triggered, this, [this] { viewport_->focusSelected(); });
    QAction* drop = modes->addAction(editorIcon("floor"), tr("To floor"));
    drop->setShortcut(QKeySequence(Qt::Key_End));
    drop->setToolTip(tr("Put the selection on what is below it (End)"));
    connect(drop, &QAction::triggered, this, &MainWindow::dropToFloor);
    dup->setIcon(editorIcon("duplicate"));
    del->setIcon(editorIcon("delete"));
    modes->addAction(dup);
    modes->addAction(del);

    // View options menu of the viewport tool bar.
    QMenu* options = viewportPanel_->optionsMenu();
    options->addAction(focus);
    options->addAction(drop);
    options->addSeparator();
    navShowAction_ = options->addAction(tr("Navigation mesh (where AI can walk)"));
    navShowAction_->setCheckable(true);
    navShowAction_->setShortcut(QKeySequence(Qt::Key_P));
    connect(navShowAction_, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("view/navmesh", on);
        if (on && (navStale_ || !navMesh_))
            buildNavMesh();
        viewport_->setDebugGeometry(on ? &navDebug_ : nullptr);
    });
    if (QSettings().value("view/navmesh", false).toBool())
        QTimer::singleShot(0, navShowAction_, [this] { navShowAction_->setChecked(true); });
    QAction* gridAction = options->addAction(tr("Metre grid (1 m / 10 m)"));
    gridAction->setCheckable(true);
    gridAction->setShortcut(QKeySequence(Qt::Key_G));
    gridAction->setChecked(QSettings().value("view/grid", true).toBool());
    viewport_->setShowGrid(gridAction->isChecked());
    connect(gridAction, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("view/grid", on);
        viewport_->setShowGrid(on);
    });
    QAction* reference = options->addAction(tr("Reference human 1.8 m at player start"));
    reference->setCheckable(true);
    reference->setChecked(QSettings().value("view/reference", false).toBool());
    viewport_->setShowReference(reference->isChecked());
    connect(reference, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("view/reference", on);
        viewport_->setShowReference(on);
    });
    QAction* orthoWire = options->addAction(tr("Wireframe in orthographic views"));
    orthoWire->setCheckable(true);
    orthoWire->setChecked(QSettings().value("view/orthoWireframe", true).toBool());
    viewport_->setOrthoWireframe(orthoWire->isChecked());
    connect(orthoWire, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("view/orthoWireframe", on);
        viewport_->setOrthoWireframe(on);
    });
    options->addSeparator();
    options->addMenu(qualityMenu);
    for (QAction* t : *toggles)
        options->addAction(t);
    options->addSeparator();
    options->addAction(tr("Player start = camera position"), this, [this] {
        scene_.playerStart = viewport_->camera().position - glm::vec3(0.0f, scene_.player.eyeHeight, 0.0f);
        refreshLevelProperties();
        markDirty();
    });

    // Gizmo edits: children follow, history records the whole drag as one step.
    connect(viewport_, &Viewport::moveRequested, this, [this](int object, glm::vec3 delta) {
        for (int child : engine::descendantObjects(scene_, engine::objectName(scene_, object)))
            engine::translateObject(scene_, child, delta);
        engine::translateObject(scene_, object, delta);
        refreshProperties();
        dirty_ = true;
    });
    connect(viewport_, &Viewport::rotateRequested, this, [this](int object, glm::vec3 degrees) {
        if (scene_.isModel(object)) {
            scene_.model(object).rotation += degrees;
            scene_.model(object).rotation.y = std::fmod(scene_.model(object).rotation.y, 360.0f);
        } else if (scene_.isEntity(object)) {
            scene_.entity(object).yawDegrees -= degrees.y;
        } else if (scene_.isBox(object)) {
            glm::vec3& r = scene_.boxes[static_cast<std::size_t>(object)].rotation;
            r += degrees;
            for (int i = 0; i < 3; ++i)
                r[i] = std::fmod(r[i], 360.0f);
        }
        refreshProperties();
        dirty_ = true;
    });
    connect(viewport_, &Viewport::scaleRequested, this, [this](int object, glm::vec3 f) {
        if (scene_.isModel(object)) {
            const float uniform = f.x != 1.0f ? f.x : (f.y != 1.0f ? f.y : f.z);
            scene_.model(object).scale = std::max(0.01f, scene_.model(object).scale * uniform);
        } else if (scene_.isBox(object)) {
            auto& b = scene_.boxes[static_cast<std::size_t>(object)];
            b.size = glm::max(b.size * f, glm::vec3(0.01f));
        } else if (scene_.isEntity(object)) {
            auto& e = scene_.entity(object);
            e.size = glm::max(e.size * f, glm::vec3(0.05f));
        }
        refreshProperties();
        dirty_ = true;
    });
    connect(viewport_, &Viewport::dragFinished, this, [this](int) {
        dependencies_->showObject(selected_);
        markDirty();
    });
}

void MainWindow::dropToFloor()
{
    // Puts the selection on whatever is below it (or y = 0).
    if (selected_ < 0)
        return;
    const engine::Aabb b = scene_.objectBounds(selected_);
    const glm::vec3 origin((b.min.x + b.max.x) * 0.5f, b.min.y - 0.001f, (b.min.z + b.max.z) * 0.5f);
    float floorY = 0.0f;
    float best = 1e30f;
    for (int i = 0; i < scene_.objectCount(); ++i) {
        if (i == selected_)
            continue;
        const engine::Aabb o = scene_.objectBounds(i);
        if (origin.x >= o.min.x && origin.x <= o.max.x && origin.z >= o.min.z && origin.z <= o.max.z && o.max.y <= origin.y + 0.002f &&
            origin.y - o.max.y < best) {
            best = origin.y - o.max.y;
            floorY = o.max.y;
        }
    }
    const glm::vec3 delta(0.0f, floorY - b.min.y, 0.0f);
    for (int child : engine::descendantObjects(scene_, engine::objectName(scene_, selected_)))
        engine::translateObject(scene_, child, delta);
    engine::translateObject(scene_, selected_, delta);
    refreshProperties();
    markDirty();
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
    outlinerPanel_ = new OutlinerPanel(outliner_, this);
    outlinerPanel_->setAddMenu(addMenu_, editorIcon("add"));
    auto* outlinerDock = new QDockWidget(tr("Outliner"), this);
    outlinerDock->setObjectName("outliner");
    outlinerDock->setWidget(outlinerPanel_);
    addDockWidget(Qt::RightDockWidgetArea, outlinerDock);

    // What the selected object uses and what else uses the same files.
    dependencies_ = new DependencyPanel(&scene_, this);
    connect(dependencies_, &DependencyPanel::objectActivated, this, [this](int object) { select(object); });
    connect(dependencies_, &DependencyPanel::assetActivated, this, [this](const QString& path, int kind) {
        switch (static_cast<engine::AssetKind>(kind)) {
        case engine::AssetKind::Material: openMaterialEditor(path); break;
        case engine::AssetKind::Texture: openTexture(path); break;
        case engine::AssetKind::Script: openScript(path); break;
        default:
            notify(path, 5000);
            break;
        }
    });
    auto* depsDock = new QDockWidget(tr("Relations"), this);
    depsDock->setObjectName("relations");
    depsDock->setWidget(dependencies_);
    addDockWidget(Qt::RightDockWidgetArea, depsDock);

    // Properties of the selected box.
    boxPanel_ = new QWidget(this);
    auto* sectionsLayout = new QVBoxLayout(boxPanel_);
    sectionsLayout->setContentsMargins(0, 0, 0, 0);
    sectionsLayout->setSpacing(0);
    auto makeSection = [&](const QString& title, const QString& category) {
        auto* section = new CollapsibleSection(title, boxPanel_);
        auto* f = new QFormLayout();
        f->setLabelAlignment(Qt::AlignLeft);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        section->setBodyLayout(f);
        sectionsLayout->addWidget(section);
        detailSections_.push_back({section, f, category});
        return f;
    };
    QFormLayout* generalForm = makeSection(tr("General"), "General");
    QFormLayout* transformForm = makeSection(tr("Transform"), "Transform");
    QFormLayout* renderingForm = makeSection(tr("Rendering"), "Rendering");
    QFormLayout* characterForm = makeSection(tr("Character"), "Character");
    QFormLayout* scriptForm = makeSection(tr("Script"), "Script");
    QFormLayout* soundForm = makeSection(tr("Sound"), "Sound");
    effectForm_ = makeSection(tr("Particle effect"), "Rendering");
    nameEdit_ = new QLineEdit(boxPanel_);
    kindLabel_ = new QLabel(boxPanel_);
    generalForm->addRow(tr("Type"), kindLabel_);
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
    generalForm->addRow(tr("Entity type"), typeEdit_);
    transformForm->addRow(tr("Position"), positionEdit_);
    rotationEdit_ = new Vec3Editor(-360.0, 360.0, 15.0, boxPanel_);
    rotationEdit_->setToolTip(tr("Degrees around X, Y and Z"));
    scaleSpin_ = new QDoubleSpinBox(boxPanel_);
    scaleSpin_->setRange(0.01, 100.0);
    scaleSpin_->setSingleStep(0.1);
    scaleSpin_->setDecimals(3);
    scaleSpin_->setKeyboardTracking(false);
    transformForm->addRow(tr("Yaw"), yawSpin_);
    transformForm->addRow(tr("Rotation"), rotationEdit_);
    transformForm->addRow(tr("Scale"), scaleSpin_);
    transformForm->addRow(tr("Size"), sizeEdit_);
    connect(rotationEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        if (auto* m = selectedModel()) {
            m->rotation = v;
            markDirty();
        } else if (auto* b = selectedBox()) {
            b->rotation = v;
            markDirty();
        }
    });
    connect(scaleSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (auto* m = selectedModel(); m && std::abs(m->scale - static_cast<float>(v)) > 1e-6f) {
            m->scale = static_cast<float>(v);
            markDirty();
        }
    });
    renderingForm->addRow(tr("Color"), colorButton_);

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
    renderingForm->addRow(tr("Material"), materialRow_);
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
    renderingForm->addRow(materialSlots_);

    // Distance LOD of glTF models: simpler file drawn beyond the distance.
    lodRow_ = new QWidget(boxPanel_);
    auto* lodLayout = new QHBoxLayout(lodRow_);
    lodLayout->setContentsMargins(0, 0, 0, 0);
    lodEdit_ = new QLineEdit(lodRow_);
    lodEdit_->setReadOnly(true);
    lodEdit_->setPlaceholderText(tr("no LOD"));
    auto* browseLod = new QPushButton(tr("..."), lodRow_);
    auto* clearLod = new QPushButton(tr("x"), lodRow_);
    browseLod->setFixedWidth(32);
    clearLod->setFixedWidth(32);
    lodLayout->addWidget(lodEdit_, 1);
    lodLayout->addWidget(browseLod);
    lodLayout->addWidget(clearLod);
    lodDistanceSpin_ = new QDoubleSpinBox(boxPanel_);
    lodDistanceSpin_->setRange(5.0, 5000.0);
    lodDistanceSpin_->setSingleStep(10.0);
    lodDistanceSpin_->setSuffix(QStringLiteral(" m"));
    lodDistanceSpin_->setKeyboardTracking(false);
    lodDistanceSpin_->setToolTip(tr("Camera distance to the model's center where the LOD replaces it"));
    renderingForm->addRow(tr("LOD model"), lodRow_);
    renderingForm->addRow(tr("LOD distance"), lodDistanceSpin_);
    // Farther LOD levels (each with its own distance).
    auto* farRow = new QWidget(boxPanel_);
    auto* farLayout = new QHBoxLayout(farRow);
    farLayout->setContentsMargins(0, 0, 0, 0);
    farLodEdit_ = new QLineEdit(farRow);
    farLodEdit_->setReadOnly(true);
    farLodEdit_->setPlaceholderText(tr("none"));
    auto* addFar = new QPushButton(tr("+"), farRow);
    auto* clearFar = new QPushButton(tr("x"), farRow);
    addFar->setFixedWidth(32);
    clearFar->setFixedWidth(32);
    addFar->setToolTip(tr("Add a farther, simpler LOD level (needs the first LOD)"));
    clearFar->setToolTip(tr("Remove the farther LOD levels"));
    farLayout->addWidget(farLodEdit_, 1);
    farLayout->addWidget(addFar);
    farLayout->addWidget(clearFar);
    renderingForm->addRow(tr("Far LODs"), farRow);
    connect(clearFar, &QPushButton::clicked, this, [this] {
        if (auto* m = selectedModel(); m && !m->farLods.empty()) {
            m->farLods.clear();
            refreshProperties();
            markDirty();
        }
    });
    connect(addFar, &QPushButton::clicked, this, [this] {
        auto* m = selectedModel();
        if (!m || !m->lod) {
            notify(tr("Set the first LOD model first."), 4000);
            return;
        }
        const QString start = QString::fromStdU16String(m->lodPath.parent_path().u16string());
        const QString path = QFileDialog::getOpenFileName(this, tr("Far LOD model"), start, tr("glTF (*.glb *.gltf)"));
        if (path.isEmpty())
            return;
        const float last = m->farLods.empty() ? m->lodDistance : m->farLods.back().distance;
        bool ok = false;
        const double distance = QInputDialog::getDouble(this, tr("Far LOD"), tr("Used beyond (m):"), last * 2.0, last + 1.0, 100000.0, 0, &ok);
        if (!ok)
            return;
        const std::string name = m->name;
        QPointer<MainWindow> self(this);
        engine::JobSystem::global().run([self, path, name, distance] {
            auto lod = std::make_shared<engine::Model>();
            std::string error;
            const bool loaded = engine::loadModelFile(*lod, std::filesystem::path(path.toStdU16String()), &error);
            QMetaObject::invokeMethod(qApp, [self, path, name, lod, loaded, error, distance] {
                if (!self)
                    return;
                if (!loaded) {
                    QMessageBox::warning(self, tr("Far LOD"), QString::fromStdString(error));
                    return;
                }
                const int object = engine::findObject(self->scene_, name);
                if (object < 0 || !self->scene_.isModel(object))
                    return;
                engine::Model::FarLod far;
                far.path = std::filesystem::path(path.toStdU16String());
                far.model = lod;
                far.distance = static_cast<float>(distance);
                engine::Model& target = self->scene_.model(object);
                target.farLods.push_back(std::move(far));
                std::sort(target.farLods.begin(), target.farLods.end(),
                          [](const auto& a, const auto& b) { return a.distance < b.distance; });
                self->refreshProperties();
                self->markDirty();
            });
        });
    });
    cullDistanceSpin_ = new QDoubleSpinBox(boxPanel_);
    cullDistanceSpin_->setRange(0.0, 10000.0);
    cullDistanceSpin_->setSingleStep(10.0);
    cullDistanceSpin_->setSuffix(QStringLiteral(" m"));
    cullDistanceSpin_->setSpecialValueText(tr("always drawn"));
    cullDistanceSpin_->setKeyboardTracking(false);
    cullDistanceSpin_->setToolTip(tr("Not drawn farther than this from the camera (small props, grass, debris); 0 = always"));
    renderingForm->addRow(tr("Cull distance"), cullDistanceSpin_);
    collisionCheck_ = new QCheckBox(tr("Collision (blocks the player and bullets)"), boxPanel_);
    collisionCheck_->setToolTip(tr("Off for grass, leaves and small debris: cheaper physics, walk and shoot through"));
    renderingForm->addRow(tr("Physics"), collisionCheck_);
    shadowsCheck_ = new QCheckBox(tr("Cast shadows"), boxPanel_);
    shadowsCheck_->setToolTip(tr("Off for grass and small plants: shadow maps are the most expensive pass"));
    renderingForm->addRow(tr("Shadows"), shadowsCheck_);
    connect(shadowsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (auto* m = selectedModel(); m && m->castShadows != on) {
            m->castShadows = on;
            markDirty();
        }
    });
    connect(cullDistanceSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (auto* m = selectedModel(); m && m->cullDistance != static_cast<float>(v)) {
            m->cullDistance = static_cast<float>(v);
            markDirty();
        }
    });
    connect(collisionCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (auto* m = selectedModel(); m && m->collision != on) {
            m->collision = on;
            markDirty();
        }
    });
    connect(lodDistanceSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (auto* m = selectedModel(); m && m->lodDistance != static_cast<float>(v)) {
            m->lodDistance = static_cast<float>(v);
            markDirty();
        }
    });
    connect(clearLod, &QPushButton::clicked, this, [this] {
        if (auto* m = selectedModel(); m && !m->lodPath.empty()) {
            m->lodPath.clear();
            m->lod.reset();
            m->farLods.clear();
            refreshProperties();
            markDirty();
        }
    });
    connect(browseLod, &QPushButton::clicked, this, [this] {
        auto* m = selectedModel();
        if (!m)
            return;
        const QString start = QString::fromStdU16String(m->sourcePath.parent_path().u16string());
        const QString path = QFileDialog::getOpenFileName(this, tr("LOD model"), start, tr("glTF (*.glb *.gltf)"));
        if (path.isEmpty())
            return;
        // Loaded on a worker thread, attached to the model that was selected.
        const std::string name = m->name;
        notify(tr("Loading %1...").arg(QFileInfo(path).fileName()));
        QPointer<MainWindow> self(this);
        engine::JobSystem::global().run([self, path, name] {
            auto lod = std::make_shared<engine::Model>();
            std::string error;
            const bool ok = engine::loadModelFile(*lod, std::filesystem::path(path.toStdU16String()), &error);
            QMetaObject::invokeMethod(qApp, [self, path, name, lod, ok, error] {
                if (!self)
                    return;
                if (!ok) {
                    QMessageBox::warning(self, tr("LOD model"), QString::fromStdString(error));
                    return;
                }
                const int object = engine::findObject(self->scene_, name);
                if (object < 0 || !self->scene_.isModel(object))
                    return;
                engine::Model& target = self->scene_.model(object);
                target.lodPath = std::filesystem::path(path.toStdU16String());
                target.lod = lod;
                self->refreshProperties();
                self->markDirty();
            });
        });
    });
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
    scriptForm->addRow(tr("Lua script"), scriptRow_);

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
    characterForm->addRow(tr("Character model"), characterRow_);
    characterForm->addRow(tr("Model scale"), modelScaleSpin_);
    characterForm->addRow(tr("Model yaw"), modelYawSpin_);
    characterForm->addRow(tr("Animation"), animationCombo_);
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

    // Sound source (entity type "sound"): a looping 3D sound at the entity, or an
    // ambient zone that fills the entity's box and fades out around it.
    entitySound_ = new SoundPicker(boxPanel_);
    soundZoneCheck_ = new QCheckBox(tr("Ambient zone (fills the box)"), boxPanel_);
    soundZoneCheck_->setToolTip(tr("Off: a 3D sound at the entity. On: heard everywhere inside the box, fading out around it."));
    soundFadeSpin_ = new QDoubleSpinBox(boxPanel_);
    soundFadeSpin_->setRange(0.0, 500.0);
    soundFadeSpin_->setSingleStep(1.0);
    soundFadeSpin_->setSuffix(tr(" m"));
    soundFadeSpin_->setToolTip(tr("Distance outside the zone over which it fades out"));
    soundVolumeSpin_ = new QDoubleSpinBox(boxPanel_);
    soundVolumeSpin_->setRange(0.0, 4.0);
    soundVolumeSpin_->setSingleStep(0.05);
    for (QDoubleSpinBox* s : {soundFadeSpin_, soundVolumeSpin_})
        s->setKeyboardTracking(false);
    soundForm->addRow(tr("Sound"), entitySound_);
    soundForm->addRow(QString(), soundZoneCheck_);
    soundForm->addRow(tr("Zone fade"), soundFadeSpin_);
    soundForm->addRow(tr("Volume"), soundVolumeSpin_);
    auto setSoundProp = [this](const std::string& key, const std::string& value) {
        auto* e = selectedEntity();
        if (!e || e->type != "sound")
            return;
        auto it = e->properties.find(key);
        if (value.empty() ? it == e->properties.end() : (it != e->properties.end() && it->second == value))
            return;
        if (value.empty())
            e->properties.erase(key);
        else
            e->properties[key] = value;
        markDirty();
    };
    connect(entitySound_, &SoundPicker::pathChanged, this, [this, setSoundProp](const QString& path) {
        // Stored relative to the assets folder so the game finds it next to its exe.
        QString rel = path.isEmpty() ? QString() : QDir(assetRoot()).relativeFilePath(path);
        setSoundProp("sound", rel.toStdString());
    });
    connect(soundZoneCheck_, &QCheckBox::toggled, this, [this, setSoundProp](bool on) {
        setSoundProp("zone", on ? "1" : "");
        soundFadeSpin_->setEnabled(on);
        updateDetailsVisibility();
    });
    connect(soundFadeSpin_, &QDoubleSpinBox::valueChanged, this, [setSoundProp](double v) {
        setSoundProp("fade", QString::number(v, 'g', 4).toStdString());
    });
    connect(soundVolumeSpin_, &QDoubleSpinBox::valueChanged, this, [setSoundProp](double v) {
        setSoundProp("volume", v == 1.0 ? std::string() : QString::number(v, 'g', 4).toStdString());
    });

    // Particle effect (entity type "particle"): a .particle playing at the entity.
    entityEffect_ = new QLineEdit(boxPanel_);
    entityEffect_->setReadOnly(true);
    {
        auto* browse = new QPushButton(tr("..."), boxPanel_);
        browse->setMaximumWidth(32);
        auto* edit = new QPushButton(tr("Edit"), boxPanel_);
        auto* row = new QWidget(boxPanel_);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(entityEffect_, 1);
        rowLayout->addWidget(browse);
        rowLayout->addWidget(edit);
        effectForm_->addRow(tr("Effect"), row);
        connect(browse, &QPushButton::clicked, this, [this] {
            auto* e = selectedEntity();
            if (!e || e->type != "particle")
                return;
            const QString path = QFileDialog::getOpenFileName(this, tr("Particle effect"), assetPath("particles"),
                                                              tr("Particle effects (*.particle)"));
            if (path.isEmpty())
                return;
            e->properties["effect"] = QDir(assetRoot()).relativeFilePath(path).toStdString();
            entityEffect_->setText(QString::fromStdString(e->properties["effect"]));
            markDirty();
        });
        connect(edit, &QPushButton::clicked, this, [this] {
            if (!entityEffect_->text().isEmpty())
                openParticleEditor(QDir(assetRoot()).absoluteFilePath(entityEffect_->text()));
        });
    }

    // Tunables the script declares in `properties`; values are saved per entity.
    scriptParams_ = new QTableWidget(0, 2, boxPanel_);
    scriptParams_->setHorizontalHeaderLabels({tr("Script parameter"), tr("Value")});
    scriptParams_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    scriptParams_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    scriptParams_->verticalHeader()->hide();
    scriptParams_->setMinimumHeight(200);
    auto* editScript = new QPushButton(tr("Edit script"), boxPanel_);
    scriptForm->addRow(scriptParams_);
    scriptForm->addRow(editScript);
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

    // In-game map: own picture, or the level rendered from above when it loads.
    levelForm->addRow(new QLabel(tr("<b>Map</b>"), levelPanel));
    auto* mapImageRow = new QWidget(levelPanel);
    auto* mapImageLayout = new QHBoxLayout(mapImageRow);
    mapImageLayout->setContentsMargins(0, 0, 0, 0);
    mapImageEdit_ = new QLineEdit(mapImageRow);
    mapImageEdit_->setReadOnly(true);
    mapImageEdit_->setPlaceholderText(tr("rendered from above"));
    auto* browseMap = new QPushButton(tr("..."), mapImageRow);
    auto* clearMap = new QPushButton(tr("x"), mapImageRow);
    browseMap->setFixedWidth(32);
    clearMap->setFixedWidth(32);
    mapImageLayout->addWidget(mapImageEdit_, 1);
    mapImageLayout->addWidget(browseMap);
    mapImageLayout->addWidget(clearMap);
    mapBoundsEdit_ = new Vec3Editor(-100000.0, 100000.0, 10.0, levelPanel);
    mapBoundsEdit_->setToolTip(tr("X and Z of the map's top-left (north-west) corner and its size in metres; size 0 = whole level"));
    levelForm->addRow(tr("Map image"), mapImageRow);
    levelForm->addRow(tr("Map X, Z, size"), mapBoundsEdit_);
    auto* mapHint = new QLabel(tr("Points on the map: Add > Map marker (the name is the label)."), levelPanel);
    mapHint->setObjectName("dim");
    mapHint->setWordWrap(true);
    levelForm->addRow(mapHint);
    connect(browseMap, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Map image (north up)"), assetPath("textures"),
                                                          tr("Images (*.png *.jpg *.jpeg)"));
        if (path.isEmpty())
            return;
        scene_.map.image = std::filesystem::path(path.toStdU16String());
        refreshLevelProperties();
        markDirty();
    });
    connect(clearMap, &QPushButton::clicked, this, [this] {
        scene_.map.image.clear();
        refreshLevelProperties();
        markDirty();
    });
    connect(mapBoundsEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        scene_.map.min = {v.x, v.y};
        scene_.map.size = std::max(0.0f, v.z);
        markDirty();
    });

    // Loading screen the game shows while this level loads.
    levelForm->addRow(new QLabel(tr("<b>Loading screen</b>"), levelPanel));
    auto* loadingImageRow = new QWidget(levelPanel);
    auto* loadingImageLayout = new QHBoxLayout(loadingImageRow);
    loadingImageLayout->setContentsMargins(0, 0, 0, 0);
    loadingImageEdit_ = new QLineEdit(loadingImageRow);
    loadingImageEdit_->setReadOnly(true);
    loadingImageEdit_->setPlaceholderText(tr("dark background"));
    auto* browseLoading = new QPushButton(tr("..."), loadingImageRow);
    auto* clearLoading = new QPushButton(tr("x"), loadingImageRow);
    browseLoading->setFixedWidth(32);
    clearLoading->setFixedWidth(32);
    loadingImageLayout->addWidget(loadingImageEdit_, 1);
    loadingImageLayout->addWidget(browseLoading);
    loadingImageLayout->addWidget(clearLoading);
    loadingTitleEdit_ = new QLineEdit(levelPanel);
    loadingTitleEdit_->setPlaceholderText(tr("level file name"));
    loadingHintEdit_ = new QLineEdit(levelPanel);
    loadingHintEdit_->setPlaceholderText(tr("e.g. a tip for the player"));
    levelForm->addRow(tr("Image"), loadingImageRow);
    levelForm->addRow(tr("Title"), loadingTitleEdit_);
    levelForm->addRow(tr("Hint"), loadingHintEdit_);
    connect(browseLoading, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Loading screen image"), assetPath("textures"),
                                                          tr("Images (*.png *.jpg *.jpeg)"));
        if (path.isEmpty())
            return;
        scene_.loading.image = std::filesystem::path(path.toStdU16String());
        refreshLevelProperties();
        markDirty();
    });
    connect(clearLoading, &QPushButton::clicked, this, [this] {
        scene_.loading.image.clear();
        refreshLevelProperties();
        markDirty();
    });
    connect(loadingTitleEdit_, &QLineEdit::editingFinished, this, [this] {
        if (scene_.loading.title != loadingTitleEdit_->text().toStdString()) {
            scene_.loading.title = loadingTitleEdit_->text().toStdString();
            markDirty();
        }
    });
    connect(loadingHintEdit_, &QLineEdit::editingFinished, this, [this] {
        if (scene_.loading.hint != loadingHintEdit_->text().toStdString()) {
            scene_.loading.hint = loadingHintEdit_->text().toStdString();
            markDirty();
        }
    });

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
    // Player sounds: footsteps (per stride), landing after a fall, getting hurt.
    const char* playerSoundLabels[] = {"Footsteps", "Landing", "Hurt"};
    for (std::size_t i = 0; i < playerSounds_.size(); ++i) {
        auto* picker = new SoundPicker(levelPanel);
        levelForm->addRow(tr(playerSoundLabels[i]), picker);
        playerSounds_[i] = picker;
        connect(picker, &SoundPicker::pathChanged, this, [this, i](const QString& path) {
            std::filesystem::path* slot[] = {&scene_.player.footstepSound, &scene_.player.landSound, &scene_.player.hurtSound};
            *slot[i] = std::filesystem::path(path.toStdU16String());
            markDirty();
        });
    }
    // Navigation: where AI characters can walk (Recast). Built on worker threads.
    levelForm->addRow(new QLabel(tr("<b>Navigation</b>"), levelPanel));
    navEnabled_ = new QCheckBox(tr("Build a navigation mesh for AI"), levelPanel);
    levelForm->addRow(QString(), navEnabled_);
    connect(navEnabled_, &QCheckBox::toggled, this, [this](bool on) {
        if (scene_.nav.enabled != on) {
            scene_.nav.enabled = on;
            markDirty();
            setNavOutOfDate();
        }
    });
    struct NavField {
        const char* label;
        float engine::NavSettings::*member;
        double min, max, step;
        const char* suffix;
        const char* tip;
    };
    const NavField navFields[] = {
        {"Agent radius", &engine::NavSettings::agentRadius, 0.05, 5, 0.05, " m", "Characters keep this far from walls"},
        {"Agent height", &engine::NavSettings::agentHeight, 0.3, 5, 0.05, " m", "Headroom a character needs"},
        {"Step height", &engine::NavSettings::agentClimb, 0, 2, 0.05, " m", "Higher ledges block the way"},
        {"Max slope", &engine::NavSettings::maxSlope, 0, 89, 1, " °", "Steeper ground is not walkable"},
        {"Cell size", &engine::NavSettings::cellSize, 0.05, 2, 0.05, " m", "Smaller = more precise around props, slower to build"},
        {"Cell height", &engine::NavSettings::cellHeight, 0.02, 1, 0.02, " m", "Vertical precision"},
        {"Tile size", &engine::NavSettings::tileSize, 4, 256, 4, " m", "Tiles build in parallel"},
        {"Min island", &engine::NavSettings::minRegion, 0, 500, 1, " m²", "Smaller walkable islands (roofs, tables) are dropped"},
    };
    for (const NavField& f : navFields) {
        auto* spin = new QDoubleSpinBox(levelPanel);
        spin->setRange(f.min, f.max);
        spin->setSingleStep(f.step);
        spin->setDecimals(2);
        spin->setSuffix(QString::fromUtf8(f.suffix));
        spin->setKeyboardTracking(false);
        spin->setToolTip(tr(f.tip));
        levelForm->addRow(tr(f.label), spin);
        const auto member = f.member;
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, member](double v) {
            if (scene_.nav.*member != static_cast<float>(v)) {
                scene_.nav.*member = static_cast<float>(v);
                markDirty();
                setNavOutOfDate();
                if (navShowAction_ && navShowAction_->isChecked())
                    navAutoBuild_->start(); // rebuild shortly after the last change
            }
        });
        navSpins_.emplace_back(spin, member);
    }
    {
        auto* navRow = new QHBoxLayout();
        navBuild_ = new QPushButton(tr("Build navmesh"), levelPanel);
        navBuild_->setToolTip(tr("Builds from the level's collision (boxes, models, terrain) and shows it in the viewport"));
        auto* navShow = new QCheckBox(tr("Show"), levelPanel);
        navRow->addWidget(navBuild_);
        navRow->addWidget(navShow);
        navRow->addStretch(1);
        levelForm->addRow(QString(), navRow);
        navStatus_ = new QLabel(tr("Not built"), levelPanel);
        navStatus_->setObjectName("dim");
        navStatus_->setWordWrap(true);
        levelForm->addRow(QString(), navStatus_);
        connect(navBuild_, &QPushButton::clicked, this, [this] {
            if (navShowAction_ && !navShowAction_->isChecked())
                navShowAction_->setChecked(true);
            buildNavMesh();
        });
        // The checkbox mirrors View > Navigation mesh (created later in the menus).
        QTimer::singleShot(0, this, [this, navShow] {
            if (!navShowAction_)
                return;
            navShow->setChecked(navShowAction_->isChecked());
            connect(navShow, &QCheckBox::toggled, navShowAction_, &QAction::setChecked);
            connect(navShowAction_, &QAction::toggled, navShow, [navShow](bool on) {
                QSignalBlocker block(navShow);
                navShow->setChecked(on);
            });
        });
    }
    navAutoBuild_ = new QTimer(this);
    navAutoBuild_->setSingleShot(true);
    navAutoBuild_->setInterval(700);
    connect(navAutoBuild_, &QTimer::timeout, this, &MainWindow::buildNavMesh);
    navPoll_ = new QTimer(this);
    navPoll_->setInterval(100);
    connect(navPoll_, &QTimer::timeout, this, [this] {
        if (navJob_ && !navJob_->done)
            navStatus_->setText(tr("Building... %1 %").arg(int(navJob_->progress * 100.0f)));
    });

    connect(playerStartEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        scene_.playerStart = v;
        markDirty();
    });
    connect(sunEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        if (glm::length(v) > 0.001f) {
            scene_.sunDirection = v;
            environment_->refresh();
            markDirty();
        }
    });
    connect(fromCamera, &QPushButton::clicked, this, [this] {
        // Camera is the eye; the player start is the feet position.
        scene_.playerStart = viewport_->camera().position - glm::vec3(0.0f, scene_.player.eyeHeight, 0.0f);
        refreshLevelProperties();
        markDirty();
    });

    {
        auto* levelSection = new CollapsibleSection(tr("Level"), boxPanel_);
        auto* levelLayout = new QVBoxLayout();
        levelLayout->addWidget(levelPanel);
        levelSection->setBodyLayout(levelLayout);
        detailSections_.push_back({levelSection, levelForm, "Level"});
    }

    // Details: object name and Add button, search, category tabs, then the sections.
    auto* propsHost = new QWidget(this);
    auto* propsLayout = new QVBoxLayout(propsHost);
    propsLayout->setContentsMargins(0, 0, 0, 0);
    propsLayout->setSpacing(0);
    auto* header = new QHBoxLayout();
    header->setContentsMargins(6, 6, 6, 4);
    detailsIcon_ = new QLabel(propsHost);
    nameEdit_->setPlaceholderText(tr("Nothing selected"));
    auto* addComponent = new QToolButton(propsHost);
    addComponent->setText(tr("Add"));
    addComponent->setIcon(editorIcon("add"));
    addComponent->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addComponent->setPopupMode(QToolButton::InstantPopup);
    auto* componentMenu = new QMenu(addComponent);
    componentMenu->addAction(tr("Material..."), this, &MainWindow::browseBoxMaterial);
    componentMenu->addAction(tr("Lua script..."), this, &MainWindow::browseScript);
    componentMenu->addAction(tr("Character model..."), this, &MainWindow::browseCharacter);
    addComponent->setMenu(componentMenu);
    header->addWidget(detailsIcon_);
    header->addWidget(nameEdit_, 1);
    header->addWidget(addComponent);
    propsLayout->addLayout(header);
    detailsSearch_ = new QLineEdit(propsHost);
    detailsSearch_->setPlaceholderText(tr("Search details"));
    detailsSearch_->setClearButtonEnabled(true);
    auto* searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(6, 0, 6, 4);
    searchRow->addWidget(detailsSearch_);
    propsLayout->addLayout(searchRow);
    detailsTabs_ = new QTabBar(propsHost);
    detailsTabs_->setObjectName("categories");
    detailsTabs_->setDrawBase(false);
    detailsTabs_->setExpanding(false);
    detailsTabs_->setUsesScrollButtons(false);
    for (const char* c : {"General", "Transform", "Rendering", "Character", "Script", "Sound", "Level", "All"})
        detailsTabs_->addTab(tr(c));
    detailsTabs_->setCurrentIndex(7);
    auto* tabRow = new QHBoxLayout();
    tabRow->setContentsMargins(6, 0, 6, 6);
    tabRow->addWidget(detailsTabs_);
    tabRow->addStretch(1);
    propsLayout->addLayout(tabRow);
    auto* sectionsHost = new QWidget(propsHost);
    auto* sectionsHostLayout = new QVBoxLayout(sectionsHost);
    sectionsHostLayout->setContentsMargins(0, 0, 0, 0);
    sectionsHostLayout->setSpacing(0);
    sectionsHostLayout->addWidget(boxPanel_);
    sectionsHostLayout->addWidget(detailSections_.back().section);
    sectionsHostLayout->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(sectionsHost);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    propsLayout->addWidget(scroll, 1);
    connect(detailsTabs_, &QTabBar::currentChanged, this, &MainWindow::updateDetailsVisibility);
    connect(detailsSearch_, &QLineEdit::textChanged, this, &MainWindow::updateDetailsVisibility);

    auto* propsDock = new QDockWidget(tr("Details"), this);
    propsDock->setObjectName("properties");
    propsDock->setWidget(propsHost);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);

    // Asset browser: thumbnails of everything under assets/.
    assets_ = new AssetBrowser(this);
    connect(assets_, &AssetBrowser::assetActivated, this, [this](const QString& path, AssetType type) {
        openAsset(path, static_cast<int>(type));
    });
    connect(assets_, &AssetBrowser::saveAllRequested, this, &MainWindow::saveAll);
    connect(assets_, &AssetBrowser::newAssetRequested, this, [this](const QString& kind, const QString& folder) {
        if (kind == "material")
            openMaterialEditor();
        else if (kind == "script")
            newScript();
        else if (kind == "level")
            newLevel();
        else if (kind == "sound")
            newSound(folder);
        else if (kind == "particle")
            newParticleEffect(folder);
    });
    assetsDock_ = new QDockWidget(tr("Content Browser"), this);
    auto* assetsDock = assetsDock_;
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

    // Environment: sun, sky, fog and image settings of the level.
    environment_ = new EnvironmentPanel(&scene_, this);
    connect(environment_, &EnvironmentPanel::edited, this, [this] {
        sunEdit_->setValue(scene_.sunDirection);
        markDirty();
    });
    auto* environmentDock = new QDockWidget(tr("Environment"), this);
    environmentDock->setObjectName("environment");
    auto* environmentScroll = new QScrollArea(environmentDock);
    environmentScroll->setWidget(environment_);
    environmentScroll->setWidgetResizable(true);
    environmentDock->setWidget(environmentScroll);
    addDockWidget(Qt::RightDockWidgetArea, environmentDock);
    // Scythe-like layout: outliner top right, details (properties, relations, weapons) below it,
    // content browser at the bottom, tools on the left.
    splitDockWidget(outlinerDock, propsDock, Qt::Vertical);
    tabifyDockWidget(propsDock, depsDock);
    tabifyDockWidget(propsDock, weaponsDock);
    tabifyDockWidget(propsDock, environmentDock);
    propsDock->raise();

    // Output log: editor messages, Qt warnings and the running game's output.
    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto* logHost = new QWidget(this);
    auto* logLayout = new QVBoxLayout(logHost);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(0);
    auto* logBar = new QHBoxLayout();
    logBar->setContentsMargins(4, 4, 4, 4);
    auto* logFilter = new QLineEdit(logHost);
    logFilter->setPlaceholderText(tr("Search log"));
    logFilter->setClearButtonEnabled(true);
    auto* clearLog = new QPushButton(tr("Clear"), logHost);
    logBar->addWidget(logFilter, 1);
    logBar->addWidget(clearLog);
    logLayout->addLayout(logBar);
    logLayout->addWidget(log_, 1);
    connect(clearLog, &QPushButton::clicked, log_, &QPlainTextEdit::clear);
    connect(logFilter, &QLineEdit::returnPressed, this, [this, logFilter] {
        if (!log_->find(logFilter->text())) {
            log_->moveCursor(QTextCursor::Start);
            log_->find(logFilter->text());
        }
    });
    logDock_ = new QDockWidget(tr("Output Log"), this);
    logDock_->setObjectName("outputLog");
    logDock_->setWidget(logHost);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    tabifyDockWidget(assetsDock, logDock_);
    // Engine log (audio, physics, scripts, loading...) shows here too, from any thread.
    for (const engine::LogLine& line : engine::recentLog(200))
        appendLog(QString::fromStdString(line.category + ": " + line.text), line.level >= engine::LogLevel::Error ? 2 : line.level == engine::LogLevel::Warning ? 1 : 0);
    {
        QPointer<MainWindow> self(this);
        logListener_ = engine::addLogListener([self](const engine::LogLine& line) {
            const QString text = QString::fromStdString(line.category.empty() ? line.text : line.category + ": " + line.text);
            const int level = line.level >= engine::LogLevel::Error ? 2 : line.level == engine::LogLevel::Warning ? 1 : 0;
            QMetaObject::invokeMethod(qApp, [self, text, level] {
                if (self)
                    self->appendLog(text, level);
            });
        });
    }

    // Memory: GPU (renderer) and CPU (level assets, whole process), refreshed while visible.
    auto* memoryLabel = new QLabel(this);
    memoryLabel->setTextFormat(Qt::RichText);
    memoryLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    memoryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    memoryLabel->setContentsMargins(8, 6, 8, 6);
    auto* memoryScroll = new QScrollArea(this);
    memoryScroll->setWidget(memoryLabel);
    memoryScroll->setWidgetResizable(true);
    auto* memoryDock = new QDockWidget(tr("Memory"), this);
    memoryDock->setObjectName("memory");
    memoryDock->setWidget(memoryScroll);
    addDockWidget(Qt::BottomDockWidgetArea, memoryDock);
    tabifyDockWidget(logDock_, memoryDock);
    auto* memoryTimer = new QTimer(this);
    connect(memoryTimer, &QTimer::timeout, this, [this, memoryLabel, memoryDock] {
        if (!memoryDock->isVisible() || memoryDock->visibleRegion().isEmpty())
            return;
        const auto mb = [](std::size_t bytes) { return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + " MB"; };
        const engine::Renderer::MemoryStats g = viewport_->memoryStats();
        const engine::SceneMemory c = engine::sceneMemory(scene_);
        const std::size_t process = engine::processMemoryBytes();
        const QString row = "<tr><td>%1</td><td align='right'>&nbsp;&nbsp;<b>%2</b></td><td>&nbsp;&nbsp;%3</td></tr>";
        QString html = "<table cellspacing='2'>";
        html += "<tr><td colspan='3'><b>GPU (3D view)</b></td></tr>";
        html += row.arg(tr("Textures"), mb(g.textureBytes),
                        tr("%1 textures, %2 saved by compression and streaming, %3 small (far away), %4 loading")
                            .arg(g.textures)
                            .arg(mb(g.savedBytes))
                            .arg(g.streamedLow)
                            .arg(g.streaming));
        html += row.arg(tr("Meshes"), mb(g.meshBytes), tr("%1 vertex/index buffers").arg(g.meshes));
        html += row.arg(tr("Render targets"), mb(g.targetBytes), tr("HDR, depth, SSAO, bloom, shadow maps"));
        html += row.arg(tr("Total"), mb(g.textureBytes + g.meshBytes + g.targetBytes), QString());
        html += "<tr><td colspan='3'><br><b>CPU (level assets)</b></td></tr>";
        html += row.arg(tr("Meshes"), mb(c.meshBytes),
                        tr("%1 parts; %2 saved by sharing between copies of a model").arg(c.meshParts).arg(mb(c.sharedMeshBytes)));
        html += row.arg(tr("Texture pixels"), mb(c.textureBytes),
                        tr("%1 textures (pixels are dropped once on the GPU; the cook cache has them)").arg(c.textures));
        if (process)
            html += row.arg(tr("Editor process"), mb(process), tr("resident memory (with a software GPU driver this includes the GPU data)"));
        html += "</table>";
        html += tr("<p>Texture cook cache: %1</p>").arg(QDir::toNativeSeparators(QString::fromStdU16String(engine::textureCookConfig().cacheDir.u16string())));
        memoryLabel->setText(html);
    });
    memoryTimer->start(1000);

    // Frame: CPU and GPU time per render pass (timer queries), draw calls and culling,
    // with the renderer optimizations that can be switched off to compare.
    auto* framePanel = new QWidget(this);
    auto* frameLayout = new QVBoxLayout(framePanel);
    frameLayout->setContentsMargins(8, 6, 8, 6);
    auto* frameOptions = new QHBoxLayout;
    auto* instancingCheck = new QCheckBox(tr("GPU instancing"), framePanel);
    instancingCheck->setToolTip(tr("Copies of one model in one draw call per mesh part"));
    auto* shadowCacheCheck = new QCheckBox(tr("Shadow cascade cache"), framePanel);
    shadowCacheCheck->setToolTip(tr("Far shadow cascades re-rendered every 2nd / 4th frame, without small casters"));
    instancingCheck->setChecked(viewport_->quality().instancing);
    shadowCacheCheck->setChecked(viewport_->quality().shadowCache);
    frameOptions->addWidget(instancingCheck);
    frameOptions->addWidget(shadowCacheCheck);
    auto* occlusionCheck = new QCheckBox(tr("Occlusion culling"), framePanel);
    occlusionCheck->setToolTip(tr("Skip objects hidden behind nearer ones (depth of a recent frame, read back without stalls)"));
    occlusionCheck->setChecked(viewport_->quality().occlusionCulling);
    frameOptions->addWidget(occlusionCheck);
    frameOptions->addStretch();
    frameLayout->addLayout(frameOptions);
    auto* frameLabel = new QLabel(framePanel);
    frameLabel->setTextFormat(Qt::RichText);
    frameLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    frameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    frameLayout->addWidget(frameLabel, 1);
    auto applyFrameOption = [this, instancingCheck, shadowCacheCheck, occlusionCheck] {
        engine::RenderQuality q = viewport_->quality();
        q.occlusionCulling = occlusionCheck->isChecked();
        q.instancing = instancingCheck->isChecked();
        q.shadowCache = shadowCacheCheck->isChecked();
        viewport_->setQuality(q);
    };
    connect(instancingCheck, &QCheckBox::toggled, this, applyFrameOption);
    connect(shadowCacheCheck, &QCheckBox::toggled, this, applyFrameOption);
    connect(occlusionCheck, &QCheckBox::toggled, this, applyFrameOption);
    auto* frameScroll = new QScrollArea(this);
    frameScroll->setWidget(framePanel);
    frameScroll->setWidgetResizable(true);
    auto* frameDock = new QDockWidget(tr("Frame"), this);
    frameDock->setObjectName("frame");
    frameDock->setWidget(frameScroll);
    addDockWidget(Qt::BottomDockWidgetArea, frameDock);
    tabifyDockWidget(logDock_, frameDock);
    auto* frameTimer = new QTimer(this);
    connect(frameTimer, &QTimer::timeout, this, [this, frameLabel, frameDock, instancingCheck, shadowCacheCheck] {
        if (!frameDock->isVisible() || frameDock->visibleRegion().isEmpty())
            return;
        instancingCheck->blockSignals(true); // quality presets change these too
        instancingCheck->setChecked(viewport_->quality().instancing);
        instancingCheck->blockSignals(false);
        shadowCacheCheck->blockSignals(true);
        shadowCacheCheck->setChecked(viewport_->quality().shadowCache);
        shadowCacheCheck->blockSignals(false);
        const engine::Renderer::FrameStats& f = viewport_->frameStats();
        const auto ms = [](double v) { return QString::number(v, 'f', 2); };
        QString html = "<table cellspacing='2'><tr><td><b>" + tr("Pass") + "</b></td><td align='right'>&nbsp;&nbsp;<b>CPU ms</b></td>"
                       "<td align='right'>&nbsp;&nbsp;<b>GPU ms</b></td></tr>";
        for (int p = 0; p < engine::Renderer::PassCount; ++p)
            html += QString("<tr><td>%1</td><td align='right'>%2</td><td align='right'>%3</td></tr>")
                        .arg(QString::fromUtf8(engine::Renderer::passName(p)), ms(f.cpuMs[p]), f.gpuValid ? ms(f.gpuMs[p]) : QStringLiteral("-"));
        html += QString("<tr><td><b>%1</b></td><td align='right'><b>%2</b></td><td align='right'><b>%3</b></td></tr></table>")
                    .arg(tr("Total"), ms(f.cpuTotalMs), f.gpuValid ? ms(f.gpuTotalMs) : QStringLiteral("-"));
        html += tr("<p>%1 draw calls, %2 triangles, %3 objects instanced, %4 culled, %5 hidden by occlusion, %6 GPU uploads</p>")
                    .arg(f.drawCalls)
                    .arg(QLocale().toString(f.triangles))
                    .arg(f.instanced)
                    .arg(f.culled)
                    .arg(f.occluded)
                    .arg(f.uploads);
        if (!f.gpuValid)
            html += tr("<p>GPU timer queries are not available on this driver.</p>");
        frameLabel->setText(html);
    });
    frameTimer->start(500);
    assetsDock->raise();

    buildModePanel();

    resizeDocks({outlinerDock, propsDock}, {300, 560}, Qt::Vertical);
    resizeDocks({outlinerDock}, {400}, Qt::Horizontal);
    resizeDocks({assetsDock}, {270}, Qt::Vertical);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
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
    outlinerPanel_->setCounts(scene_.objectCount(), selected_ >= 0 ? 1 : 0);
    dependencies_->showObject(selected_);
    viewport_->setSelected(selected_);
    refreshProperties();
}

void MainWindow::refreshOutliner()
{
    outliner_->rebuild();
    outlinerPanel_->setCounts(scene_.objectCount(), selected_ >= 0 ? 1 : 0);
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
    rotationEdit_->setEnabled(m != nullptr || b != nullptr);
    scaleSpin_->setEnabled(m != nullptr);
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
        rotationEdit_->setValue(b->rotation);
        colorButton_->setStyleSheet(colorStyle(b->color));
    } else if (m) {
        kindLabel_->setText(tr("glTF model: %1").arg(QString::fromStdU16String(m->sourcePath.filename().u16string())));
        nameEdit_->setText(QString::fromStdString(m->name));
        positionEdit_->setValue(m->position);
        rotationEdit_->setValue(m->rotation);
        scaleSpin_->blockSignals(true);
        scaleSpin_->setValue(m->scale);
        scaleSpin_->blockSignals(false);
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
    lodRow_->setEnabled(m != nullptr);
    lodDistanceSpin_->setEnabled(m != nullptr && !m->lodPath.empty());
    lodEdit_->setText(m && !m->lodPath.empty() ? QString::fromStdU16String(m->lodPath.filename().u16string()) : QString());
    {
        QStringList levels;
        if (m)
            for (const engine::Model::FarLod& far : m->farLods)
                levels << QString("%1 @ %2 m").arg(QString::fromStdU16String(far.path.filename().u16string())).arg(static_cast<double>(far.distance));
        farLodEdit_->setText(levels.join(", "));
        farLodEdit_->setEnabled(m != nullptr);
    }
    lodDistanceSpin_->blockSignals(true);
    lodDistanceSpin_->setValue(m ? m->lodDistance : 80.0);
    lodDistanceSpin_->blockSignals(false);
    cullDistanceSpin_->setEnabled(m != nullptr);
    collisionCheck_->setEnabled(m != nullptr);
    cullDistanceSpin_->blockSignals(true);
    cullDistanceSpin_->setValue(m ? m->cullDistance : 0.0);
    cullDistanceSpin_->blockSignals(false);
    collisionCheck_->blockSignals(true);
    collisionCheck_->setChecked(m ? m->collision : true);
    collisionCheck_->blockSignals(false);
    shadowsCheck_->setEnabled(m != nullptr);
    shadowsCheck_->blockSignals(true);
    shadowsCheck_->setChecked(m ? m->castShadows : true);
    shadowsCheck_->blockSignals(false);
    nameEdit_->setEnabled(b || m || e);
    {
        const bool sound = e && e->type == "sound";
        auto prop = [&](const char* key, const char* fallback) {
            if (!sound)
                return QString::fromLatin1(fallback);
            const auto it = e->properties.find(key);
            return it == e->properties.end() ? QString::fromLatin1(fallback) : QString::fromStdString(it->second);
        };
        const QString soundRel = prop("sound", "");
        entitySound_->setPath(soundRel.isEmpty() ? QString() : QDir(assetRoot()).absoluteFilePath(soundRel));
        for (QWidget* w : std::initializer_list<QWidget*>{entitySound_, soundZoneCheck_, soundVolumeSpin_})
            w->setEnabled(sound);
        const QSignalBlocker b1(soundZoneCheck_), b2(soundFadeSpin_), b3(soundVolumeSpin_);
        soundZoneCheck_->setChecked(prop("zone", "0") == "1");
        soundFadeSpin_->setEnabled(sound && soundZoneCheck_->isChecked());
        soundFadeSpin_->setValue(prop("fade", "10").toDouble());
        soundVolumeSpin_->setValue(prop("volume", "1").toDouble());
    }
    {
        const bool particle = e && e->type == "particle";
        QString effect;
        if (particle)
            if (const auto it = e->properties.find("effect"); it != e->properties.end())
                effect = QString::fromStdString(it->second);
        entityEffect_->setText(effect);
        entityEffect_->setEnabled(particle);
    }
    detailsIcon_->setPixmap(editorIcon(b ? "cube" : m ? "model" : e ? "person" : "select").pixmap(18, 18));
    updateDetailsVisibility();
    refreshTransformBox();
    refreshTexturePanel();
}

void MainWindow::refreshScriptParams()
{
    const engine::Entity* e = selectedEntity();
    scriptProps_.clear();
    std::string error;
    if (e && !e->scriptPath.empty() && !engine::readScriptProperties(e->scriptPath, scriptProps_, &error))
        notify(tr("Script error: %1").arg(QString::fromStdString(error)), 8000);

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
    mapImageEdit_->setText(QString::fromStdU16String(scene_.map.image.filename().u16string()));
    mapBoundsEdit_->setValue(glm::vec3(scene_.map.min.x, scene_.map.min.y, scene_.map.size));
    loadingImageEdit_->setText(QString::fromStdU16String(scene_.loading.image.filename().u16string()));
    loadingImageEdit_->setToolTip(QString::fromStdU16String(scene_.loading.image.u16string()));
    loadingTitleEdit_->setText(QString::fromStdString(scene_.loading.title));
    loadingHintEdit_->setText(QString::fromStdString(scene_.loading.hint));
    for (auto& [spin, member] : playerSpins_) {
        spin->blockSignals(true);
        spin->setValue(scene_.player.*member);
        spin->blockSignals(false);
    }
    const std::filesystem::path* sounds[] = {&scene_.player.footstepSound, &scene_.player.landSound, &scene_.player.hurtSound};
    for (std::size_t i = 0; i < playerSounds_.size(); ++i)
        playerSounds_[i]->setPath(QString::fromStdU16String(sounds[i]->u16string()));
    {
        QSignalBlocker block(navEnabled_);
        navEnabled_->setChecked(scene_.nav.enabled);
    }
    for (auto& [spin, member] : navSpins_) {
        QSignalBlocker block(spin);
        spin->setValue(scene_.nav.*member);
    }
    // Another level or an undo step: the shown navmesh no longer matches.
    setNavOutOfDate();
    if (navShowAction_ && navShowAction_->isChecked())
        navAutoBuild_->start();
    refreshTerrainPanel();
}

void MainWindow::setNavOutOfDate()
{
    if (navStale_)
        return;
    navStale_ = true;
    if (navStatus_ && navMesh_ && !(navJob_ && !navJob_->done))
        navStatus_->setText(navStatus_->text() + tr(" (out of date)"));
}

void MainWindow::buildNavMesh()
{
    if (navJob_)
        navJob_->cancel = true; // the newest build wins
    if (!scene_.nav.enabled) {
        navMesh_.reset();
        navDebug_.triangles.clear();
        navStatus_->setText(tr("Off for this level"));
        viewport_->update();
        return;
    }
    auto job = std::make_shared<NavJob>();
    navJob_ = job;
    navStale_ = false;
    navStatus_->setText(tr("Building..."));
    navPoll_->start();
    // The build reads a copy: the level can be edited meanwhile (models and terrain are shared, read-only).
    auto scene = std::make_shared<engine::Scene>(scene_);
    QPointer<MainWindow> self(this);
    engine::JobSystem::global().run([self, job, scene] {
        auto mesh = std::make_shared<engine::NavMesh>();
        std::string error;
        const bool ok = mesh->build(*scene, scene->nav, &error, &job->progress, &job->cancel);
        std::vector<glm::vec3> triangles = ok ? mesh->debugTriangles() : std::vector<glm::vec3>{};
        job->done = true;
        QMetaObject::invokeMethod(qApp, [self, job, mesh, ok, error, triangles = std::move(triangles)]() mutable {
            if (!self || self->navJob_ != job || job->cancel)
                return;
            self->navPoll_->stop();
            if (!ok) {
                self->navStatus_->setText(tr("Build failed: %1").arg(QString::fromStdString(error)));
                return;
            }
            self->navMesh_ = mesh;
            self->navDebug_.triangles = std::move(triangles);
            self->navStatus_->setText(tr("%1 tiles, %2 polygons, built in %3 s")
                                          .arg(mesh->tileCount())
                                          .arg(mesh->polygonCount())
                                          .arg(mesh->buildSeconds(), 0, 'f', 1) +
                                      (self->navStale_ ? tr(" (out of date)") : QString()));
            self->viewport_->update();
        });
    });
}

void MainWindow::markDirty()
{
    setNavOutOfDate();
    if (!restoring_ && history_) {
        auto after = std::make_shared<SceneSnapshot>(meshStore_.capture(scene_, selected_));
        if (lastState_)
            history_->push(new SceneEditCommand(tr("Edit"), lastState_, after,
                                                [this](const SceneSnapshot& s) { applySnapshot(s); }));
        lastState_ = after;
    }
    dirty_ = true;
    viewport_->update();
    updateTitle();
}

void MainWindow::resetHistory()
{
    if (!history_)
        return;
    history_->clear();
    lastState_ = std::make_shared<SceneSnapshot>(meshStore_.capture(scene_, selected_));
}

void MainWindow::applySnapshot(const SceneSnapshot& snapshot)
{
    restoring_ = true;
    scene_ = meshStore_.restore(snapshot);
    lastState_ = std::make_shared<SceneSnapshot>(snapshot);
    weapons_->refresh();
    environment_->refresh();
    selected_ = -1;
    refreshOutliner();
    refreshLevelProperties();
    select(snapshot.selected < scene_.objectCount() ? snapshot.selected : -1);
    dirty_ = true;
    viewport_->update();
    updateTitle();
    restoring_ = false;
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
    // Parsing the glTF and decoding its textures happens on a worker thread;
    // the model is added to the level when it is ready.
    notify(tr("Loading %1...").arg(QFileInfo(path).fileName()));
    QPointer<MainWindow> self(this);
    engine::JobSystem::global().run([self, path, point] {
        auto model = std::make_shared<engine::Model>();
        std::string error;
        const std::filesystem::path file(path.toStdU16String());
        const bool ok = engine::loadModelFile(*model, file, &error);
        // A "foo_lod1.glb" next to the file becomes the model's LOD automatically.
        if (ok)
            if (const auto lodFile = engine::findLodFile(file); !lodFile.empty()) {
                auto lod = std::make_shared<engine::Model>();
                if (engine::loadModelFile(*lod, lodFile)) {
                    model->lodPath = lodFile;
                    model->lod = lod;
                }
            }
        QMetaObject::invokeMethod(qApp, [self, path, point, model, ok, error] {
            if (!self)
                return;
            if (!ok) {
                QMessageBox::warning(self, tr("Place model"), QString::fromStdString(error));
                return;
            }
            self->insertModel(path, std::move(*model), point);
        });
    });
    return true;
}

void MainWindow::insertModel(const QString& path, engine::Model model, glm::vec3 point)
{
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
    notify(tr("Placed %1").arg(QFileInfo(path).fileName()), 4000);
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
    notify(tr("Material %1 assigned to %2").arg(QFileInfo(path).completeBaseName(),
                                                                  QString::fromStdString(engine::objectName(scene_, object))), 5000);
    return true;
}

void MainWindow::openAssetFile(const QString& path)
{
    if (QFileInfo(path).fileName() == "surfaces.cfg") {
        openSurfacesEditor();
        return;
    }
    openAsset(path, static_cast<int>(assetTypeOf(path)));
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
    case AssetType::Sound: openSoundEditor(path); break;
    case AssetType::Audio: previewSound(path); break;
    case AssetType::Particle: openParticleEditor(path); break;
    case AssetType::Level:
        if (confirmDiscard())
            openFile(path);
        break;
    default:
        if (QFileInfo(path).fileName() == "surfaces.cfg")
            openSurfacesEditor();
        else
            notify(path, 5000);
        break;
    }
}

void MainWindow::addEntity(const QString& type)
{
    const auto& cam = viewport_->camera();
    const glm::vec3 p = cam.position + cam.flatForward() * 6.0f;
    engine::Entity e;
    e.type = type.toStdString();
    e.name = engine::uniqueNodeName(scene_, (type == "enemy" ? "Enemy " : type == "sound" ? "Sound " : type == "particle" ? "Effect " : "Entity ") +
                                                std::to_string(scene_.entities.size() + 1));
    if (type == "sound") {
        e.size = {1.0f, 1.0f, 1.0f};
        e.color = {0.25f, 0.55f, 0.95f};
        e.properties["sound"] = "sounds/ambient/geiger.sound";
    }
    if (type == "particle") {
        e.size = {0.4f, 0.4f, 0.4f};
        e.color = {0.95f, 0.55f, 0.2f};
        e.properties["effect"] = "particles/fire.particle";
    }
    e.position = {std::round(p.x * 2.0f) * 0.5f, 0.0f, std::round(p.z * 2.0f) * 0.5f};
    if (type == "enemy") {
        e.color = {0.55f, 0.5f, 0.5f};
        // Default AI script shipped next to the editor (copied by the game build).
        const QString script = assetPath("scripts/enemy.lua");
        if (QFileInfo::exists(script))
            e.scriptPath = std::filesystem::path(script.toStdU16String());
        // A random human from the bundled characters (stalker, soldier, bandit, ...).
        QStringList humans = QDir(assetPath("models/characters/humans")).entryList({"*.glb"}, QDir::Files);
        const QString human = humans.isEmpty()
                                  ? assetPath("models/characters/library/ual_mannequin.glb")
                                  : assetPath("models/characters/humans/" + humans.at(QRandomGenerator::global()->bounded(humans.size())));
        if (QFileInfo::exists(human)) {
            e.modelPath = std::filesystem::path(human.toStdU16String());
            e.model = engine::loadCharacterModel(e.modelPath);
            if (e.model)
                e.animation = "Idle_Loop";
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
    environment_->refresh();
    refreshOutliner();
    refreshLevelProperties();
    select(-1);
    resetHistory();
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
    // Models, materials and textures of the level load on worker threads (models
    // in parallel); the progress shows over the 3D view, in the status bar and
    // on the start-up splash screen.
    notify(tr("Opening %1...").arg(QFileInfo(path).fileName()));
    setEnabled(false);
    auto progress = std::make_shared<engine::LoadProgress>();
    showLoading(QFileInfo(path).fileName());
    auto* poll = new QTimer(this);
    connect(poll, &QTimer::timeout, this, [this, progress] {
        updateLoading(progress->fraction(), QString::fromStdString(progress->step()));
    });
    poll->start(50);
    QPointer<MainWindow> self(this);
    engine::JobSystem::global().run([self, path, progress, poll] {
        auto loaded = std::make_shared<engine::Scene>();
        std::string error;
        const bool ok = engine::loadScene(*loaded, std::filesystem::path(path.toStdU16String()), &error, progress.get());
        QMetaObject::invokeMethod(qApp, [self, path, loaded, ok, error, poll] {
            poll->deleteLater();
            if (!self)
                return;
            self->setEnabled(true);
            self->hideLoading();
            if (!ok) {
                QMessageBox::warning(self, tr("Open level"), QString::fromStdString(error));
                emit self->loadingFinished();
                return;
            }
            self->applyLoadedLevel(path, std::move(*loaded));
            emit self->loadingFinished();
        });
    });
    return true;
}

void MainWindow::showLoading(const QString& name)
{
    if (!loadingPanel_) {
        loadingPanel_ = new QFrame(viewportPanel_);
        loadingPanel_->setObjectName("floatingPanel");
        auto* layout = new QVBoxLayout(loadingPanel_);
        layout->setContentsMargins(18, 14, 18, 14);
        loadingTitle_ = new QLabel(loadingPanel_);
        loadingBar_ = new QProgressBar(loadingPanel_);
        loadingBar_->setRange(0, 100);
        loadingBar_->setTextVisible(true);
        loadingStep_ = new QLabel(loadingPanel_);
        loadingStep_->setObjectName("dim");
        layout->addWidget(loadingTitle_);
        layout->addWidget(loadingBar_);
        layout->addWidget(loadingStep_);
        loadingPanel_->setFixedWidth(420);
        statusBar_ = new QProgressBar(statusBar());
        statusBar_->setRange(0, 100);
        statusBar_->setMaximumWidth(180);
        statusBar_->setMaximumHeight(14);
        statusBar_->setTextVisible(false);
        statusBar()->addPermanentWidget(statusBar_);
    }
    loadingTitle_->setText(tr("<b>Opening %1</b>").arg(name.toHtmlEscaped()));
    loadingBar_->setValue(0);
    loadingStep_->clear();
    loadingPanel_->adjustSize();
    loadingPanel_->move((viewportPanel_->width() - loadingPanel_->width()) / 2, (viewportPanel_->height() - loadingPanel_->height()) / 2);
    loadingPanel_->show();
    loadingPanel_->raise();
    statusBar_->setValue(0);
    statusBar_->show();
}

void MainWindow::updateLoading(float fraction, const QString& step)
{
    const int percent = static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * 100.0f + 0.5f);
    if (loadingBar_) {
        loadingBar_->setValue(percent);
        statusBar_->setValue(percent);
        loadingStep_->setText(step.isEmpty() ? QString() : tr("Loading %1").arg(step));
    }
    emit loadingProgress(percent, step);
}

void MainWindow::hideLoading()
{
    if (loadingPanel_) {
        loadingPanel_->hide();
        statusBar_->hide();
    }
    emit loadingProgress(100, QString());
}

void MainWindow::applyLoadedLevel(const QString& path, engine::Scene loaded)
{
    scene_ = std::move(loaded);
    weapons_->refresh();
    environment_->refresh();
    currentFile_ = path;
    dirty_ = false;
    selected_ = -1;
    refreshOutliner();
    refreshLevelProperties();
    select(-1);
    resetHistory();
    updateTitle();
    notify(tr("Opened %1").arg(QFileInfo(path).fileName()), 4000);
    emit levelOpened();
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
    notify(tr("Saved %1").arg(path), 4000);
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
    QStringList args{snapshot};
    if (customShaders_)
        args << "--shaders" << shaderRoot(); // the game runs the shaders edited here
    if (!QFileInfo::exists(exe)) {
        QMessageBox::warning(this, tr("Play"), tr("Could not start %1").arg(exe));
        return;
    }
    if (game_ && game_->state() != QProcess::NotRunning)
        stopGame();
    // Runs as a child process: its output goes to the Output Log, Pause and Stop control it.
    game_ = new QProcess(this);
    game_->setProcessChannelMode(QProcess::MergedChannels);
    connect(game_, &QProcess::readyReadStandardOutput, this, [this, process = game_] {
        const QString text = QString::fromLocal8Bit(process->readAllStandardOutput());
        for (const QString& line : text.split('\n', Qt::SkipEmptyParts))
            appendLog(QStringLiteral("[game] ") + line.trimmed(), 0);
    });
    connect(game_, &QProcess::finished, this, [this, process = game_](int code) {
        appendLog(tr("[game] exited with code %1").arg(code), code == 0 ? 0 : 1);
        process->deleteLater();
        if (game_ == process)
            game_ = nullptr;
        gamePaused_ = false;
        updatePlayButtons();
    });
    connect(game_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            appendLog(tr("[game] could not start"), 2);
    });
    appendLog(tr("Play: %1 %2").arg(QDir::toNativeSeparators(exe), args.join(' ')), 0);
    game_->start(exe, args);
    gamePaused_ = false;
    updatePlayButtons();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (confirmCloseScripts() && confirmDiscard()) {
        shutdownGame();
        e->accept();
    } else {
        e->ignore();
    }
}

MainWindow::~MainWindow()
{
    engine::removeLogListener(logListener_);
    shutdownGame();
}

void MainWindow::shutdownGame()
{
    // ~QProcess kills a running child and emits finished(); its slots would then
    // write to the Output Log of a window that is being destroyed (SIGSEGV).
    QProcess* process = game_;
    game_ = nullptr;
    if (!process)
        return;
    process->disconnect();
    if (process->state() != QProcess::NotRunning) {
        if (gamePaused_)
            ::kill(static_cast<pid_t>(process->processId()), SIGCONT);
        process->terminate();
        if (!process->waitForFinished(1500)) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
    delete process;
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
        notify(tr("Saved %1").arg(saved), 4000);
    });
    se->editor()->setFocus();
    return se;
}

void MainWindow::setRenderQuality(const engine::RenderQuality& quality)
{
    viewport_->setQuality(quality);
}

void MainWindow::openShaderEditor()
{
    if (shaderEditor_) {
        tabs_->setCurrentWidget(shaderEditor_);
        return;
    }
    shaderEditor_ = new ShaderEditor(shaderRoot(), tabs_);
    const int index = tabs_->addTab(shaderEditor_, tr("Shaders"));
    tabs_->setTabToolTip(index, shaderRoot());
    tabs_->setCurrentIndex(index);
    connect(shaderEditor_, &ShaderEditor::applyRequested, this, &MainWindow::applyShaders);
    connect(shaderEditor_, &ShaderEditor::modificationChanged, this, [this](bool modified) {
        if (const int i = tabs_->indexOf(shaderEditor_); i >= 0)
            tabs_->setTabText(i, modified ? tr("Shaders *") : tr("Shaders"));
    });
}

void MainWindow::applyShaders()
{
    // Recompile from disk; the viewport keeps the old shaders if this fails.
    QString error;
    const bool ok = viewport_->reloadShaders(shaderRoot(), &error);
    customShaders_ = customShaders_ || ok;
    if (shaderEditor_)
        shaderEditor_->showResult(ok, error);
    if (ok)
        notify(tr("Shaders reloaded from %1").arg(shaderRoot()), 4000);
    else if (!shaderEditor_)
        QMessageBox::warning(this, tr("Shaders"), error);
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
    if (auto* sh = qobject_cast<ShaderEditor*>(w)) {
        if (sh->isModified()) {
            tabs_->setCurrentIndex(index);
            const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save and apply the changed shaders?"),
                                                      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel)
                return false;
            QString error;
            if (answer == QMessageBox::Save) {
                if (!sh->saveAll(&error)) {
                    QMessageBox::warning(this, tr("Save"), error);
                    return false;
                }
                applyShaders();
            }
        }
        tabs_->removeTab(index);
        shaderEditor_ = nullptr;
        w->deleteLater();
        return true;
    }
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
        else if (auto* sh = qobject_cast<ShaderEditor*>(w))
            modified = sh->isModified();
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

void MainWindow::newSound(const QString& folder)
{
    const QString path = QFileDialog::getSaveFileName(this, tr("New sound"), QDir(folder).filePath("new.sound"), tr("Sounds (*.sound)"));
    if (path.isEmpty())
        return;
    // Start from the audio files picked next (a sound without variations stays silent).
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Variations of the new sound"), QFileInfo(path).absolutePath(),
                                                            tr("Audio files (*.wav *.ogg *.mp3 *.flac)"));
    engine::SoundDef def;
    for (const QString& f : files)
        def.files.push_back(std::filesystem::path(QFileInfo(f).absoluteFilePath().toStdU16String()));
    std::string error;
    if (!engine::saveSoundFile(def, std::filesystem::path(path.toStdU16String()), &error)) {
        QMessageBox::warning(this, tr("Sound"), QString::fromStdString(error));
        return;
    }
    assets_->rescan();
    openSoundEditor(path);
}

void MainWindow::openParticleEditor(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (int i = 1; i < tabs_->count(); ++i)
        if (auto* pe = qobject_cast<ParticleEditor*>(tabs_->widget(i)); pe && pe->path() == absolute) {
            tabs_->setCurrentIndex(i);
            return;
        }
    auto* pe = new ParticleEditor(tabs_);
    QString error;
    if (!pe->load(absolute, &error)) {
        delete pe;
        QMessageBox::warning(this, tr("Particles"), error);
        return;
    }
    const int index = tabs_->addTab(pe, pe->title());
    tabs_->setTabToolTip(index, absolute);
    tabs_->setCurrentIndex(index);
    connect(pe, &ParticleEditor::modificationChanged, this, [this, pe] {
        if (const int i = tabs_->indexOf(pe); i >= 0)
            tabs_->setTabText(i, pe->title());
    });
}

void MainWindow::newParticleEffect(const QString& folder)
{
    const QString start = folder.isEmpty() ? assetPath("particles") : folder;
    QString path = QFileDialog::getSaveFileName(this, tr("New particle effect"), QDir(start).filePath("effect.particle"),
                                                tr("Particle effects (*.particle)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".particle"))
        path += ".particle";
    engine::ParticleEffect fx;
    engine::ParticleEmitter smoke;
    smoke.name = "Smoke";
    smoke.rate = 8.0f;
    smoke.lifeMin = 2.0f;
    smoke.lifeMax = 3.5f;
    smoke.speedMin = 0.4f;
    smoke.speedMax = 0.9f;
    smoke.gravity = -0.02f;
    smoke.sizeMin = 0.3f;
    smoke.sizeMax = 0.5f;
    smoke.sizeCurve = {{0.0f, glm::vec4(0.6f)}, {1.0f, glm::vec4(2.5f)}};
    smoke.colorCurve = {{0.0f, glm::vec4(1, 1, 1, 0)}, {0.15f, glm::vec4(1, 1, 1, 0.5f)}, {1.0f, glm::vec4(1, 1, 1, 0)}};
    smoke.color = glm::vec3(0.5f);
    smoke.rotationMax = 360.0f;
    smoke.spinMin = -30.0f;
    smoke.spinMax = 30.0f;
    const QString tex = assetPath("textures/particles/smoke_8x8.png");
    smoke.texture = std::filesystem::path(tex.toStdU16String());
    smoke.columns = smoke.rows = 8;
    fx.emitters.push_back(smoke);
    std::string error;
    if (!engine::saveParticleFile(fx, std::filesystem::path(path.toStdU16String()), &error)) {
        QMessageBox::warning(this, tr("Particles"), QString::fromStdString(error));
        return;
    }
    assets_->rescan();
    openParticleEditor(path);
}

void MainWindow::openSurfacesEditor()
{
    for (int i = 1; i < tabs_->count(); ++i)
        if (qobject_cast<SurfacesEditor*>(tabs_->widget(i))) {
            tabs_->setCurrentIndex(i);
            return;
        }
    auto* se = new SurfacesEditor(tabs_);
    QString error;
    if (!se->load(assetPath("surfaces.cfg"), &error)) {
        delete se;
        QMessageBox::warning(this, tr("Surfaces"), error);
        return;
    }
    const int index = tabs_->addTab(se, se->title());
    tabs_->setTabToolTip(index, se->path());
    tabs_->setCurrentIndex(index);
    connect(se, &SurfacesEditor::modificationChanged, this, [this, se] {
        if (const int i = tabs_->indexOf(se); i >= 0)
            tabs_->setTabText(i, se->title());
    });
    connect(se, &SurfacesEditor::openAsset, this, &MainWindow::openAssetFile);
}

SoundEditor* MainWindow::openSoundEditor(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (int i = 1; i < tabs_->count(); ++i)
        if (auto* se = qobject_cast<SoundEditor*>(tabs_->widget(i)); se && se->path() == absolute) {
            tabs_->setCurrentIndex(i);
            return se;
        }
    auto* se = new SoundEditor(tabs_);
    QString error;
    if (!se->load(absolute, &error)) {
        delete se;
        QMessageBox::warning(this, tr("Sound"), error);
        return nullptr;
    }
    const int index = tabs_->addTab(se, se->title());
    tabs_->setTabToolTip(index, absolute);
    tabs_->setCurrentIndex(index);
    connect(se, &SoundEditor::modificationChanged, this, [this, se] {
        if (const int i = tabs_->indexOf(se); i >= 0)
            tabs_->setTabText(i, se->title());
    });
    return se;
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
    else if (auto* sd = qobject_cast<SoundEditor*>(tabs_->currentWidget()))
        ok = sd->save(&error);
    else if (auto* su = qobject_cast<SurfacesEditor*>(tabs_->currentWidget()))
        ok = su->save(&error);
    else if (auto* pe = qobject_cast<ParticleEditor*>(tabs_->currentWidget()))
        ok = pe->save(&error);
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
    materialSlots_->setEnabled(m != nullptr); // the Details panel hides disabled rows
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

// ---------------------------------------------------------------------------
// Status bar: content drawer, output log, console; messages on the right.

void MainWindow::buildStatusBar()
{
    auto* bar = statusBar();
    bar->setSizeGripEnabled(false);
    auto toggleButton = [bar](const QString& icon, const QString& text, QDockWidget* dock) {
        auto* b = new QToolButton(bar);
        b->setIcon(editorIcon(icon));
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setAutoRaise(true);
        QObject::connect(b, &QToolButton::clicked, dock, [dock] {
            const bool show = !dock->isVisible() || dock->visibleRegion().isEmpty();
            dock->setVisible(true);
            if (show)
                dock->raise();
            else
                dock->hide();
        });
        return b;
    };
    auto* host = new QWidget(bar);
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(2);
    layout->addWidget(toggleButton("content", tr("Content Drawer"), assetsDock_));
    layout->addWidget(toggleButton("log", tr("Output Log"), logDock_));
    auto* cmdLabel = new QLabel(tr("Cmd"), host);
    cmdLabel->setObjectName("dim");
    auto* cmd = new QLineEdit(host);
    cmd->setPlaceholderText(tr("Enter console command (help)"));
    cmd->setMinimumWidth(260);
    cmd->setMaximumWidth(420);
    auto* completer = new QCompleter(QStringList{"help", "play", "stop", "pause", "save", "view perspective", "view top", "view front",
                                                 "view side", "view quad", "shading lit", "shading unlit", "shading lighting",
                                                 "shading wireframe", "quality 0", "quality 1", "quality 2", "quality 3", "r.ssao 0",
                                                 "r.ssao 1", "r.bloom 0", "r.bloom 1", "r.fxaa 0", "r.fxaa 1", "select ", "focus",
                                                 "tool select", "tool move", "tool rotate", "tool scale", "camspeed ", "stat",
                                                 "reloadshaders", "mode select", "mode place", "mode model", "clear"},
                                     cmd);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    cmd->setCompleter(completer);
    layout->addSpacing(8);
    layout->addWidget(cmdLabel);
    layout->addWidget(cmd);
    statusMessage_ = new QLabel(bar);
    statusMessage_->setObjectName("dim");
    bar->addPermanentWidget(host);
    bar->addPermanentWidget(statusMessage_, 1);
    statusMessage_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(cmd, &QLineEdit::returnPressed, this, [this, cmd] {
        const QString text = cmd->text().trimmed();
        if (text.isEmpty())
            return;
        commandHistory_.removeAll(text);
        commandHistory_ << text;
        cmd->clear();
        runCommand(text);
    });
    // Up / Down walk through the command history.
    auto* up = new QShortcut(QKeySequence(Qt::Key_Up), cmd, nullptr, nullptr, Qt::WidgetShortcut);
    auto* down = new QShortcut(QKeySequence(Qt::Key_Down), cmd, nullptr, nullptr, Qt::WidgetShortcut);
    auto step = [this, cmd](int dir) {
        if (commandHistory_.isEmpty())
            return;
        int i = static_cast<int>(commandHistory_.indexOf(cmd->text()));
        i = i < 0 ? (dir < 0 ? static_cast<int>(commandHistory_.size()) - 1 : 0)
                  : std::clamp(i + dir, 0, static_cast<int>(commandHistory_.size()) - 1);
        cmd->setText(commandHistory_[i]);
    };
    connect(up, &QShortcut::activated, this, [step] { step(-1); });
    connect(down, &QShortcut::activated, this, [step] { step(1); });
}

void MainWindow::notify(const QString& message, int timeoutMs)
{
    if (statusMessage_) {
        statusMessage_->setText(message);
        if (timeoutMs > 0)
            QTimer::singleShot(timeoutMs, statusMessage_, [label = statusMessage_, message] {
                if (label->text() == message)
                    label->clear();
            });
    }
    if (!message.isEmpty())
        appendLog(message, 0);
}

void MainWindow::appendLog(const QString& line, int level)
{
    if (!log_)
        return;
    const QString color = level >= 2 ? "#ff6b5e" : level == 1 ? "#e8c35a" : "#c8cbd0";
    log_->appendHtml(QStringLiteral("<span style='color:#6c6f75'>%1</span> <span style='color:%2'>%3</span>")
                         .arg(QTime::currentTime().toString("HH:mm:ss"), color, line.toHtmlEscaped()));
}

void MainWindow::runCommand(const QString& command)
{
    appendLog(QStringLiteral("> ") + command, 0);
    const QStringList words = command.split(' ', Qt::SkipEmptyParts);
    const QString name = words.value(0).toLower();
    const QString arg = words.value(1).toLower();
    auto boolArg = [&] { return arg == "1" || arg == "on" || arg == "true"; };
    auto setToggle = [&](bool engine::RenderQuality::*member, const char* key) {
        engine::RenderQuality q = viewport_->quality();
        q.*member = boolArg();
        QSettings().setValue(key, q.*member);
        setRenderQuality(q);
    };
    if (name == "help") {
        appendLog(tr("Commands: play, stop, pause, save, view perspective|top|front|side|quad, "
                     "shading lit|unlit|lighting|wireframe, quality 0-3, r.ssao|r.bloom|r.fxaa 0|1, select NAME, focus, "
                     "tool select|move|rotate|scale, camspeed M/S, mode select|place|model, stat, reloadshaders, clear"),
                  0);
    } else if (name == "play") {
        playLevel();
    } else if (name == "stop") {
        stopGame();
    } else if (name == "pause") {
        pauseGame();
    } else if (name == "save") {
        saveAll();
    } else if (name == "clear") {
        log_->clear();
    } else if (name == "view") {
        const QStringList views{"perspective", "top", "front", "side", "quad"};
        const int i = static_cast<int>(views.indexOf(arg));
        if (i < 0)
            appendLog(tr("view: perspective, top, front, side or quad"), 1);
        else
            viewportPanel_->setView(i);
    } else if (name == "shading") {
        const QStringList modes{"lit", "unlit", "lighting", "wireframe"};
        const int i = static_cast<int>(modes.indexOf(arg));
        if (i < 0)
            appendLog(tr("shading: lit, unlit, lighting or wireframe"), 1);
        else
            viewportPanel_->setShading(i);
    } else if (name == "quality") {
        bool ok = false;
        const int level = arg.toInt(&ok);
        if (!ok || level < 0 || level > 3) {
            appendLog(tr("quality: 0 low, 1 medium, 2 high, 3 ultra"), 1);
        } else {
            QSettings().setValue("render/quality", level);
            setRenderQuality(engine::RenderQuality::preset(level));
        }
    } else if (name == "r.ssao") {
        setToggle(&engine::RenderQuality::ssao, "render/ssao");
    } else if (name == "r.bloom") {
        setToggle(&engine::RenderQuality::bloom, "render/bloom");
    } else if (name == "r.fxaa") {
        setToggle(&engine::RenderQuality::fxaa, "render/fxaa");
    } else if (name == "select") {
        const std::string wanted = command.section(' ', 1).trimmed().toStdString();
        int found = -1;
        for (int i = 0; i < scene_.objectCount() && found < 0; ++i)
            if (engine::objectName(scene_, i) == wanted)
                found = i;
        if (found < 0)
            appendLog(tr("No object named %1").arg(QString::fromStdString(wanted)), 1);
        else
            select(found);
    } else if (name == "focus") {
        viewport_->focusSelected();
    } else if (name == "tool") {
        const QStringList tools{"select", "move", "rotate", "scale"};
        const int i = static_cast<int>(tools.indexOf(arg));
        if (i >= 0)
            viewport_->setTool(static_cast<Viewport::Tool>(i));
    } else if (name == "camspeed") {
        bool ok = false;
        const float v = arg.toFloat(&ok);
        if (ok && v > 0.0f)
            viewport_->setCameraSpeed(std::clamp(v, 0.5f, 200.0f));
    } else if (name == "mode") {
        const QStringList modes{"select", "place", "model"};
        const int i = static_cast<int>(modes.indexOf(arg));
        if (i >= 0)
            setEditorMode(i);
    } else if (name == "stat") {
        appendLog(tr("%1 boxes, %2 models, %3 entities, %4 groups; last frame: %5 culled, %6 model draw calls "
                     "(all passes), %7 model copies instanced")
                      .arg(scene_.boxes.size())
                      .arg(scene_.models.size())
                      .arg(scene_.entities.size())
                      .arg(scene_.groups.size())
                      .arg(viewport_->culledObjects())
                      .arg(viewport_->drawCalls())
                      .arg(viewport_->instancedObjects()),
                  0);
    } else if (name == "reloadshaders") {
        applyShaders();
    } else {
        appendLog(tr("Unknown command \"%1\" (type help)").arg(name), 1);
    }
}

// ---------------------------------------------------------------------------
// Play / Pause / Stop of the game process.

void MainWindow::updatePlayButtons()
{
    const bool running = game_ && game_->state() != QProcess::NotRunning;
    if (pauseAction_) {
        pauseAction_->setEnabled(running);
        pauseAction_->setChecked(running && gamePaused_);
        stopAction_->setEnabled(running);
    }
}

void MainWindow::pauseGame()
{
    if (!game_ || game_->state() != QProcess::Running) {
        updatePlayButtons();
        return;
    }
    const bool pause = !gamePaused_;
#ifdef Q_OS_WIN
    // Suspends every thread of the game.
    const DWORD pid = static_cast<DWORD>(game_->processId());
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID != pid)
                continue;
            if (HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID)) {
                if (pause)
                    SuspendThread(thread);
                else
                    ResumeThread(thread);
                CloseHandle(thread);
            }
        }
        CloseHandle(snapshot);
    }
#else
    ::kill(static_cast<pid_t>(game_->processId()), pause ? SIGSTOP : SIGCONT);
#endif
    gamePaused_ = pause;
    notify(pause ? tr("Game paused") : tr("Game resumed"), 3000);
    updatePlayButtons();
}

void MainWindow::stopGame()
{
    if (!game_ || game_->state() == QProcess::NotRunning)
        return;
    if (gamePaused_)
        pauseGame(); // a stopped process does not handle the terminate request
    game_->terminate();
    if (!game_->waitForFinished(1500))
        game_->kill();
}

void MainWindow::saveAll()
{
    QString error;
    if (!saveAllScripts())
        return;
    if (!weapons_->saveModified(&error))
        appendLog(error, 2);
    if (materialEditor_ && !materialEditor_->saveModified(&error))
        appendLog(error, 2);
    save();
}

// ---------------------------------------------------------------------------
// Details: category tabs and search.

void MainWindow::updateDetailsVisibility()
{
    if (!detailsTabs_)
        return;
    const engine::Box* b = selectedBox();
    const engine::Model* m = selectedModel();
    const engine::Entity* e = selectedEntity();
    const bool any = b || m || e;
    const QString category = QStringList{"General", "Transform", "Rendering", "Character", "Script", "Sound", "Level", "All"}.value(detailsTabs_->currentIndex());
    const QStringList words = detailsSearch_->text().toLower().split(' ', Qt::SkipEmptyParts);
    for (const DetailSection& d : detailSections_) {
        bool relevant = true;
        if (d.category == "General" || d.category == "Transform")
            relevant = any;
        else if (d.category == "Rendering")
            relevant = b || m || e;
        else if (d.category == "Character" || d.category == "Script")
            relevant = e != nullptr;
        else if (d.category == "Sound")
            relevant = e != nullptr && e->type == "sound";
        if (d.form == effectForm_)
            relevant = e != nullptr && e->type == "particle";
        bool shown = relevant && (category == "All" || category == d.category);
        // Search: rows whose label contains every word (a matching section title shows all rows).
        const bool titleMatch = std::all_of(words.begin(), words.end(), [&](const QString& w) { return d.section->title().toLower().contains(w); });
        int visibleRows = 0;
        for (int row = 0; row < d.form->rowCount(); ++row) {
            QString label;
            if (QLayoutItem* item = d.form->itemAt(row, QFormLayout::LabelRole); item && item->widget())
                if (auto* l = qobject_cast<QLabel*>(item->widget()))
                    label = l->text().toLower();
            // Fields that do not apply to the selection (disabled) are hidden.
            QLayoutItem* field = d.form->itemAt(row, QFormLayout::FieldRole);
            if (!field)
                field = d.form->itemAt(row, QFormLayout::SpanningRole);
            const bool applies = !any || !field || !field->widget() || field->widget()->isEnabled();
            const bool match = applies && (words.isEmpty() || titleMatch ||
                                           std::all_of(words.begin(), words.end(), [&](const QString& w) { return label.contains(w); }));
            d.form->setRowVisible(row, match);
            visibleRows += match ? 1 : 0;
        }
        if (!words.isEmpty() && visibleRows == 0)
            shown = false;
        d.section->setVisible(shown);
    }
}

// ---------------------------------------------------------------------------
// Floating transform box in the viewport.

void MainWindow::refreshTransformBox()
{
    TransformBox* box = viewportPanel_->transformBox();
    if (selected_ < 0) {
        box->hide();
        return;
    }
    if (auto* b = selectedBox())
        box->setValues(b->position, b->rotation, b->size, tr("Size"), true, true);
    else if (auto* m = selectedModel())
        box->setValues(m->position, m->rotation, glm::vec3(m->scale), tr("Scale"), true, true);
    else if (auto* e = selectedEntity())
        box->setValues(e->position, glm::vec3(0.0f, e->yawDegrees, 0.0f), e->size, tr("Size"), true, true);
    box->show();
    box->adjustSize();
    box->raise();
}

void MainWindow::applyTransformBox(int field, glm::vec3 v, bool delta)
{
    if (selected_ < 0)
        return;
    switch (field) {
    case TransformBox::Location: {
        const glm::vec3 move = delta ? v : v - engine::objectPosition(scene_, selected_);
        for (int child : engine::descendantObjects(scene_, engine::objectName(scene_, selected_)))
            engine::translateObject(scene_, child, move);
        engine::translateObject(scene_, selected_, move);
        break;
    }
    case TransformBox::Rotation:
        if (auto* b = selectedBox())
            b->rotation = delta ? b->rotation + v : v;
        else if (auto* m = selectedModel())
            m->rotation = delta ? m->rotation + v : v;
        else if (auto* e = selectedEntity())
            e->yawDegrees = delta ? e->yawDegrees + v.y : v.y;
        break;
    case TransformBox::Scale:
        if (auto* b = selectedBox())
            b->size = glm::max(delta ? b->size * v : v, glm::vec3(0.01f));
        else if (auto* m = selectedModel()) {
            // Models scale uniformly: take the component that changed.
            const glm::vec3 cur(m->scale);
            float s = v.x;
            for (int i = 0; i < 3; ++i)
                if (delta ? v[i] != 1.0f : v[i] != cur[i])
                    s = v[i];
            m->scale = std::max(0.01f, delta ? m->scale * s : s);
        } else if (auto* e = selectedEntity())
            e->size = glm::max(delta ? e->size * v : v, glm::vec3(0.05f));
        break;
    default: break;
    }
    refreshProperties();
    markDirty();
}

// ---------------------------------------------------------------------------
// Left tool panel: Select, Place and Model modes.

void MainWindow::setEditorMode(int mode)
{
    if (!modePages_)
        return;
    modePages_->setCurrentIndex(mode);
    if (mode >= 0 && mode < static_cast<int>(modeActions_.size()))
        modeActions_[static_cast<std::size_t>(mode)]->setChecked(true);
    modesDock_->setWindowTitle(QStringList{tr("Select"), tr("Place"), tr("Model"), tr("Landscape"), tr("Foliage")}.value(mode));
    modesDock_->show();
    if (mode == 2)
        refreshMaterialList();
    if (mode == 3 || mode == 4)
        refreshTerrainPanel();
    updateTerrainBrush();
}

void MainWindow::buildModePanel()
{
    modePages_ = new QStackedWidget(this);
    auto toolButton = [](const QString& icon, const QString& text, QWidget* parent) {
        auto* b = new QPushButton(icon.isEmpty() ? QIcon() : editorIcon(icon), text, parent);
        b->setObjectName("tool");
        b->setMinimumHeight(26);
        return b;
    };

    // Select: what is selected and the common actions.
    {
        auto* page = new QWidget(modePages_);
        auto* layout = new QVBoxLayout(page);
        selectionInfo_ = new QLabel(page);
        selectionInfo_->setWordWrap(true);
        layout->addWidget(selectionInfo_);
        auto* grid = new QGridLayout();
        auto* focus = toolButton("focus", tr("Focus"), page);
        auto* floor = toolButton("floor", tr("To floor"), page);
        auto* dup = toolButton("duplicate", tr("Duplicate"), page);
        auto* del = toolButton("delete", tr("Delete"), page);
        grid->addWidget(focus, 0, 0);
        grid->addWidget(floor, 0, 1);
        grid->addWidget(dup, 1, 0);
        grid->addWidget(del, 1, 1);
        layout->addLayout(grid);
        auto* help = new QLabel(tr("<p><b>Mouse</b><br>Left click: select<br>Right drag + WASD/QE: fly<br>Wheel while flying: speed</p>"
                                   "<p><b>Keys</b><br>Q W E R: select, move, rotate, scale<br>F: focus · End: to floor<br>"
                                   "Ctrl+1..5: views · Alt+1..4: shading<br>Shift while dragging: no snap</p>"),
                               page);
        help->setObjectName("dim");
        help->setWordWrap(true);
        layout->addWidget(help);
        layout->addStretch(1);
        connect(focus, &QPushButton::clicked, this, [this] { viewport_->focusSelected(); });
        connect(floor, &QPushButton::clicked, this, &MainWindow::dropToFloor);
        connect(dup, &QPushButton::clicked, this, &MainWindow::duplicateBox);
        connect(del, &QPushButton::clicked, this, &MainWindow::deleteBox);
        modePages_->addWidget(page);
    }

    // Place: things to add to the level.
    {
        auto* page = new QWidget(modePages_);
        auto* layout = new QVBoxLayout(page);
        auto* box = toolButton("cube", tr("Box"), page);
        auto* enemy = toolButton("person", tr("Enemy (random human)"), page);
        auto* entity = toolButton("select", tr("Empty entity"), page);
        auto* sound = toolButton("sound", tr("Sound source / ambient zone"), page);
        auto* particles = toolButton("particles", tr("Particle effect (fire, smoke, dust)"), page);
        auto* model = toolButton("model", tr("glTF model..."), page);
        for (QPushButton* b : {box, enemy, entity, sound, particles, model})
            layout->addWidget(b);
        auto* hint = new QLabel(tr("Or drag a model from the Content Browser into the view: it lands where you drop it."), page);
        hint->setObjectName("dim");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        layout->addStretch(1);
        connect(box, &QPushButton::clicked, this, &MainWindow::addBox);
        connect(enemy, &QPushButton::clicked, this, [this] { addEntity("enemy"); });
        connect(entity, &QPushButton::clicked, this, [this] { addEntity("empty"); });
        connect(sound, &QPushButton::clicked, this, [this] { addEntity("sound"); });
        connect(particles, &QPushButton::clicked, this, [this] { addEntity("particle"); });
        connect(model, &QPushButton::clicked, this, &MainWindow::importModel);
        modePages_->addWidget(page);
    }

    // Model: mesh tools, texture placement and the active material.
    {
        auto* page = new QWidget(modePages_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* meshSection = new CollapsibleSection(tr("Model"), page);
        auto* meshGrid = new QGridLayout();
        meshGrid->setSpacing(3);
        const char* meshTools[] = {"Delete", "Detach", "Duplicate", "Collapse", "Offset", "Invert", "Transform Texture", "Inset", "Cut",
                                   "Clean", "Extract", "Merge Faces", "Extrude", "Recalculate Normals", "Edit Pivot", "PushPull",
                                   "Outset", "Bevel"};
        int index = 0;
        for (const char* t : meshTools) {
            auto* b = toolButton(QString(), tr(t), page);
            const QString name = QString::fromLatin1(t);
            if (name == "Delete") {
                b->setIcon(editorIcon("delete"));
                connect(b, &QPushButton::clicked, this, &MainWindow::deleteBox);
            } else if (name == "Duplicate") {
                b->setIcon(editorIcon("duplicate"));
                connect(b, &QPushButton::clicked, this, &MainWindow::duplicateBox);
            } else if (name == "Transform Texture") {
                connect(b, &QPushButton::clicked, this, [this] {
                    if (texturePanel_)
                        texturePanel_->setFocus();
                    if (uvSpins_[0])
                        uvSpins_[0]->setFocus();
                });
            } else {
                b->setEnabled(false);
                b->setToolTip(tr("%1 works on editable meshes (face / edge / vertex selection), which come next.").arg(tr(t)));
            }
            meshGrid->addWidget(b, index / 2, index % 2);
            ++index;
        }
        meshSection->setBodyLayout(meshGrid);
        layout->addWidget(meshSection);

        // Texture placement on boxes (world-aligned mapping + scale / shift / rotation).
        auto* textureSection = new CollapsibleSection(tr("Modify Texture"), page);
        texturePanel_ = textureSection;
        auto* textureLayout = new QVBoxLayout();
        auto* textureGrid = new QGridLayout();
        textureGrid->setSpacing(3);
        struct TexTool {
            const char* name;
            const char* tip;
            std::function<void(engine::Box&)> edit;
        };
        auto maxTiling = [](const engine::Box& b) { return b.material ? std::max(b.material->tiling.x, b.material->tiling.y) : 1.0f; };
        const std::vector<TexTool> texTools = {
            {"Align", "Back to the world grid: no shift, no rotation", [](engine::Box& b) { b.uvOffset = glm::vec2(0.0f); b.uvRotation = 0.0f; }},
            {"Scale", "Texture twice as dense (Shift+click: half)",
             [](engine::Box& b) { b.uvScale *= (QApplication::keyboardModifiers() & Qt::ShiftModifier) ? 0.5f : 2.0f; }},
            {"Shift", "Move by a quarter tile along U (Shift+click: along V)",
             [](engine::Box& b) {
                 const int axis = (QApplication::keyboardModifiers() & Qt::ShiftModifier) ? 1 : 0;
                 b.uvOffset[axis] = std::fmod(b.uvOffset[axis] + 0.25f, 1.0f);
             }},
            {"Rotate", "Rotate the texture by 90°", [](engine::Box& b) { b.uvRotation = std::fmod(b.uvRotation + 90.0f, 360.0f); }},
            {"Fit", "One tile across the largest face of the box",
             [maxTiling](engine::Box& b) {
                 const float extent = std::max({b.size.x, b.size.y, b.size.z});
                 b.uvScale = glm::vec2(1.0f / std::max(0.01f, extent * maxTiling(b)));
                 b.uvOffset = glm::vec2(0.0f);
             }},
            {"Justify", "Start the texture at the box's lower corner",
             [maxTiling](engine::Box& b) {
                 const glm::vec3 lo = b.position - b.size * 0.5f;
                 const glm::vec2 t = b.uvScale * maxTiling(b);
                 b.uvOffset = glm::vec2(-std::fmod(lo.x * t.x, 1.0f), std::fmod(lo.y * t.y, 1.0f));
             }},
            {"Treat as One", "", nullptr},
            {"Hotspot", "", nullptr},
        };
        index = 0;
        for (const TexTool& t : texTools) {
            auto* b = toolButton(QString(), tr(t.name), page);
            if (t.edit) {
                b->setToolTip(tr(t.tip));
                connect(b, &QPushButton::clicked, this, [this, edit = t.edit] { editBoxUv(edit); });
            } else {
                b->setEnabled(false);
                b->setToolTip(tr("Needs editable meshes (comes with mesh modelling)"));
            }
            textureGrid->addWidget(b, index / 2, index % 2);
            ++index;
        }
        textureLayout->addLayout(textureGrid);
        auto* uvForm = new QFormLayout();
        const char* uvNames[5] = {"Scale U", "Scale V", "Shift U", "Shift V", "Rotation"};
        for (int i = 0; i < 5; ++i) {
            auto* spin = new QDoubleSpinBox(page);
            spin->setDecimals(3);
            spin->setKeyboardTracking(false);
            if (i < 2) {
                spin->setRange(0.01, 100.0);
                spin->setSingleStep(0.1);
            } else if (i < 4) {
                spin->setRange(-100.0, 100.0);
                spin->setSingleStep(0.05);
            } else {
                spin->setRange(-360.0, 360.0);
                spin->setSingleStep(15.0);
                spin->setSuffix(QStringLiteral(" °"));
            }
            uvSpins_[i] = spin;
            uvForm->addRow(tr(uvNames[i]), spin);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
                editBoxUv([i, v](engine::Box& b) {
                    const float f = static_cast<float>(v);
                    if (i < 2)
                        b.uvScale[i] = f;
                    else if (i < 4)
                        b.uvOffset[i - 2] = f;
                    else
                        b.uvRotation = f;
                });
            });
        }
        textureLayout->addLayout(uvForm);
        textureSection->setBodyLayout(textureLayout);
        layout->addWidget(textureSection);

        // Active material: what Apply Material puts on the selection.
        auto* materialSection = new CollapsibleSection(tr("Active Material"), page);
        auto* materialLayout = new QVBoxLayout();
        activeMaterial_ = new QComboBox(page);
        activeMaterial_->setIconSize(QSize(32, 32));
        activeMaterial_->setToolTip(tr("Materials under assets/materials"));
        auto* materialButtons = new QHBoxLayout();
        auto* apply = new QPushButton(tr("Apply Material"), page);
        apply->setObjectName("accent");
        auto* edit = new QPushButton(tr("Edit"), page);
        materialButtons->addWidget(apply, 1);
        materialButtons->addWidget(edit);
        materialLayout->addWidget(activeMaterial_);
        materialLayout->addLayout(materialButtons);
        materialSection->setBodyLayout(materialLayout);
        layout->addWidget(materialSection);
        layout->addStretch(1);
        connect(apply, &QPushButton::clicked, this, [this] {
            const QString path = activeMaterial_->currentData().toString();
            if (path.isEmpty() || selected_ < 0) {
                notify(tr("Select a box or a model, then Apply Material."), 4000);
                return;
            }
            if (!assignMaterial(path, selected_))
                notify(tr("Materials go on boxes and models."), 4000);
        });
        connect(edit, &QPushButton::clicked, this, [this] {
            const QString path = activeMaterial_->currentData().toString();
            if (!path.isEmpty())
                openMaterialEditor(path);
        });
        // Picking a material in the Content Browser makes it the active one.
        connect(assets_, &AssetBrowser::assetSelected, this, [this](const QString& path, AssetType type) {
            if (type != AssetType::Material)
                return;
            if (activeMaterial_->findData(path) < 0)
                refreshMaterialList();
            activeMaterial_->setCurrentIndex(activeMaterial_->findData(path));
        });

        auto* scroll = new QScrollArea(modePages_);
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        modePages_->addWidget(scroll);
    }

    buildTerrainPages();

    modesDock_ = new QDockWidget(tr("Model"), this);
    modesDock_->setObjectName("modes");
    modesDock_->setWidget(modePages_);
    modesDock_->setMinimumWidth(250);
    addDockWidget(Qt::LeftDockWidgetArea, modesDock_);
    resizeDocks({modesDock_}, {270}, Qt::Horizontal);
    setEditorMode(2);
}

void MainWindow::refreshMaterialList()
{
    if (!activeMaterial_)
        return;
    const QString current = activeMaterial_->currentData().toString();
    activeMaterial_->blockSignals(true);
    activeMaterial_->clear();
    QDirIterator it(assetPath("materials"), {"*.material"}, QDir::Files, QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext())
        files << it.next();
    files.sort(Qt::CaseInsensitive);
    for (const QString& f : files) {
        QIcon icon = editorIcon("content");
        // Same thumbnails as the Content Browser when they are cached.
        if (QIcon thumb = assets_->iconFor(f); !thumb.isNull())
            icon = thumb;
        activeMaterial_->addItem(icon, QFileInfo(f).completeBaseName(), f);
    }
    const int index = activeMaterial_->findData(current);
    activeMaterial_->setCurrentIndex(index >= 0 ? index : 0);
    activeMaterial_->blockSignals(false);
}

void MainWindow::editBoxUv(const std::function<void(engine::Box&)>& edit)
{
    engine::Box* b = selectedBox();
    if (!b) {
        notify(tr("Texture tools work on boxes: select one."), 4000);
        return;
    }
    const glm::vec2 scale = b->uvScale, offset = b->uvOffset;
    const float rotation = b->uvRotation;
    edit(*b);
    b->uvScale = glm::max(b->uvScale, glm::vec2(0.001f));
    if (b->uvScale == scale && b->uvOffset == offset && b->uvRotation == rotation)
        return;
    refreshTexturePanel();
    markDirty();
}

void MainWindow::refreshTexturePanel()
{
    if (selectionInfo_) {
        if (selected_ < 0)
            selectionInfo_->setText(tr("<b>Nothing selected</b><br>Click an object in the view or the Outliner."));
        else
            selectionInfo_->setText(tr("<b>%1</b><br>%2").arg(QString::fromStdString(engine::objectName(scene_, selected_)).toHtmlEscaped(),
                                                             kindLabel_->text().toHtmlEscaped()));
    }
    if (!uvSpins_[0])
        return;
    const engine::Box* b = selectedBox();
    const float values[5] = {b ? b->uvScale.x : 1.0f, b ? b->uvScale.y : 1.0f, b ? b->uvOffset.x : 0.0f, b ? b->uvOffset.y : 0.0f,
                             b ? b->uvRotation : 0.0f};
    for (int i = 0; i < 5; ++i) {
        uvSpins_[i]->blockSignals(true);
        uvSpins_[i]->setValue(values[i]);
        uvSpins_[i]->setEnabled(b != nullptr);
        uvSpins_[i]->blockSignals(false);
    }
}
