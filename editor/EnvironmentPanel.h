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
    void previewed(); // time of day playback: redraw only, nothing to save

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
    void buildTimeOfDay(class QFormLayout* form);
    void refreshTimeOfDay();
    void setHourPreview(float hour); // slider label + viewport, no undo step
    void readWeatherTable();
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
    SoundPicker* thunderSound_ = nullptr;
    class QComboBox* reverb_ = nullptr;
    SoundPicker* ambience_ = nullptr;
    QComboBox* preset_ = nullptr;
    bool updating_ = false;
    // Day/night cycle (Environment::time).
    class QCheckBox* timeEnabled_ = nullptr;
    class QSlider* hourSlider_ = nullptr;
    class QLabel* hourLabel_ = nullptr;
    class QToolButton* play_ = nullptr;
    class QTimer* playTimer_ = nullptr;
    float playStartHour_ = 12.0f;
    std::vector<std::pair<QDoubleSpinBox*, float engine::TimeOfDay::*>> timeFloats_;
    class QSpinBox* dayOfYear_ = nullptr;
    class QTableWidget* weather_ = nullptr;
};
