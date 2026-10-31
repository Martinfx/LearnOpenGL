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

    enum class Tool { Select, Move, Rotate, Scale };
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    void setSnap(float grid, float angleDegrees, float scaleStep); // 0 = off
    void setSelected(int index);
    void focusSelected();
    const engine::FpsCamera& camera() const { return camera_; }

signals:
    void boxPicked(int index);
    // A file was dropped into the view: world point under the cursor and the object there (-1 = none).
    void assetDropped(const QString& path, glm::vec3 point, int object);
    void rendererReady(const QString& info);
    // Gizmo edits of the selected object (snapped increments while dragging).
    void moveRequested(int object, glm::vec3 delta);
    void rotateRequested(int object, glm::vec3 degrees);
    void scaleRequested(int object, glm::vec3 factor);
    void dragFinished(int object);
    void toolChanged(Tool tool);
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
    struct Handle {
        engine::Aabb pick;   // generous pick volume
        int axis;            // 0 x, 1 y, 2 z, 3 free / uniform
    };
    void tick();
    glm::vec3 gizmoCenter() const;
    float gizmoSize() const;
    std::vector<engine::OverlayBox> gizmoBoxes() const;
    std::vector<Handle> gizmoHandles() const;
    void dragTo(QPointF pos);
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
    Tool tool_ = Tool::Move;
    float gridSnap_ = 0.25f;
    float angleSnap_ = 15.0f;
    float scaleSnap_ = 0.1f;
    struct Drag {
        bool active = false;
        int axis = -1;
        QPointF startMouse;
        glm::vec3 startPosition{0.0f};  // object position at the start
        glm::vec3 anchor{0.0f};         // world point grabbed
        float startParam = 0.0f;        // position along the axis
        glm::vec3 applied{0.0f};        // move already sent
        float appliedAngle = 0.0f;
        float appliedScale = 1.0f;
    } drag_;
    bool panning_ = false;
};
