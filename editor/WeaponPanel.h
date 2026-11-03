#pragma once
#include <engine/Scene.h>
#include <engine/Weapon.h>

#include <QWidget>
#include <array>
#include <map>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class Vec3Editor;
class SoundPicker;

// Player loadout of the level + editor for the selected .weapon file.
class WeaponPanel : public QWidget {
    Q_OBJECT
public:
    explicit WeaponPanel(engine::Scene* scene, QWidget* parent = nullptr);

    void refresh();                 // after the scene was replaced (new/open)
    bool saveModified(QString* error); // writes edited .weapon files

signals:
    void loadoutChanged();          // the level changed (needs saving)

private:
    engine::WeaponSettings* current();
    void showCurrent();
    void edited();
    void addExisting();
    void createNew();
    void removeCurrent();
    void move(int delta);
    void updateTypeRows();
    void fillClipNames();
    void fillBases();
    void markOverrides();
    void resetToBase();

    engine::Scene* scene_;
    std::map<std::filesystem::path, engine::WeaponSettings> cache_;
    std::map<std::filesystem::path, bool> modified_;
    bool updating_ = false;

    QListWidget* list_ = nullptr;
    QWidget* form_ = nullptr;
    QComboBox* type_ = nullptr;
    QComboBox* base_ = nullptr;      // parent .weapon file (inheritance)
    QPushButton* resetBase_ = nullptr;
    QWidget* modelRow_ = nullptr;
    std::vector<std::pair<QWidget*, std::vector<std::string>>> fieldKeys_; // field -> keys in the .weapon file
    QLineEdit* name_ = nullptr;
    QLineEdit* model_ = nullptr;
    QDoubleSpinBox* damage_ = nullptr;
    QDoubleSpinBox* interval_ = nullptr;
    QCheckBox* automatic_ = nullptr;
    QSpinBox* pellets_ = nullptr;
    QSpinBox* magazine_ = nullptr;
    QLineEdit* ammoType_ = nullptr;   // empty = endless reserve
    QSpinBox* startReserve_ = nullptr;
    QDoubleSpinBox* reload_ = nullptr;
    QDoubleSpinBox* reloadEmpty_ = nullptr;
    QLabel* aimLabel_ = nullptr;
    Vec3Editor* aimOffset_ = nullptr;
    Vec3Editor* aimRotation_ = nullptr;
    QDoubleSpinBox* aimFov_ = nullptr;
    QDoubleSpinBox* aimTime_ = nullptr;
    QDoubleSpinBox* aimSpread_ = nullptr;
    QDoubleSpinBox* spread_ = nullptr;
    QDoubleSpinBox* range_ = nullptr;
    QSpinBox* price_ = nullptr;           // trade (assets/items.cfg prices the rest)
    QDoubleSpinBox* weight_ = nullptr;    // kg, inventory load
    QDoubleSpinBox* wear_ = nullptr;      // condition lost per shot
    QDoubleSpinBox* jam_ = nullptr;       // jam chance per shot when wrecked
    Vec3Editor* viewOffset_ = nullptr;
    QDoubleSpinBox* viewScale_ = nullptr;
    Vec3Editor* viewRotation_ = nullptr;
    Vec3Editor* muzzle_ = nullptr;
    std::array<SoundPicker*, 7> sounds_{}; // fire, reload, draw, impact, hit, reload empty, dry fire
    std::array<QComboBox*, 8> clips_{};  // idle, fire, reload, draw, reload empty, walk, last shot, inspect
    QLabel* clipInfo_ = nullptr;
    std::array<QWidget*, 14> firearmRows_{}; // rows hidden for melee weapons
    QFormLayout* formLayout_ = nullptr;
    QLabel* intervalLabel_ = nullptr;
    QLabel* spreadLabel_ = nullptr;
    QLabel* damageLabel_ = nullptr;
};
