#include "MainWindow.h"
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
    setCentralWidget(viewport_);
    connect(viewport_, &Viewport::boxPicked, this, &MainWindow::select);
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
    file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
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
    // Outliner: list of boxes in the level.
    outliner_ = new QListWidget(this);
    connect(outliner_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row != selected_)
            select(row);
    });
    auto* outlinerDock = new QDockWidget(tr("Outliner"), this);
    outlinerDock->setObjectName("outliner");
    outlinerDock->setWidget(outliner_);
    addDockWidget(Qt::LeftDockWidgetArea, outlinerDock);

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
    auto* editScript = new QPushButton(tr("Open script in text editor"), boxPanel_);
    form->addRow(scriptParams_);
    form->addRow(editScript);
    connect(editScript, &QPushButton::clicked, this, [this] {
        if (auto* e = selectedEntity(); e && !e->scriptPath.empty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdU16String(e->scriptPath.u16string())));
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
        std::string* name = selectedBox()      ? &selectedBox()->name
                          : selectedModel()  ? &selectedModel()->name
                          : selectedEntity() ? &selectedEntity()->name
                                             : nullptr;
        if (name && *name != nameEdit_->text().toStdString()) {
            *name = nameEdit_->text().toStdString();
            refreshOutliner();
            markDirty();
        }
    });
    connect(positionEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
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

    // Weapons: the level's player loadout and the .weapon files it uses.
    weapons_ = new WeaponPanel(&scene_, this);
    connect(weapons_, &WeaponPanel::loadoutChanged, this, &MainWindow::markDirty);
    auto* weaponsDock = new QDockWidget(tr("Weapons"), this);
    weaponsDock->setObjectName("weapons");
    weaponsDock->setWidget(weapons_);
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
    outliner_->blockSignals(true);
    outliner_->setCurrentRow(selected_);
    outliner_->blockSignals(false);
    viewport_->setSelected(selected_);
    refreshProperties();
}

void MainWindow::refreshOutliner()
{
    outliner_->blockSignals(true);
    outliner_->clear();
    for (const engine::Box& b : scene_.boxes)
        outliner_->addItem(QString::fromStdString(b.name));
    for (const engine::Model& m : scene_.models)
        outliner_->addItem(QString::fromStdString(m.name) + tr("  [glTF]"));
    for (const engine::Entity& e : scene_.entities)
        outliner_->addItem(QString::fromStdString(e.name) + QString("  [%1]").arg(QString::fromStdString(e.type)));
    outliner_->setCurrentRow(selected_);
    outliner_->blockSignals(false);
}

void MainWindow::refreshProperties()
{
    const engine::Box* b = selectedBox();
    const engine::Model* m = selectedModel();
    const engine::Entity* e = selectedEntity();
    boxPanel_->setEnabled(b || m || e);
    sizeEdit_->setEnabled(b || e);
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
    box.name = "Box " + std::to_string(scene_.boxes.size() + 1);
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
    engine::Model model;
    std::string error;
    if (!engine::loadModelFile(model, std::filesystem::path(path.toStdU16String()), &error)) {
        QMessageBox::warning(this, tr("Import glTF"), QString::fromStdString(error));
        return;
    }
    model.name = QFileInfo(path).completeBaseName().toStdString();
    // Place it on the ground in front of the camera.
    const auto& cam = viewport_->camera();
    const glm::vec3 p = cam.position + cam.flatForward() * 8.0f;
    model.position = {std::round(p.x), 0.0f, std::round(p.z)};
    scene_.models.push_back(std::move(model));
    refreshOutliner();
    select(scene_.firstEntity() - 1);
    markDirty();
}

void MainWindow::addEntity(const QString& type)
{
    const auto& cam = viewport_->camera();
    const glm::vec3 p = cam.position + cam.flatForward() * 6.0f;
    engine::Entity e;
    e.type = type.toStdString();
    e.name = (type == "enemy" ? "Enemy " : "Entity ") + std::to_string(scene_.entities.size() + 1);
    e.position = {std::round(p.x * 2.0f) * 0.5f, 0.0f, std::round(p.z * 2.0f) * 0.5f};
    if (type == "enemy") {
        e.color = {0.55f, 0.5f, 0.5f};
        // Default AI script shipped next to the editor (copied by the game build).
        const QString script = QDir(QCoreApplication::applicationDirPath()).filePath("assets/scripts/enemy.lua");
        if (QFileInfo::exists(script))
            e.scriptPath = std::filesystem::path(script.toStdU16String());
        // Default animated character shipped with the engine.
        const QString robot = QDir(QCoreApplication::applicationDirPath()).filePath("assets/models/characters/robot.glb");
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
    QString start = e->modelPath.empty() ? QDir(QCoreApplication::applicationDirPath()).filePath("assets/models/characters")
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
        copy.name += " copy";
        copy.position.x += copy.size.x;
        scene_.boxes.push_back(copy);
        refreshOutliner();
        select(static_cast<int>(scene_.boxes.size()) - 1);
    } else if (const engine::Model* m = selectedModel()) {
        engine::Model copy = *m; // shares mesh ids, so the GPU data is shared too
        copy.name += " copy";
        copy.position.x += copy.localBounds.max.x - copy.localBounds.min.x + 1.0f;
        scene_.models.push_back(std::move(copy));
        refreshOutliner();
        select(scene_.firstEntity() - 1);
    } else if (const engine::Entity* en = selectedEntity()) {
        engine::Entity copy = *en;
        copy.name += " copy";
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
    if (confirmDiscard())
        e->accept();
    else
        e->ignore();
}
