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
    // Weather cycle (Environment::weather): kinds of weather changing over time.
    void buildWeatherCycle(class QFormLayout* form);
    void refreshWeatherCycle();
    void readCycleTable();
    void previewKind(int index); // -1 = off; viewport only, nothing to save
    // Anomaly storm (Environment::anomalyStorm): the Zone's storm that sends everyone under a roof.
    void buildAnomalyStorm(class QFormLayout* form);
    void refreshAnomalyStorm();
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
    class QCheckBox* cycleEnabled_ = nullptr;
    std::vector<std::pair<QDoubleSpinBox*, float engine::WeatherCycle::*>> cycleFloats_;
    QComboBox* cycleStart_ = nullptr;
    QComboBox* cyclePreview_ = nullptr;
    class QTableWidget* kinds_ = nullptr;
    class QCheckBox* stormEnabled_ = nullptr;
    std::vector<std::pair<QDoubleSpinBox*, float engine::AnomalyStormSettings::*>> stormFloats_;
    class QPushButton* stormTint_ = nullptr;
    class QSlider* stormPreview_ = nullptr;
};
