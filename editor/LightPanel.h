#pragma once
#include <engine/Lights.h>
#include <engine/Scene.h>

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;

// Details of a light entity (type "light"): point or spot, brightness, range,
// cone, shadows and flicker (engine/Lights.h). The color is the entity color
// (General > Color), the position the entity position and a spot light points
// along the entity's yaw, tilted by Pitch. Every value is an entity property.
class LightPanel : public QWidget {
    Q_OBJECT
public:
    explicit LightPanel(QWidget* parent = nullptr);
    void setEntity(engine::Entity* entity); // null = none

signals:
    void edited(); // a value changed (undo step, dirty, viewport refresh)

private:
    void refresh();
    void apply();

    engine::Entity* entity_ = nullptr;
    QComboBox* type_ = nullptr;
    QDoubleSpinBox* intensity_ = nullptr;
    QDoubleSpinBox* range_ = nullptr;
    QDoubleSpinBox* inner_ = nullptr;
    QDoubleSpinBox* outer_ = nullptr;
    QDoubleSpinBox* pitch_ = nullptr;
    QCheckBox* shadows_ = nullptr;
    QCheckBox* cookie_ = nullptr;
    QComboBox* flicker_ = nullptr;
    QDoubleSpinBox* flickerSpeed_ = nullptr;
    QCheckBox* enabled_ = nullptr;
    QFormLayout* form_ = nullptr;
    bool updating_ = false;
};
