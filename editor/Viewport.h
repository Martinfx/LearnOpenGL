#pragma once
#include <engine/Camera.h>
#include <engine/Particles.h>
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
    // Perspective camera or orthographic views from the top, front (+Z) and side (+X).
    enum class ViewMode { Perspective, Top, Front, Side };
    void setViewMode(ViewMode mode); // single view
    void setQuadView(bool on);       // top, perspective, front and side at once
    ViewMode viewMode() const { return mode_; }
    bool quadView() const { return quad_; }
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    void setSnap(float grid, float angleDegrees, float scaleStep); // 0 = off
    void setSelected(int index);
    void focusSelected();
    const engine::FpsCamera& camera() const { return camera_; }
    void setShading(engine::Shading shading) { shading_ = shading; update(); }
    engine::Shading shading() const { return shading_; }
    void setShowGrid(bool on) { showGrid_ = on; update(); }
    // Helper geometry drawn over the level (navmesh), not owned; null = none.
    void setDebugGeometry(const engine::DebugGeometry* geometry) { debug_ = geometry; update(); }
    // Second overlay (streaming cells), drawn with the navmesh.
    std::size_t particleMemoryBytes() const;
    void setOverlayGeometry(const engine::DebugGeometry* geometry) { overlay_ = geometry; update(); }
    bool showGrid() const { return showGrid_; }
    void setShowReference(bool on); // 1.8 m human next to the player start
    bool showReference() const { return showReference_; }
    void setOrthoWireframe(bool on) { orthoWireframe_ = on; update(); }
    bool orthoWireframe() const { return orthoWireframe_; }
    void setCameraSpeed(float metersPerSecond) { flySpeed_ = metersPerSecond; }
    float cameraSpeed() const { return flySpeed_; }
    int culledObjects() const { return renderer_.lastCulled(); }
    engine::Renderer::MemoryStats memoryStats() const { return renderer_.memoryStats(); }
    int drawCalls() const { return renderer_.lastDrawCalls(); }
    int instancedObjects() const { return renderer_.lastInstanced(); }
    const engine::Renderer::FrameStats& frameStats() const { return renderer_.frameStats(); }
    void setQuality(const engine::RenderQuality& quality);
    // Terrain brush (Landscape and Foliage modes): a ring follows the cursor on the
    // terrain; holding the left button sends brushStroke() about 30 times a second.
    void setBrush(bool on, float radius, glm::vec3 color);
    bool brushActive() const { return brush_; }
    const engine::RenderQuality& quality() const { return quality_; }
    // Recompiles the shaders from `directory` (over the built-in ones); false + compiler log on error.
    bool reloadShaders(const QString& directory, QString* error);

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
    void cameraSpeedChanged(float metersPerSecond);
    void rendererFailed(const QString& error);
    void brushStroke(glm::vec3 point, Qt::KeyboardModifiers modifiers, bool first);
    void brushFinished();
    void renderWarning(const QString& message); // OpenGL error while drawing (reported once per error code)

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
    // Particle effects of the level ("particle" entities, rain) play live in the view.
    void syncParticles(float dt);
    engine::ParticleSystem particles_;
    struct LiveEffect {
        std::string entity;
        std::filesystem::path file;
        engine::ParticleSystem::Handle handle = 0;
    };
    std::vector<LiveEffect> liveEffects_;
    void addAnomalyHelpers(engine::RenderView& view, bool warp) const;
    void addLightHelpers(engine::RenderView& view) const;
    mutable engine::DebugGeometry lightHelpers_; // filled per pane in addLightHelpers
    // Trigger volumes and the selected entity's I/O connections (arrows to its targets
    // in yellow, from the entities that send it inputs in cyan).
    void addIoHelpers(engine::RenderView& view) const;
    mutable engine::DebugGeometry ioHelpers_;
    mutable engine::DebugGeometry ioArrows_; // seen through walls
    engine::ParticleSystem::Handle rain_ = 0;
    QTimer particleTimer_;
    QElapsedTimer particleClock_;
    qint64 particleLast_ = 0;

    struct Handle {
        engine::Aabb pick;   // generous pick volume
        int axis;            // 0 x, 1 y, 2 z, 3 free / uniform
    };
    void tick();
    glm::vec3 gizmoCenter() const;
    float gizmoSize() const;
    std::vector<engine::OverlayBox> gizmoBoxes() const;
    std::vector<Handle> gizmoHandles() const;
    int handleAt(QPointF pos, float* distance = nullptr) const; // gizmo axis under the cursor, -1 = none
    void dragTo(QPointF pos);
    struct Pane {
        ViewMode mode;
        QRectF rect; // widget coordinates
    };
    std::vector<Pane> panes() const;
    void selectPane(QPointF pos) const;                 // makes the pane under `pos` current
    glm::mat4 paneView(ViewMode mode) const;
    glm::mat4 paneProjection(ViewMode mode, float aspect) const;
    glm::vec3 paneEye(ViewMode mode) const;
    void cleanup();

    engine::Scene* scene_;
    engine::Renderer renderer_;
    engine::RenderQuality quality_;
    unsigned lastGlError_ = 0;
    QString shaderDirectory_; // custom shaders applied, re-applied when the GL context is recreated
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
        float lastAngle = 0.0f;         // rotate ring: last angle under the cursor (radians)
        bool haveAngle = false;
        float totalAngle = 0.0f;        // rotate ring: unwrapped angle since the start (radians)
        QPointF lastMouse;
        float appliedScale = 1.0f;
    } drag_;
    bool panning_ = false;
    int hoverAxis_ = -1; // gizmo handle under the cursor (highlighted)
    ViewMode mode_ = ViewMode::Perspective;
    bool quad_ = false;
    engine::Shading shading_ = engine::Shading::Lit;
    bool orthoWireframe_ = true;
    bool showGrid_ = true;
    const engine::DebugGeometry* debug_ = nullptr;
    const engine::DebugGeometry* overlay_ = nullptr;
    bool showReference_ = false;
    bool brush_ = false, brushHover_ = false, painting_ = false;
    float brushRadius_ = 5.0f;
    glm::vec3 brushColor_{0.3f, 0.8f, 1.0f};
    glm::vec3 brushPoint_{0.0f};
    QTimer brushTimer_;
    bool brushHit(QPointF pos, glm::vec3& point) const;
    int settleFrames_ = 0;      // extra repaints so cached shadow cascades catch up
    bool settling_ = false;
    bool referenceLoading_ = false;
    std::shared_ptr<engine::Model> reference_;
    glm::vec3 orthoCenter_{0.0f, 0.0f, 0.0f};
    float orthoSize_ = 14.0f;                              // half height of the orthographic views, m
    mutable ViewMode paneMode_ = ViewMode::Perspective;    // pane being drawn or under the mouse
    mutable QRectF paneRect_;
};
