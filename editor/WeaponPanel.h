#pragma once
#include <engine/Scene.h>
#include <engine/Weapon.h>

#include <QWidget>
#include <map>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QSpinBox;
class Vec3Editor;

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

    engine::Scene* scene_;
    std::map<std::filesystem::path, engine::WeaponSettings> cache_;
    std::map<std::filesystem::path, bool> modified_;
    bool updating_ = false;

    QListWidget* list_ = nullptr;
    QWidget* form_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* model_ = nullptr;
    QDoubleSpinBox* damage_ = nullptr;
    QDoubleSpinBox* interval_ = nullptr;
    QCheckBox* automatic_ = nullptr;
    QSpinBox* pellets_ = nullptr;
    QSpinBox* magazine_ = nullptr;
    QDoubleSpinBox* reload_ = nullptr;
    QDoubleSpinBox* spread_ = nullptr;
    QDoubleSpinBox* range_ = nullptr;
    Vec3Editor* viewOffset_ = nullptr;
    QDoubleSpinBox* viewScale_ = nullptr;
};
