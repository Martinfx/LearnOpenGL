#include "WeaponPanel.h"
#include "Vec3Editor.h"
#include "AssetPaths.h"
#include "SoundEditor.h"

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

#include <algorithm>
#include <set>

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
    modelRow_ = modelRow;
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
    type_->addItems({tr("Firearm (hitscan)"), tr("Melee (knife, no ammo)"), tr("Throwable (incendiary bottle)")});
    viewRotation_ = new Vec3Editor(-360.0, 360.0, 5.0, form_);
    viewRotation_->setToolTip(tr("Pitch, yaw, roll in degrees: turns the model so it points into the screen"));
    muzzle_ = new Vec3Editor(-100.0, 100.0, 0.05, form_);
    muzzle_->setToolTip(tr("Muzzle flash position in model units"));
    clipInfo_ = new QLabel(form_);
    clipInfo_->setWordWrap(true);
    const char* clipLabels[8] = {"Idle clip", "Fire / swing clip", "Reload clip", "Draw clip", "Empty reload clip", "Walk clip",
                                 "Last shot clip", "Inspect clip"};
    damageLabel_ = new QLabel(tr("Damage"), form_);
    intervalLabel_ = new QLabel(tr("Fire interval"), form_);
    spreadLabel_ = new QLabel(tr("Spread"), form_);

    auto* baseRow = new QWidget(form_);
    auto* baseLayout = new QHBoxLayout(baseRow);
    baseLayout->setContentsMargins(0, 0, 0, 0);
    base_ = new QComboBox(baseRow);
    base_->setToolTip(tr("Inherit every value from another weapon file; this file then keeps only what differs "
                         "(labels in bold). Changing the base updates all values not overridden here."));
    resetBase_ = new QPushButton(tr("Reset"), baseRow);
    resetBase_->setToolTip(tr("Drop all overrides: take every value from the base again (keeps the name)"));
    baseLayout->addWidget(base_, 1);
    baseLayout->addWidget(resetBase_);
    form->addRow(tr("Base"), baseRow);
    form->addRow(tr("Type"), type_);
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Model (glTF)"), modelRow);
    form->addRow(damageLabel_, damage_);
    form->addRow(intervalLabel_, interval_);
    form->addRow(tr("Automatic"), automatic_);
    form->addRow(tr("Pellets"), pellets_);
    form->addRow(tr("Magazine"), magazine_);
    ammoType_ = new QLineEdit(form_);
    ammoType_->setPlaceholderText(tr("endless"));
    ammoType_->setToolTip(tr("Ammo type, e.g. 5.45x39: reloads take rounds from the player's stock of it (ammo pickups add to it); "
                             "empty = endless reserve"));
    form->addRow(tr("Ammo type"), ammoType_);
    startReserve_ = new QSpinBox(form_);
    startReserve_->setRange(0, 10000);
    startReserve_->setToolTip(tr("Rounds of the ammo type the player gets with this weapon (besides the full magazine)"));
    form->addRow(tr("Start reserve"), startReserve_);
    form->addRow(tr("Reload time"), reload_);
    form->addRow(tr("Empty reload time"), reloadEmpty_);
    form->addRow(spreadLabel_, spread_);
    form->addRow(tr("Range"), range_);
    form->addRow(new QLabel(tr("<b>Trade and wear</b>"), form_));
    price_ = new QSpinBox(form_);
    price_->setRange(0, 1000000);
    price_->setSuffix(tr(" RU"));
    price_->setToolTip(tr("Base price: traders sell it for more (markup), buy it for less, worn ones cheaper"));
    form->addRow(tr("Price"), price_);
    weight_ = spin(form_, 0, 100, 0.1, 2, tr(" kg"));
    weight_->setToolTip(tr("Counts toward the player's load (assets/items.cfg: carry free / max)"));
    form->addRow(tr("Weight"), weight_);
    wear_ = spin(form_, 0, 0.1, 0.0001, 4, tr(" per shot"));
    wear_->setToolTip(tr("Condition lost per shot (1 = new). Worn weapons spread more and below 60 % they jam"));
    form->addRow(tr("Wear"), wear_);
    jam_ = spin(form_, 0, 1, 0.01, 2, QString());
    jam_->setToolTip(tr("Chance per shot to jam when completely worn (0 = never jams); R clears a jam"));
    form->addRow(tr("Jam chance"), jam_);
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
    for (int i = 0; i < 8; ++i) {
        clips_[static_cast<std::size_t>(i)] = new QComboBox(form_);
        clips_[static_cast<std::size_t>(i)]->setEditable(true);
        form->addRow(tr(clipLabels[i]), clips_[static_cast<std::size_t>(i)]);
        connect(clips_[static_cast<std::size_t>(i)], &QComboBox::currentTextChanged, this, &WeaponPanel::edited);
    }
    buildThrowRows(form);
    form->addRow(new QLabel(tr("<b>Sounds</b>"), form_));
    const char* soundLabels[] = {"Fire / swing", "Reload", "Draw", "Impact (world)", "Hit (character)",
                                 "Reload (empty mag)", "Dry fire"};
    for (std::size_t i = 0; i < sounds_.size(); ++i) {
        sounds_[i] = new SoundPicker(form_);
        form->addRow(tr(soundLabels[i]), sounds_[i]);
        connect(sounds_[i], &SoundPicker::pathChanged, this, &WeaponPanel::edited);
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
    firearmRows_[12] = ammoType_;
    firearmRows_[13] = startReserve_;
    formLayout_ = form;
    fieldKeys_ = {{type_, {"type"}}, {name_, {"name"}}, {modelRow_, {"model"}}, {damage_, {"damage"}},
                  {interval_, {"fire_interval"}}, {automatic_, {"automatic"}}, {pellets_, {"pellets"}},
                  {magazine_, {"magazine"}}, {ammoType_, {"ammo_type"}}, {startReserve_, {"ammo_type"}},
                  {reload_, {"reload_time"}}, {reloadEmpty_, {"reload_empty_time"}}, {spread_, {"spread"}},
                  {range_, {"range"}}, {price_, {"price"}}, {weight_, {"weight"}}, {wear_, {"wear"}}, {jam_, {"wear"}},
                  {viewOffset_, {"view_offset"}}, {viewRotation_, {"view_rotation"}}, {viewScale_, {"view_scale"}},
                  {muzzle_, {"muzzle"}}, {aimFov_, {"aim_fov"}}, {aimOffset_, {"aim_offset"}},
                  {aimRotation_, {"aim_rotation"}}, {aimTime_, {"aim_time"}}, {aimSpread_, {"aim_spread"}}};
    const char* clipKeys[8] = {"anim_idle", "anim_fire", "anim_reload", "anim_draw", "anim_reload_empty", "anim_walk",
                               "anim_fire_last", "anim_inspect"};
    for (std::size_t i = 0; i < clips_.size(); ++i)
        fieldKeys_.push_back({clips_[i], {clipKeys[i]}});
    const char* soundKeys[7] = {"sound_fire", "sound_reload", "sound_draw", "sound_impact", "sound_hit",
                                "sound_reload_empty", "sound_dry_fire"};
    for (std::size_t i = 0; i < sounds_.size(); ++i)
        fieldKeys_.push_back({sounds_[i], {soundKeys[i]}});
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
    for (QDoubleSpinBox* s : {damage_, interval_, reload_, reloadEmpty_, aimFov_, aimTime_, aimSpread_, spread_, range_, viewScale_, weight_, wear_, jam_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &WeaponPanel::edited);
    connect(ammoType_, &QLineEdit::editingFinished, this, &WeaponPanel::edited);
    for (QSpinBox* s : {pellets_, magazine_, startReserve_, price_})
        connect(s, &QSpinBox::valueChanged, this, &WeaponPanel::edited);
    connect(automatic_, &QCheckBox::toggled, this, &WeaponPanel::edited);
    connect(viewOffset_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(viewRotation_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(muzzle_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(aimOffset_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(aimRotation_, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
    connect(base_, &QComboBox::currentIndexChanged, this, [this] {
        auto* w = current();
        if (!w || updating_)
            return;
        w->base = base_->currentData().toString().toStdString();
        resetBase_->setEnabled(!w->base.empty());
        edited();
    });
    connect(resetBase_, &QPushButton::clicked, this, &WeaponPanel::resetToBase);
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
    type_->setCurrentIndex(w->melee() ? 1 : w->throwable() ? 2 : 0);
    {
        const engine::WeaponSettings::Thrown& t = w->thrown;
        throwSpeed_->setValue(t.speed);
        throwLift_->setValue(t.lift);
        throwRelease_->setValue(t.release);
        throwGravity_->setValue(t.gravity);
        fuse_->setValue(t.fuse);
        throwOffset_->setValue(t.handOffset);
        throwModel_->setPath(toQString(t.model));
        effectTrail_->setPath(toQString(t.effectTrail));
        effectBurst_->setPath(toQString(t.effectBurst));
        effectFire_->setPath(toQString(t.effectFire));
        soundLoop_->setPath(toQString(t.soundLoop));
        trailBone_->setText(QString::fromStdString(t.trailBone));
        trailOffset_->setValue(t.trailOffset);
        fireRadius_->setValue(t.fireRadius);
        fireDuration_->setValue(t.fireDuration);
        fireDamage_->setValue(t.fireDamage);
        burnTime_->setValue(t.burnTime);
        burnDamage_->setValue(t.burnDamage);
        spread2_->setValue(t.spread);
        maxPatches_->setValue(t.maxPatches);
        lightIntensity_->setValue(t.lightIntensity);
        lightRange_->setValue(t.lightRange);
        lightColor_->setValue(t.lightColor);
    }
    name_->setText(QString::fromStdString(w->name));
    model_->setText(w->modelPath.empty() ? QString() : toQString(w->modelPath.filename()));
    {
        const std::filesystem::path* sounds[] = {&w->soundFire, &w->soundReload, &w->soundDraw, &w->soundImpact, &w->soundHit, &w->soundReloadEmpty, &w->soundDryFire};
        for (std::size_t i = 0; i < sounds_.size(); ++i)
            sounds_[i]->setPath(toQString(*sounds[i]));
    }
    model_->setToolTip(toQString(w->modelPath));
    damage_->setValue(w->damage);
    interval_->setValue(w->fireInterval);
    automatic_->setChecked(w->automatic);
    pellets_->setValue(w->pellets);
    magazine_->setValue(w->magazineSize);
    price_->setValue(w->price);
    weight_->setValue(w->weight);
    wear_->setValue(w->wearPerShot);
    jam_->setValue(w->jamChance);
    ammoType_->setText(QString::fromStdString(w->ammoType));
    startReserve_->setValue(w->startReserve);
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
    fillBases();
    updating_ = false;
    updateTypeRows();
    markOverrides();
}

// Other .weapon files in the same folder can be the base (no chains back to this one).
void WeaponPanel::fillBases()
{
    const engine::WeaponSettings* w = current();
    if (!w)
        return;
    const auto& file = scene_->loadout[static_cast<std::size_t>(list_->currentRow())];
    base_->clear();
    base_->addItem(tr("(none)"), QString());
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::absolute(file).parent_path(), ec))
        if (entry.path().extension() == ".weapon" && !std::filesystem::equivalent(entry.path(), file, ec))
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        engine::WeaponSettings other;
        if (!engine::loadWeaponFile(other, f))
            continue;
        // Skip a file that (indirectly) inherits from this one: that would be a loop.
        bool loop = false;
        engine::WeaponSettings walk = other;
        std::filesystem::path at = f;
        for (int depth = 0; depth < 8 && !walk.base.empty(); ++depth) {
            const auto next = engine::weaponBaseFile(walk, at);
            if (std::filesystem::equivalent(next, file, ec)) {
                loop = true;
                break;
            }
            engine::WeaponSettings up;
            if (!engine::loadWeaponFile(up, next))
                break;
            walk = up;
            at = next;
        }
        if (!loop)
            base_->addItem(QString("%1  (%2)").arg(toQString(f.filename()), QString::fromStdString(other.name)),
                           toQString(f.filename()));
    }
    int index = base_->findData(QString::fromStdString(w->base));
    if (index < 0 && !w->base.empty()) {
        base_->addItem(QString::fromStdString(w->base) + tr("  (missing)"), QString::fromStdString(w->base));
        index = base_->count() - 1;
    }
    base_->setCurrentIndex(std::max(index, 0));
    resetBase_->setEnabled(!w->base.empty());
}

// Bold labels mark values this file overrides; the rest come from the base.
void WeaponPanel::markOverrides()
{
    const engine::WeaponSettings* w = current();
    std::set<std::string> overrides;
    if (w && !w->base.empty())
        overrides = engine::weaponOverrides(*w, scene_->loadout[static_cast<std::size_t>(list_->currentRow())]);
    for (const auto& [field, keys] : fieldKeys_) {
        auto* label = qobject_cast<QLabel*>(formLayout_->labelForField(field));
        if (!label)
            continue;
        bool bold = false;
        for (const auto& key : keys)
            bold = bold || overrides.count(key) > 0;
        QFont font = label->font();
        font.setBold(bold);
        label->setFont(font);
        label->setToolTip(w && !w->base.empty() ? (bold ? tr("Overridden in this file") : tr("Inherited from the base"))
                                                : QString());
    }
}

void WeaponPanel::resetToBase()
{
    auto* w = current();
    if (!w || w->base.empty())
        return;
    const auto& file = scene_->loadout[static_cast<std::size_t>(list_->currentRow())];
    engine::WeaponSettings base;
    std::string error;
    if (!engine::loadWeaponFile(base, engine::weaponBaseFile(*w, file), &error)) {
        QMessageBox::warning(this, tr("Base weapon"), QString::fromStdString(error));
        return;
    }
    base.name = w->name;
    base.base = w->base;
    *w = base;
    modified_[file] = true;
    showCurrent();
    emit loadoutChanged();
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
    const std::string* values[8] = {&w->animIdle, &w->animFire, &w->animReload, &w->animDraw, &w->animReloadEmpty, &w->animWalk,
                                    &w->animFireLast, &w->animInspect};
    for (std::size_t i = 0; i < 8; ++i) {
        QComboBox* c = clips_[i];
        c->clear();
        c->addItem(QString());
        c->addItems(names);
        c->setCurrentText(QString::fromStdString(*values[i]));
    }
    updating_ = wasUpdating;
}

// Melee weapons have no ammo: hide the firearm-only rows and rename the shared ones.
// Throwables keep the ammo type (their stock) and show their own rows.
void WeaponPanel::updateTypeRows()
{
    const bool melee = type_->currentIndex() == 1;
    const bool throwable = type_->currentIndex() == 2;
    for (QWidget* row : firearmRows_)
        formLayout_->setRowVisible(row, !melee && (!throwable || row == ammoType_ || row == startReserve_));
    for (QWidget* row : throwRows_)
        formLayout_->setRowVisible(row, throwable);
    throwLabel_->setVisible(throwable);
    fireLabel_->setVisible(throwable);
    aimLabel_->setVisible(!melee && !throwable);
    formLayout_->setRowVisible(spread_, !throwable);
    formLayout_->setRowVisible(range_, !throwable);
    if (throwable) {
        damageLabel_->setText(tr("Direct hit damage"));
        damage_->setSuffix(QString());
        intervalLabel_->setText(tr("Throw time"));
        automatic_->setText(tr("Hold to keep throwing"));
        startReserve_->setToolTip(tr("How many the player gets with this weapon (bottles, grenades)"));
        return;
    }
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
    {
        std::filesystem::path* sounds[] = {&w->soundFire, &w->soundReload, &w->soundDraw, &w->soundImpact, &w->soundHit, &w->soundReloadEmpty, &w->soundDryFire};
        for (std::size_t i = 0; i < sounds_.size(); ++i)
            *sounds[i] = toPath(sounds_[i]->path());
    }
    w->damage = static_cast<float>(damage_->value());
    w->fireInterval = static_cast<float>(interval_->value());
    w->automatic = automatic_->isChecked();
    w->pellets = pellets_->value();
    w->magazineSize = magazine_->value();
    w->price = price_->value();
    w->weight = static_cast<float>(weight_->value());
    w->wearPerShot = static_cast<float>(wear_->value());
    w->jamChance = static_cast<float>(jam_->value());
    w->ammoType = ammoType_->text().trimmed().toStdString();
    w->startReserve = startReserve_->value();
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
    w->type = type_->currentIndex() == 1   ? engine::WeaponSettings::Type::Melee
              : type_->currentIndex() == 2 ? engine::WeaponSettings::Type::Throwable
                                           : engine::WeaponSettings::Type::Firearm;
    {
        engine::WeaponSettings::Thrown& t = w->thrown;
        t.speed = static_cast<float>(throwSpeed_->value());
        t.lift = static_cast<float>(throwLift_->value());
        t.release = static_cast<float>(throwRelease_->value());
        t.gravity = static_cast<float>(throwGravity_->value());
        t.fuse = static_cast<float>(fuse_->value());
        t.handOffset = throwOffset_->value();
        t.model = toPath(throwModel_->path());
        t.effectTrail = toPath(effectTrail_->path());
        t.effectBurst = toPath(effectBurst_->path());
        t.effectFire = toPath(effectFire_->path());
        t.soundLoop = toPath(soundLoop_->path());
        t.trailBone = trailBone_->text().trimmed().toStdString();
        t.trailOffset = trailOffset_->value();
        t.fireRadius = static_cast<float>(fireRadius_->value());
        t.fireDuration = static_cast<float>(fireDuration_->value());
        t.fireDamage = static_cast<float>(fireDamage_->value());
        t.burnTime = static_cast<float>(burnTime_->value());
        t.burnDamage = static_cast<float>(burnDamage_->value());
        t.spread = static_cast<float>(spread2_->value());
        t.maxPatches = maxPatches_->value();
        t.lightIntensity = static_cast<float>(lightIntensity_->value());
        t.lightRange = static_cast<float>(lightRange_->value());
        t.lightColor = lightColor_->value();
    }
    w->viewRotation = viewRotation_->value();
    w->muzzle = muzzle_->value();
    w->animIdle = clips_[0]->currentText().toStdString();
    w->animFire = clips_[1]->currentText().toStdString();
    w->animReload = clips_[2]->currentText().toStdString();
    w->animDraw = clips_[3]->currentText().toStdString();
    w->animReloadEmpty = clips_[4]->currentText().toStdString();
    w->animWalk = clips_[5]->currentText().toStdString();
    w->animFireLast = clips_[6]->currentText().toStdString();
    w->animInspect = clips_[7]->currentText().toStdString();
    modified_[scene_->loadout[static_cast<std::size_t>(list_->currentRow())]] = true;
    list_->currentItem()->setText(QString("%1  %2").arg(list_->currentRow() + 1).arg(name_->text()));
    markOverrides();
    emit loadoutChanged();
}

// Rows of a throwable: how it flies, what it looks like, the fire it starts.
void WeaponPanel::buildThrowRows(QFormLayout* form)
{
    auto row = [&](const QString& label, QWidget* w, const QString& tip, std::vector<std::string> keys) {
        w->setToolTip(tip);
        form->addRow(label, w);
        throwRows_.push_back(w);
        fieldKeys_.push_back({w, std::move(keys)});
    };
    auto file = [&](const QString& filter, const QString& folder, bool preview) {
        auto* p = new SoundPicker(form_);
        p->setFileKind(filter, folder, preview);
        connect(p, &SoundPicker::pathChanged, this, &WeaponPanel::edited);
        return p;
    };
    auto number = [&](double min, double max, double step, int decimals, const QString& suffix) {
        QDoubleSpinBox* s = spin(form_, min, max, step, decimals, suffix);
        connect(s, &QDoubleSpinBox::valueChanged, this, &WeaponPanel::edited);
        return s;
    };
    auto vec = [&](double min, double max, double step) {
        auto* v = new Vec3Editor(min, max, step, form_);
        connect(v, &Vec3Editor::valueEdited, this, &WeaponPanel::edited);
        return v;
    };
    throwLabel_ = new QLabel(tr("<b>Throw</b>"), form_);
    form->addRow(throwLabel_);
    throwSpeed_ = number(1, 60, 0.5, 1, tr(" m/s"));
    row(tr("Throw speed"), throwSpeed_, tr("Speed when it leaves the hand (plus some of the player's own)"), {"throw"});
    throwLift_ = number(-30, 60, 1, 0, tr(" °"));
    row(tr("Throw lift"), throwLift_, tr("Thrown this much above the view direction (an arc)"), {"throw"});
    throwRelease_ = number(0, 5, 0.01, 2, tr(" s"));
    row(tr("Release"), throwRelease_, tr("Seconds from the trigger until it leaves the hand: match the throw clip"), {"throw"});
    throwGravity_ = number(0, 5, 0.1, 2, tr(" ×"));
    row(tr("Gravity"), throwGravity_, tr("Multiples of 9.81 m/s² pulling it down in flight"), {"throw"});
    fuse_ = number(0, 30, 0.1, 1, tr(" s"));
    fuse_->setSpecialValueText(tr("breaks on impact"));
    row(tr("Fuse"), fuse_, tr("Goes off by itself after this time; 0 = breaks where it hits (a bottle)"), {"fuse"});
    throwOffset_ = vec(-2.0, 2.0, 0.01);
    row(tr("Release point"), throwOffset_, tr("Where it leaves the hand in view space (m), when the view model has no trail bone"), {"throw_offset"});
    throwModel_ = file(tr("glTF 2.0 (*.gltf *.glb)"), QStringLiteral("models"), false);
    row(tr("Flying model"), throwModel_, tr("The object in flight (tumbles about its origin)"), {"throw_model"});
    trailBone_ = new QLineEdit(form_);
    trailBone_->setPlaceholderText(tr("none"));
    connect(trailBone_, &QLineEdit::editingFinished, this, &WeaponPanel::edited);
    row(tr("Trail bone"), trailBone_, tr("View model node where the trail effect burns in first person (the rag); "
                                          "a clip scaling it to nothing hides it after the release"), {"trail_bone"});
    effectTrail_ = file(tr("Particle effects (*.particle)"), QStringLiteral("particles"), false);
    row(tr("Trail effect"), effectTrail_, tr("Particles on it in hand and in flight (the burning rag)"), {"effect_trail"});
    trailOffset_ = vec(-1.0, 1.0, 0.01);
    row(tr("Trail offset"), trailOffset_, tr("Trail position on the flying model (model space, m)"), {"trail_offset"});
    effectBurst_ = file(tr("Particle effects (*.particle)"), QStringLiteral("particles"), false);
    row(tr("Break effect"), effectBurst_, tr("Where it breaks: glass and a flash of fuel (the Impact sound plays with it)"), {"effect_burst"});
    fireLabel_ = new QLabel(tr("<b>Fire</b> (burning fuel on the ground)"), form_);
    form->addRow(fireLabel_);
    effectFire_ = file(tr("Particle effects (*.particle)"), QStringLiteral("particles"), false);
    row(tr("Fire effect"), effectFire_, tr("One burning metre of ground (loops while it burns)"), {"effect_fire"});
    soundLoop_ = new SoundPicker(form_);
    connect(soundLoop_, &SoundPicker::pathChanged, this, &WeaponPanel::edited);
    row(tr("Fire sound"), soundLoop_, tr("The fire's roar (a loop), louder as the fire grows"), {"sound_fire_loop"});
    fireRadius_ = number(0, 10, 0.1, 1, tr(" m"));
    row(tr("Splash radius"), fireRadius_, tr("The pool of burning fuel where it breaks"), {"fire"});
    fireDuration_ = number(1, 120, 1, 0, tr(" s"));
    row(tr("Burn time"), fireDuration_, tr("How long the fuel burns (thinner at the rim; grass a third of it; rain shortens it)"), {"fire"});
    fireDamage_ = number(0, 500, 1, 0, tr(" hp/s"));
    row(tr("Fire damage"), fireDamage_, tr("To the player and characters standing in the flames"), {"fire"});
    burnTime_ = number(0, 30, 0.5, 1, tr(" s"));
    row(tr("Keeps burning"), burnTime_, tr("Seconds a character keeps burning after leaving the fire (water puts it out)"), {"burn"});
    burnDamage_ = number(0, 200, 1, 0, tr(" hp/s"));
    row(tr("Burning damage"), burnDamage_, tr("Damage per second while burning outside the fire"), {"burn"});
    spread2_ = number(0, 5, 0.1, 2, tr(" ×"));
    spread2_->setSpecialValueText(tr("does not spread"));
    row(tr("Spread in grass"), spread2_, tr("How readily the fire runs on through dry grass and weeds (more downwind, hardly in rain)"), {"fire_spread"});
    maxPatches_ = new QSpinBox(form_);
    maxPatches_->setRange(1, 400);
    maxPatches_->setSuffix(tr(" m²"));
    connect(maxPatches_, &QSpinBox::valueChanged, this, &WeaponPanel::edited);
    row(tr("Largest fire"), maxPatches_, tr("Most ground one throw can set alight, pool and grass together (performance)"), {"fire_spread"});
    lightIntensity_ = number(0, 100, 0.5, 1, QString());
    row(tr("Light"), lightIntensity_, tr("Brightness of the fire's flickering light"), {"fire_light"});
    lightRange_ = number(0, 60, 0.5, 1, tr(" m"));
    row(tr("Light range"), lightRange_, tr("Reach of the fire's light (grows with the fire)"), {"fire_light"});
    lightColor_ = vec(0.0, 1.0, 0.05);
    row(tr("Light colour"), lightColor_, tr("sRGB colour of the fire's light"), {"fire_light"});
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
    // Bases first: a variant's file is written as its differences to the base on disk.
    for (int pass = 0; pass < 2; ++pass)
    for (auto& [path, modified] : modified_) {
        if (!modified || cache_[path].base.empty() != (pass == 0))
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
