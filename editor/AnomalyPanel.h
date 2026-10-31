#pragma once
#include <engine/Scene.h>

#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QWidget;

// Details of an anomaly entity (type "anomaly"): its kind, the kind's parameters
// (engine/Anomaly.h), the rift link and optional effect/sound overrides. Every
// value is an entity property saved with the level. The field is the entity box
// (Transform > Size): radius = size X / 2, height = size Y.
class AnomalyPanel : public QWidget {
    Q_OBJECT
public:
    // `assetRoot` turns picked files into paths relative to assets/.
    AnomalyPanel(std::function<QString()> assetRoot, QWidget* parent = nullptr);

    // The entity shown (null = none). `scene` lists the rift link targets.
    void setEntity(engine::Entity* entity, const engine::Scene* scene);

signals:
    void edited();        // a value changed (undo step, dirty)
    void kindChanged();   // size and color were reset too: refresh the other fields

private:
    void rebuild();
    void setProp(const std::string& key, const std::string& value);

    std::function<QString()> assetRoot_;
    engine::Entity* entity_ = nullptr;
    const engine::Scene* scene_ = nullptr;
    QComboBox* kind_ = nullptr;
    QLabel* description_ = nullptr;
    QWidget* params_ = nullptr;
    QFormLayout* paramsForm_ = nullptr;
    QComboBox* link_ = nullptr;
    QWidget* linkRow_ = nullptr;
    QLineEdit* overrides_[4] = {};
    bool updating_ = false;
    bool inEdit_ = false;
};
