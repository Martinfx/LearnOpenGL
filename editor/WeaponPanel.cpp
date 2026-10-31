#include "WeaponPanel.h"
#include "Vec3Editor.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
QString toQString(const std::filesystem::path& p) { return QString::fromStdU16String(p.u16string()); }
std::filesystem::path toPath(const QString& s) { return std::filesystem::path(s.toStdU16String()); }

QDoubleSpinBox* spin(QWidget* parent, double min, double max, double step, int decimals, const QString& suffix)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}

QString defaultWeaponDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("assets/weapons");
}
} // namespace

WeaponPanel::WeaponPanel(engine::Scene* scene, QWidget* parent) : QWidget(parent), scene_(scene)
{
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("<b>Player loadout</b> (slot 1, 2, ...)"), this));
    list_ = new QListWidget(this);
    list_->setMaximumHeight(120);
    layout->addWidget(list_);

    auto* buttons = new QHBoxLayout();
    auto* add = new QPushButton(tr("Add..."), this);
    auto* create = new QPushButton(tr("New..."), this);
    auto* remove = new QPushButton(tr("Remove"), this);
    auto* up = new QPushButton(tr("Up"), this);
    auto* down = new QPushButton(tr("Down"), this);
    for (QPushButton* b : {add, create, remove, up, down})
        buttons->addWidget(b);
    layout->addLayout(buttons);

    form_ = new QWidget(this);
    auto* form = new QFormLayout(form_);
    form->setContentsMargins(0, 8, 0, 0);
    name_ = new QLineEdit(form_);
    auto* modelRow = new QWidget(form_);
    auto* modelLayout = new QHBoxLayout(modelRow);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    model_ = new QLineEdit(modelRow);
    model_->setReadOnly(true);
    auto* browse = new QPushButton(tr("..."), modelRow);
    browse->setFixedWidth(32);
    modelLayout->addWidget(model_);
    modelLayout->addWidget(browse);
    damage_ = spin(form_, 0, 1000, 1, 1, tr(" per pellet"));
    interval_ = spin(form_, 0.01, 10, 0.01, 3, tr(" s"));
    automatic_ = new QCheckBox(tr("Hold to fire"), form_);
    pellets_ = new QSpinBox(form_);
    pellets_->setRange(1, 64);
    magazine_ = new QSpinBox(form_);
    magazine_->setRange(1, 1000);
    reload_ = spin(form_, 0, 10, 0.1, 2, tr(" s"));
    spread_ = spin(form_, 0, 45, 0.1, 2, tr(" °"));
    range_ = spin(form_, 1, 5000, 10, 0, tr(" m"));
    viewOffset_ = new Vec3Editor(-2.0, 2.0, 0.01, form_);
    viewScale_ = spin(form_, 0.001, 10, 0.01, 3, QString());

    form->addRow(tr("Name"), name_);
    form->addRow(tr("Model (glTF)"), modelRow);
    form->addRow(tr("Damage"), damage_);
    form->addRow(tr("Fire interval"), interval_);
    form->addRow(tr("Automatic"), automatic_);
    form->addRow(tr("Pellets"), pellets_);
    form->addRow(tr("Magazine"), magazine_);
    form->addRow(tr("Reload time"), reload_);
    form->addRow(tr("Spread"), spread_);
    form->addRow(tr("Range"), range_);
    form->addRow(tr("View offset"), viewOffset_);
    form->addRow(tr("View scale"), viewScale_);
    layout->addWidget(form_);
    layout->addStretch(1);

    connect(list_, &QListWidget::currentRowChanged, this, [this] { showCurrent(); });
    connect(add, &QPushButton::clicked, this, &WeaponPanel::addExisting);
    connect(create, &QPushButton::clicked, this, &WeaponPanel::createNew);
    connect(remove, &QPushButton::clicked, this, &WeaponPanel::removeCurrent);
    connect(up, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down, &QPushButton::clicked, this, [this] { move(1); });
    connect(browse, &QPushButton::clicked, this, [this] {
        auto* w = current();
        if (!w)
            return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Weapon model"), QString(),
                                                          tr("glTF 2.0 (*.gltf *.glb);;All files (*)"));
        if (path.isEmpty())
            return;
        w->modelPath = toPath(path);
        model_->setText(QFileInfo(path).fileName());
        edited();
    });
    connect(name_, &QLineEdit::editingFinished, this, &WeaponPanel::edited);
    for (QDoubleSpinBox* s : {damage_, interval_, reload_, spread_, range_, viewScale_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &WeaponPanel::edited);
    for (QSpinBox* s : {pellets_, magazine_})
        connect(s, &QSpinBox::valueChanged, this, &WeaponPanel::edited);
    connect(automatic_, &QCheckBox::toggled, this, &WeaponPanel::edited);
    connect(viewOffset_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);

    refresh();
}

