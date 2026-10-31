#include "MainWindow.h"
#include "Vec3Editor.h"
#include "Viewport.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDir>
#include <QDockWidget>
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
    QAction* dup = edit->addAction(tr("&Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), this, &MainWindow::duplicateBox);
    QAction* del = edit->addAction(tr("De&lete"), QKeySequence::Delete, this, &MainWindow::deleteBox);

    QMenu* game = menuBar()->addMenu(tr("&Game"));
    QAction* play = game->addAction(tr("&Play level"), QKeySequence(Qt::Key_F5), this, &MainWindow::playLevel);

    QToolBar* tools = addToolBar(tr("Tools"));
    tools->setObjectName("tools");
    tools->addAction(add);
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
    positionEdit_ = new Vec3Editor(-10000.0, 10000.0, 0.25, boxPanel_);
    sizeEdit_ = new Vec3Editor(0.01, 10000.0, 0.25, boxPanel_);
    colorButton_ = new QPushButton(boxPanel_);
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Position"), positionEdit_);
    form->addRow(tr("Size"), sizeEdit_);
    form->addRow(tr("Color"), colorButton_);

    connect(nameEdit_, &QLineEdit::editingFinished, this, [this] {
        if (auto* b = selectedBox(); b && b->name != nameEdit_->text().toStdString()) {
            b->name = nameEdit_->text().toStdString();
            outliner_->item(selected_)->setText(nameEdit_->text());
            markDirty();
        }
    });
    connect(positionEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        if (auto* b = selectedBox()) {
            b->position = v;
            markDirty();
        }
    });
    connect(sizeEdit_, &Vec3Editor::valueEdited, this, [this](glm::vec3 v) {
        if (auto* b = selectedBox()) {
            b->size = v;
            markDirty();
        }
    });
    connect(colorButton_, &QPushButton::clicked, this, [this] {
        auto* b = selectedBox();
        if (!b)
            return;
        const QColor initial = QColor::fromRgbF(b->color.r, b->color.g, b->color.b);
        const QColor c = QColorDialog::getColor(initial, this, tr("Box color"));
        if (!c.isValid())
            return;
        b->color = {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF())};
        colorButton_->setStyleSheet(colorStyle(b->color));
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
    auto* boxTitle = new QLabel(tr("<b>Selected box</b>"), propsHost);
    auto* levelTitle = new QLabel(tr("<b>Level</b>"), propsHost);
    propsLayout->addWidget(boxTitle);
    propsLayout->addWidget(boxPanel_);
    propsLayout->addSpacing(12);
    propsLayout->addWidget(levelTitle);
    propsLayout->addWidget(levelPanel);
    propsLayout->addStretch(1);

    auto* propsDock = new QDockWidget(tr("Properties"), this);
    propsDock->setObjectName("properties");
    propsDock->setWidget(propsHost);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);
}

engine::Box* MainWindow::selectedBox()
{
    if (selected_ < 0 || selected_ >= static_cast<int>(scene_.boxes.size()))
        return nullptr;
    return &scene_.boxes[static_cast<std::size_t>(selected_)];
}

void MainWindow::select(int index)
{
    selected_ = (index >= 0 && index < static_cast<int>(scene_.boxes.size())) ? index : -1;
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
    outliner_->setCurrentRow(selected_);
    outliner_->blockSignals(false);
}

void MainWindow::refreshProperties()
{
    const engine::Box* b = selectedBox();
    boxPanel_->setEnabled(b != nullptr);
    if (!b) {
        nameEdit_->clear();
        colorButton_->setStyleSheet(QString());
        return;
    }
    nameEdit_->setText(QString::fromStdString(b->name));
    positionEdit_->setValue(b->position);
    sizeEdit_->setValue(b->size);
    colorButton_->setStyleSheet(colorStyle(b->color));
}

void MainWindow::refreshLevelProperties()
{
    playerStartEdit_->setValue(scene_.playerStart);
    sunEdit_->setValue(scene_.sunDirection);
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

void MainWindow::duplicateBox()
{
    const engine::Box* b = selectedBox();
    if (!b)
        return;
    engine::Box copy = *b;
    copy.name += " copy";
    copy.position.x += copy.size.x;
    scene_.boxes.push_back(copy);
    refreshOutliner();
    select(static_cast<int>(scene_.boxes.size()) - 1);
    markDirty();
}

void MainWindow::deleteBox()
{
    if (!selectedBox())
        return;
    scene_.boxes.erase(scene_.boxes.begin() + selected_);
    const int next = std::min(selected_, static_cast<int>(scene_.boxes.size()) - 1);
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
    if (!engine::loadScene(loaded, path.toStdString(), &error)) {
        QMessageBox::warning(this, tr("Open level"), QString::fromStdString(error));
        return false;
    }
    scene_ = std::move(loaded);
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
    std::string error;
    if (!engine::saveScene(scene_, path.toStdString(), &error)) {
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
    std::string error;
    if (!engine::saveScene(scene_, snapshot.toStdString(), &error)) {
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
