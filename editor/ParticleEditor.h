#pragma once
#include <engine/Particles.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QTimer>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

// Live preview of a particle effect on a small ground plane (drag to orbit, wheel to zoom).
class ParticlePreview : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ParticlePreview(QWidget* parent = nullptr);
    ~ParticlePreview() override;
    void setEffect(std::shared_ptr<engine::ParticleEffect> effect);
    void restart();
    int particleCount() const { return particles_.particleCount(); }

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(class QMouseEvent* e) override;
    void mouseMoveEvent(class QMouseEvent* e) override;
    void wheelEvent(class QWheelEvent* e) override;

private:
    void cleanup();

    engine::Renderer renderer_;
    engine::Scene scene_;
    engine::ParticleSystem particles_;
    std::shared_ptr<engine::ParticleEffect> effect_;
    engine::ParticleSystem::Handle handle_ = 0;
    bool ready_ = false;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 last_ = 0;
    float idle_ = 0.0f; // seconds the effect has been finished (one-shot effects replay)
    float yaw_ = 0.6f, pitch_ = 0.35f, distance_ = 4.0f;
    QPoint dragFrom_;
};

// Curve of a value over the particle's life (size): drag points, double-click adds, right-click removes.
class CurveWidget : public QWidget {
    Q_OBJECT
public:
    explicit CurveWidget(QWidget* parent = nullptr);
    void setKeys(const std::vector<engine::ParticleKey>& keys); // no signal
    const std::vector<engine::ParticleKey>& keys() const { return keys_; }

signals:
    void edited();

protected:
    void paintEvent(class QPaintEvent* e) override;
    void mousePressEvent(class QMouseEvent* e) override;
    void mouseMoveEvent(class QMouseEvent* e) override;
    void mouseReleaseEvent(class QMouseEvent* e) override;
    void mouseDoubleClickEvent(class QMouseEvent* e) override;

private:
    QPointF toScreen(float t, float v) const;
    std::pair<float, float> fromScreen(QPointF p) const;
    int keyAt(QPointF p) const;
    float maxValue() const;

    std::vector<engine::ParticleKey> keys_;
    int dragging_ = -1;
    float scale_ = 3.0f; // top of the graph, kept while dragging
};

// Color and opacity over the particle's life: click adds a key, drag moves it,
// double-click picks its color (with alpha), right-click removes it.
class GradientWidget : public QWidget {
    Q_OBJECT
public:
    explicit GradientWidget(QWidget* parent = nullptr);
    void setKeys(const std::vector<engine::ParticleKey>& keys);
    const std::vector<engine::ParticleKey>& keys() const { return keys_; }

signals:
    void edited();

protected:
    void paintEvent(class QPaintEvent* e) override;
    void mousePressEvent(class QMouseEvent* e) override;
    void mouseMoveEvent(class QMouseEvent* e) override;
    void mouseReleaseEvent(class QMouseEvent* e) override;
    void mouseDoubleClickEvent(class QMouseEvent* e) override;

private:
    int keyAt(QPointF p) const;
    float tAt(QPointF p) const;

    std::vector<engine::ParticleKey> keys_;
    int dragging_ = -1;
};

// Editor tab for a .particle file: emitters on the left, the preview in the
// middle, the selected emitter's settings on the right. Edits are live (also in
// the level view, which shares the effect); Save writes the file.
class ParticleEditor : public QWidget {
    Q_OBJECT
public:
    explicit ParticleEditor(QWidget* parent = nullptr);
    bool load(const QString& path, QString* error);
    bool save(QString* error);
    bool revert(QString* error);
    QString path() const { return path_; }
    QString title() const;
    bool isModified() const { return modified_; }

signals:
    void modificationChanged();

private:
    void refreshList();
    void showEmitter();
    void edited();          // form -> emitter
    void structureChanged(); // emitters added/removed
    engine::ParticleEmitter* current();

    QString path_;
    std::shared_ptr<engine::ParticleEffect> effect_;
    bool modified_ = false;
    bool updating_ = false;

    QListWidget* list_ = nullptr;
    ParticlePreview* preview_ = nullptr;
    QLabel* stats_ = nullptr;
    QWidget* form_ = nullptr;
    // Fields
    QLineEdit* name_ = nullptr;
    QLineEdit* texture_ = nullptr;
    QSpinBox* columns_ = nullptr;
    QSpinBox* rows_ = nullptr;
    QDoubleSpinBox* frameRate_ = nullptr;
    QCheckBox* randomFrame_ = nullptr;
    QComboBox* blend_ = nullptr;
    QCheckBox* lit_ = nullptr;
    QCheckBox* stretch_ = nullptr;
    QCheckBox* sheltered_ = nullptr;
    QDoubleSpinBox* stretchFactor_ = nullptr;
    QDoubleSpinBox* rate_ = nullptr;
    QSpinBox* burst_ = nullptr;
    QDoubleSpinBox* duration_ = nullptr;
    QDoubleSpinBox* delay_ = nullptr;
    QSpinBox* max_ = nullptr;
    QComboBox* shape_ = nullptr;
    class Vec3Editor* shapeSize_ = nullptr;
    class Vec3Editor* direction_ = nullptr;
    QDoubleSpinBox* spread_ = nullptr;
    QDoubleSpinBox* speedMin_ = nullptr;
    QDoubleSpinBox* speedMax_ = nullptr;
    QDoubleSpinBox* gravity_ = nullptr;
    QDoubleSpinBox* attract_ = nullptr;
    QDoubleSpinBox* orbit_ = nullptr;
    QDoubleSpinBox* drag_ = nullptr;
    class Vec3Editor* wind_ = nullptr;
    QCheckBox* worldSpace_ = nullptr;
    QDoubleSpinBox* lifeMin_ = nullptr;
    QDoubleSpinBox* lifeMax_ = nullptr;
    QDoubleSpinBox* sizeMin_ = nullptr;
    QDoubleSpinBox* sizeMax_ = nullptr;
    CurveWidget* sizeCurve_ = nullptr;
    QPushButton* color_ = nullptr;
    QDoubleSpinBox* intensity_ = nullptr;
    GradientWidget* colorCurve_ = nullptr;
    QDoubleSpinBox* rotationMin_ = nullptr;
    QDoubleSpinBox* rotationMax_ = nullptr;
    QDoubleSpinBox* spinMin_ = nullptr;
    QDoubleSpinBox* spinMax_ = nullptr;
};