void WeaponPanel::refresh()
{
    const int row = list_->currentRow();
    list_->blockSignals(true);
    list_->clear();
    for (std::size_t i = 0; i < scene_->loadout.size(); ++i) {
        const auto& path = scene_->loadout[i];
        if (!cache_.count(path)) {
            engine::WeaponSettings w;
            std::string error;
            if (!engine::loadWeaponFile(w, path, &error))
                w.name = "(missing) " + path.filename().string();
            cache_[path] = w;
        }
        list_->addItem(QString("%1  %2").arg(i + 1).arg(QString::fromStdString(cache_[path].name)));
    }
    list_->setCurrentRow(std::min(std::max(row, 0), list_->count() - 1));
    list_->blockSignals(false);
    showCurrent();
}

engine::WeaponSettings* WeaponPanel::current()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(scene_->loadout.size()))
        return nullptr;
    return &cache_[scene_->loadout[static_cast<std::size_t>(row)]];
}

void WeaponPanel::showCurrent()
{
    const engine::WeaponSettings* w = current();
    form_->setEnabled(w != nullptr);
    if (!w)
        return;
    updating_ = true;
    name_->setText(QString::fromStdString(w->name));
    model_->setText(w->modelPath.empty() ? QString() : toQString(w->modelPath.filename()));
    model_->setToolTip(toQString(w->modelPath));
    damage_->setValue(w->damage);
    interval_->setValue(w->fireInterval);
    automatic_->setChecked(w->automatic);
    pellets_->setValue(w->pellets);
    magazine_->setValue(w->magazineSize);
    reload_->setValue(w->reloadTime);
    spread_->setValue(w->spreadDegrees);
    range_->setValue(w->range);
    viewOffset_->setValue(w->viewOffset);
    viewScale_->setValue(w->viewScale);
    updating_ = false;
}

void WeaponPanel::edited()
{
    auto* w = current();
    if (!w || updating_)
        return;
    w->name = name_->text().toStdString();
    w->damage = static_cast<float>(damage_->value());
    w->fireInterval = static_cast<float>(interval_->value());
    w->automatic = automatic_->isChecked();
    w->pellets = pellets_->value();
    w->magazineSize = magazine_->value();
    w->reloadTime = static_cast<float>(reload_->value());
    w->spreadDegrees = static_cast<float>(spread_->value());
    w->range = static_cast<float>(range_->value());
    w->viewOffset = viewOffset_->value();
    w->viewScale = static_cast<float>(viewScale_->value());
    modified_[scene_->loadout[static_cast<std::size_t>(list_->currentRow())]] = true;
    list_->currentItem()->setText(QString("%1  %2").arg(list_->currentRow() + 1).arg(name_->text()));
    emit loadoutChanged();
}

void WeaponPanel::addExisting()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Add weapon"), defaultWeaponDir(),
                                                      tr("Weapons (*.weapon);;All files (*)"));
    if (path.isEmpty())
        return;
    engine::WeaponSettings w;
    std::string error;
    if (!engine::loadWeaponFile(w, toPath(path), &error)) {
        QMessageBox::warning(this, tr("Add weapon"), QString::fromStdString(error));
        return;
    }
    cache_[toPath(path)] = w;
    scene_->loadout.push_back(toPath(path));
    refresh();
    list_->setCurrentRow(list_->count() - 1);
    emit loadoutChanged();
}

void WeaponPanel::createNew()
{
    QString path = QFileDialog::getSaveFileName(this, tr("New weapon"), defaultWeaponDir(),
                                                tr("Weapons (*.weapon)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".weapon";
    engine::WeaponSettings w;
    w.name = QFileInfo(path).completeBaseName().toStdString();
    // Start from the bundled repeater model so the new weapon is visible right away.
    const QString model = QDir(QCoreApplication::applicationDirPath()).filePath("assets/models/weapons/blaster-repeater.glb");
    if (QFileInfo::exists(model))
        w.modelPath = toPath(model);
    std::string error;
    if (!engine::saveWeaponFile(w, toPath(path), &error)) {
        QMessageBox::warning(this, tr("New weapon"), QString::fromStdString(error));
        return;
    }
    cache_[toPath(path)] = w;
    scene_->loadout.push_back(toPath(path));
    refresh();
    list_->setCurrentRow(list_->count() - 1);
    emit loadoutChanged();
}

void WeaponPanel::removeCurrent()
{
    const int row = list_->currentRow();
    if (row < 0)
        return;
    scene_->loadout.erase(scene_->loadout.begin() + row);
    refresh();
    emit loadoutChanged();
}

void WeaponPanel::move(int delta)
{
    const int row = list_->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= static_cast<int>(scene_->loadout.size()))
        return;
    std::swap(scene_->loadout[static_cast<std::size_t>(row)], scene_->loadout[static_cast<std::size_t>(target)]);
    refresh();
    list_->setCurrentRow(target);
    emit loadoutChanged();
}

bool WeaponPanel::saveModified(QString* error)
{
    for (auto& [path, modified] : modified_) {
        if (!modified)
            continue;
        std::string err;
        if (!engine::saveWeaponFile(cache_[path], path, &err)) {
            if (error)
                *error = QString::fromStdString(err);
            return false;
        }
        modified = false;
    }
    return true;
}
