#include "WeaponPanel.h"
#include "Vec3Editor.h"
#include "AssetPaths.h"

#include <QCheckBox>
#include <QComboBox>
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
    return assetPath("weapons");
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
    reloadEmpty_ = spin(form_, 0, 10, 0.1, 2, tr(" s"));
    reloadEmpty_->setSpecialValueText(tr("same as reload"));
    reloadEmpty_->setToolTip(tr("Reload time from an empty magazine (plays the empty-reload clip)"));
    spread_ = spin(form_, 0, 45, 0.1, 2, tr(" °"));
    range_ = spin(form_, 0.5, 5000, 0.5, 1, tr(" m"));
    viewOffset_ = new Vec3Editor(-2.0, 2.0, 0.01, form_);
    viewScale_ = spin(form_, 0.001, 10, 0.01, 3, QString());

    type_ = new QComboBox(form_);
    type_->addItems({tr("Firearm (hitscan)"), tr("Melee (knife, no ammo)")});
    viewRotation_ = new Vec3Editor(-360.0, 360.0, 5.0, form_);
    viewRotation_->setToolTip(tr("Pitch, yaw, roll in degrees: turns the model so it points into the screen"));
    muzzle_ = new Vec3Editor(-100.0, 100.0, 0.05, form_);
    muzzle_->setToolTip(tr("Muzzle flash position in model units"));
    clipInfo_ = new QLabel(form_);
    clipInfo_->setWordWrap(true);
    const char* clipLabels[6] = {"Idle clip", "Fire / swing clip", "Reload clip", "Draw clip", "Empty reload clip", "Walk clip"};
    damageLabel_ = new QLabel(tr("Damage"), form_);
    intervalLabel_ = new QLabel(tr("Fire interval"), form_);
    spreadLabel_ = new QLabel(tr("Spread"), form_);

    form->addRow(tr("Type"), type_);
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Model (glTF)"), modelRow);
    form->addRow(damageLabel_, damage_);
    form->addRow(intervalLabel_, interval_);
    form->addRow(tr("Automatic"), automatic_);
    form->addRow(tr("Pellets"), pellets_);
    form->addRow(tr("Magazine"), magazine_);
    form->addRow(tr("Reload time"), reload_);
    form->addRow(tr("Empty reload time"), reloadEmpty_);
    form->addRow(spreadLabel_, spread_);
    form->addRow(tr("Range"), range_);
    form->addRow(new QLabel(tr("<b>First-person view</b>"), form_));
    form->addRow(tr("View offset"), viewOffset_);
    form->addRow(tr("View rotation"), viewRotation_);
    form->addRow(tr("View scale"), viewScale_);
    form->addRow(tr("Muzzle"), muzzle_);
    aimLabel_ = new QLabel(tr("<b>Aiming down the sights</b> (right mouse)"), form_);
    form->addRow(aimLabel_);
    aimFov_ = spin(form_, 0, 120, 1, 0, tr(" °"));
    aimFov_->setSpecialValueText(tr("cannot aim"));
    aimFov_->setToolTip(tr("Camera field of view while aiming (zoom); 0 = the weapon cannot aim"));
    aimOffset_ = new Vec3Editor(-2.0, 2.0, 0.002, form_);
    aimOffset_->setToolTip(tr("View offset while aiming: move it until the sights sit on the screen center"));
    aimRotation_ = new Vec3Editor(-90.0, 90.0, 0.5, form_);
    aimRotation_->setToolTip(tr("Extra pitch, yaw, roll while aiming, to line up front and rear sight"));
    aimTime_ = spin(form_, 0.01, 2, 0.01, 2, tr(" s"));
    aimSpread_ = spin(form_, 0, 2, 0.05, 2, tr(" ×"));
    aimSpread_->setToolTip(tr("Spread multiplier while aiming"));
    form->addRow(tr("Zoom (FOV)"), aimFov_);
    form->addRow(tr("Aim offset"), aimOffset_);
    form->addRow(tr("Aim rotation"), aimRotation_);
    form->addRow(tr("Raise time"), aimTime_);
    form->addRow(tr("Aimed spread"), aimSpread_);
    form->addRow(new QLabel(tr("<b>Arms / weapon animation</b>"), form_));
    form->addRow(QString(), clipInfo_);
    for (int i = 0; i < 6; ++i) {
        clips_[static_cast<std::size_t>(i)] = new QComboBox(form_);
        clips_[static_cast<std::size_t>(i)]->setEditable(true);
        form->addRow(tr(clipLabels[i]), clips_[static_cast<std::size_t>(i)]);
        connect(clips_[static_cast<std::size_t>(i)], &QComboBox::currentTextChanged, this, &WeaponPanel::edited);
    }
    firearmRows_[0] = pellets_;
    firearmRows_[1] = magazine_;
    firearmRows_[2] = reload_;
    firearmRows_[3] = muzzle_;
    firearmRows_[4] = clips_[2];
    firearmRows_[5] = clips_[4];
    firearmRows_[6] = reloadEmpty_;
    firearmRows_[7] = aimFov_;
    firearmRows_[8] = aimOffset_;
    firearmRows_[9] = aimRotation_;
    firearmRows_[10] = aimTime_;
    firearmRows_[11] = aimSpread_;
    formLayout_ = form;
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
        fillClipNames();
    });
    connect(name_, &QLineEdit::editingFinished, this, &WeaponPanel::edited);
    for (QDoubleSpinBox* s : {damage_, interval_, reload_, reloadEmpty_, aimFov_, aimTime_, aimSpread_, spread_, range_, viewScale_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &WeaponPanel::edited);
    for (QSpinBox* s : {pellets_, magazine_})
        connect(s, &QSpinBox::valueChanged, this, &WeaponPanel::edited);
    connect(automatic_, &QCheckBox::toggled, this, &WeaponPanel::edited);
    connect(viewOffset_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(viewRotation_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(muzzle_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(aimOffset_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(aimRotation_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        edited();
        updateTypeRows();
    });

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
    type_->setCurrentIndex(w->melee() ? 1 : 0);
    name_->setText(QString::fromStdString(w->name));
    model_->setText(w->modelPath.empty() ? QString() : toQString(w->modelPath.filename()));
    model_->setToolTip(toQString(w->modelPath));
    damage_->setValue(w->damage);
    interval_->setValue(w->fireInterval);
    automatic_->setChecked(w->automatic);
    pellets_->setValue(w->pellets);
    magazine_->setValue(w->magazineSize);
    reload_->setValue(w->reloadTime);
    reloadEmpty_->setValue(w->reloadEmptyTime);
    aimFov_->setValue(w->aimFov);
    aimOffset_->setValue(w->aimOffset);
    aimRotation_->setValue(w->aimRotation);
    aimTime_->setValue(w->aimTime);
    aimSpread_->setValue(w->aimSpread);
    spread_->setValue(w->spreadDegrees);
    range_->setValue(w->range);
    viewOffset_->setValue(w->viewOffset);
    viewScale_->setValue(w->viewScale);
    viewRotation_->setValue(w->viewRotation);
    muzzle_->setValue(w->muzzle);
    fillClipNames();
    updating_ = false;
    updateTypeRows();
}

// Clip combos list the animations of the view model (e.g. arms holding the weapon).
void WeaponPanel::fillClipNames()
{
    const engine::WeaponSettings* w = current();
    if (!w)
        return;
    const bool wasUpdating = updating_;
    updating_ = true;
    QStringList names;
    if (!w->modelPath.empty()) {
        std::string error;
        if (auto model = engine::loadCharacterModel(w->modelPath, &error); model && model->skeleton)
            for (const auto& clip : model->skeleton->clips)
                names << QString::fromStdString(clip.name);
    }
    clipInfo_->setText(names.isEmpty()
                           ? tr("The model has no animations. Use a glTF with arms and clips to animate firing, "
                                "reloading and drawing; otherwise the weapon is moved procedurally.")
                           : tr("%n clip(s) in the model. Reload and swing clips are stretched to their times.", nullptr,
                                static_cast<int>(names.size())));
    const std::string* values[6] = {&w->animIdle, &w->animFire, &w->animReload, &w->animDraw, &w->animReloadEmpty, &w->animWalk};
    for (std::size_t i = 0; i < 6; ++i) {
        QComboBox* c = clips_[i];
        c->clear();
        c->addItem(QString());
        c->addItems(names);
        c->setCurrentText(QString::fromStdString(*values[i]));
    }
    updating_ = wasUpdating;
}

// Melee weapons have no ammo: hide the firearm-only rows and rename the shared ones.
void WeaponPanel::updateTypeRows()
{
    const bool melee = type_->currentIndex() == 1;
    for (QWidget* row : firearmRows_)
        formLayout_->setRowVisible(row, !melee);
    aimLabel_->setVisible(!melee);
    damageLabel_->setText(melee ? tr("Damage per hit") : tr("Damage"));
    damage_->setSuffix(melee ? QString() : tr(" per pellet"));
    intervalLabel_->setText(melee ? tr("Swing time") : tr("Fire interval"));
    spreadLabel_->setText(melee ? tr("Swing arc") : tr("Spread"));
    automatic_->setText(melee ? tr("Hold to keep swinging") : tr("Hold to fire"));
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
    w->reloadEmptyTime = static_cast<float>(reloadEmpty_->value());
    w->aimFov = static_cast<float>(aimFov_->value());
    w->aimOffset = aimOffset_->value();
    w->aimRotation = aimRotation_->value();
    w->aimTime = static_cast<float>(aimTime_->value());
    w->aimSpread = static_cast<float>(aimSpread_->value());
    w->spreadDegrees = static_cast<float>(spread_->value());
    w->range = static_cast<float>(range_->value());
    w->viewOffset = viewOffset_->value();
    w->viewScale = static_cast<float>(viewScale_->value());
    w->type = type_->currentIndex() == 1 ? engine::WeaponSettings::Type::Melee : engine::WeaponSettings::Type::Firearm;
    w->viewRotation = viewRotation_->value();
    w->muzzle = muzzle_->value();
    w->animIdle = clips_[0]->currentText().toStdString();
    w->animFire = clips_[1]->currentText().toStdString();
    w->animReload = clips_[2]->currentText().toStdString();
    w->animDraw = clips_[3]->currentText().toStdString();
    w->animReloadEmpty = clips_[4]->currentText().toStdString();
    w->animWalk = clips_[5]->currentText().toStdString();
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
    // Start from the AK-74M arms so the new weapon is visible right away.
    const QString model = assetPath("models/weapons/fps/fp_ak74.glb");
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
