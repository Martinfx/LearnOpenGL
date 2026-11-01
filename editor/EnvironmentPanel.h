#pragma once
#include <engine/Scene.h>

#include <QWidget>
#include <functional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class SoundPicker;
class Vec3Editor;

// Level atmosphere: sun, sky, fog and post-processing (Scene::environment).
// Changes show in the viewport at once and are saved with the level.
class EnvironmentPanel : public QWidget {
    Q_OBJECT
public:
    explicit EnvironmentPanel(engine::Scene* scene, QWidget* parent = nullptr);
    void refresh(); // after the scene changed (open, undo)

signals:
    void edited();

private:
    struct FloatField {
        QDoubleSpinBox* spin;
        float engine::Environment::*member;
    };
    struct ColorField {
        QPushButton* button;
        glm::vec3 engine::Environment::*member;
    };
    QDoubleSpinBox* addFloat(class QFormLayout* form, const QString& label, float engine::Environment::*member, double min,
                             double max, double step, int decimals, const QString& tip = {});
    void addColor(class QFormLayout* form, const QString& label, glm::vec3 engine::Environment::*member);
    void addVec3(class QFormLayout* form, const QString& label, glm::vec3 engine::Environment::*member, double min, double max,
                 double step, const QString& tip);
    void exportNeutralLut();
    void applyPreset(int index);
    void changed();

    engine::Scene* scene_;
    std::vector<FloatField> floats_;
    std::vector<ColorField> colors_;
    std::vector<std::pair<Vec3Editor*, glm::vec3 engine::Environment::*>> vectors_;
    class QLineEdit* lutEdit_ = nullptr;
    Vec3Editor* sun_ = nullptr;
    SoundPicker* rainSound_ = nullptr;
    SoundPicker* windSound_ = nullptr;
    QComboBox* preset_ = nullptr;
    bool updating_ = false;
};
