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
    // A file was dropped into the view: world point under the cursor and the object there (-1 = none).
    void assetDropped(const QString& path, glm::vec3 point, int object);
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
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

public:
    // World-space ray through a widget position.
    void rayAt(QPointF pos, glm::vec3& origin, glm::vec3& dir) const;
    // Point under the cursor on the scene (or the ground plane) and the object hit.
    glm::vec3 pointAt(QPointF pos, int* object = nullptr) const;

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
    QTimer animTimer_;          // plays character animations in the viewport (preview)
    QElapsedTimer animClock_;
};
