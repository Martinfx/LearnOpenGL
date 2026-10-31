#pragma once
#include <engine/Camera.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QSet>
#include <QTimer>

// 3D level view. Uses the same engine::Renderer as the game.
// Right mouse button + WASD/QE = fly, left click = select, F = focus selection.
class Viewport : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit Viewport(engine::Scene* scene, QWidget* parent = nullptr);
    ~Viewport() override;

    void setSelected(int index);
    void focusSelected();
    const engine::FpsCamera& camera() const { return camera_; }

signals:
    void boxPicked(int index);
    void rendererReady(const QString& info);
    void rendererFailed(const QString& error);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private:
    void tick();
    void cleanup();

    engine::Scene* scene_;
    engine::Renderer renderer_;
    engine::FpsCamera camera_;
    bool ready_ = false;
    int selected_ = -1;
    bool flying_ = false;
    QPointF lastMouse_;
    QSet<int> keys_;
    float flySpeed_ = 10.0f;
    QTimer timer_;
    QElapsedTimer clock_;
};
