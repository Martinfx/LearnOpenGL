#pragma once
#include "Viewport.h"

#include <QFrame>
#include <QWidget>
#include <glm/vec3.hpp>

class QCheckBox;
class QComboBox;
class QLabel;
class QToolBar;
class QToolButton;
class Vec3Editor;

// Floating box over the 3D view with the selection's location, rotation and
// scale. In Delta Mode the fields start at zero (scale at one) and what you type
// is added to (multiplied with) the current value, then the field resets.
class TransformBox : public QFrame {
    Q_OBJECT
public:
    enum Field { Location, Rotation, Scale };
    explicit TransformBox(QWidget* parent = nullptr);

    // Values of the selection; ignored in Delta Mode. `scaleLabel` is "Scale" or "Size".
    void setValues(glm::vec3 location, glm::vec3 rotation, glm::vec3 scale, const QString& scaleLabel,
                   bool canRotate, bool canScale);
    bool deltaMode() const;

signals:
    void edited(int field, glm::vec3 value, bool delta);

private:
    void resetDeltas();
    Vec3Editor* fields_[3];
    QLabel* scaleLabel_;
    QCheckBox* delta_;
    QWidget* body_;
};

// The 3D view with its own tool bar on top (view, shading, transform tools,
// snapping, camera speed) and the floating transform box.
class ViewportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ViewportPanel(engine::Scene* scene, QWidget* parent = nullptr);

    Viewport* viewport() const { return viewport_; }
    TransformBox* transformBox() const { return transform_; }
    void setView(int index);      // 0 perspective, 1 top, 2 front, 3 side, 4 four views
    void setShading(int index);   // engine::Shading
    QMenu* optionsMenu() const { return options_; }

signals:
    void viewChanged(int index);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void applySnap();
    void placeOverlay();

    Viewport* viewport_;
    QToolBar* bar_;
    QComboBox* viewCombo_;
    QComboBox* shadingCombo_;
    QToolButton* toolButtons_[4];
    QToolButton* gridOn_;
    QToolButton* angleOn_;
    QToolButton* scaleOn_;
    QComboBox* grid_;
    QComboBox* angle_;
    QComboBox* scale_;
    QToolButton* speed_;
    QMenu* options_;
    TransformBox* transform_;
};
