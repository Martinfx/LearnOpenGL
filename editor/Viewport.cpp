#include "Viewport.h"

#include <filesystem>

#include <engine/Geometry.h>

#include <QDragEnterEvent>
#include <QGuiApplication>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

namespace {
engine::GlProc qtGetProc(const char* name)
{
    return QOpenGLContext::currentContext()->getProcAddress(name);
}
} // namespace

Viewport::Viewport(engine::Scene* scene, QWidget* parent)
    : QOpenGLWidget(parent), scene_(scene)
{
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setMouseTracking(false);
    camera_.position = {0.0f, 12.0f, 22.0f};
    camera_.yawDegrees = -90.0f;
    camera_.pitchDegrees = -28.0f;
    camera_.fovDegrees = 70.0f;
    camera_.sensitivity = 0.2f;
    connect(&timer_, &QTimer::timeout, this, &Viewport::tick);

    // Animated characters keep moving in the editor so clips can be previewed.
    animClock_.start();
    connect(&animTimer_, &QTimer::timeout, this, [this] {
        const float dt = std::min(0.1f, static_cast<float>(animClock_.restart()) / 1000.0f);
        const bool animated = std::any_of(scene_->entities.begin(), scene_->entities.end(),
                                          [](const engine::Entity& e) { return e.model && e.model->skeleton; });
        if (!animated || !isVisible())
            return;
        engine::updateAnimations(*scene_, dt);
        update();
    });
    animTimer_.start(33);
}

Viewport::~Viewport()
{
    cleanup();
}

void Viewport::cleanup()
{
    if (!ready_)
        return;
    makeCurrent();
    renderer_.shutdown();
    doneCurrent();
    ready_ = false;
}

void Viewport::initializeGL()
{
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &Viewport::cleanup, Qt::UniqueConnection);
    std::string error;
    ready_ = renderer_.init(qtGetProc, &error);
    if (ready_) {
        renderer_.setQuality(quality_);
        if (!shaderDirectory_.isEmpty())
            renderer_.reloadShaders(std::filesystem::path(shaderDirectory_.toStdU16String()));
    }
    if (ready_)
        emit rendererReady(QString::fromStdString(renderer_.glInfo()));
    else
        emit rendererFailed(QString::fromStdString(error));
}

void Viewport::resizeGL(int, int) {}

void Viewport::setQuality(const engine::RenderQuality& quality)
{
    quality_ = quality;
    renderer_.setQuality(quality);
    update();
}

bool Viewport::reloadShaders(const QString& directory, QString* error)
{
    if (!ready_) {
        shaderDirectory_ = directory;
        return true;
    }
    makeCurrent();
    std::string log;
    const bool ok = renderer_.reloadShaders(std::filesystem::path(directory.toStdU16String()), &log);
    doneCurrent();
    if (ok)
        shaderDirectory_ = directory;
    else if (error)
        *error = QString::fromStdString(log);
    update();
    return ok;
}

void Viewport::paintGL()
{
    if (!ready_)
        return;
    const qreal dpr = devicePixelRatioF();
    engine::RenderView view;
    view.viewportWidth = std::max(1, static_cast<int>(width() * dpr));
    view.viewportHeight = std::max(1, static_cast<int>(height() * dpr));
    view.view = camera_.view();
    view.projection = camera_.projection(static_cast<float>(view.viewportWidth) / static_cast<float>(view.viewportHeight));
    view.cameraPosition = camera_.position;
    view.selectedObject = selected_;
    view.drawPlayerStart = true;
    view.gizmoBoxes = gizmoBoxes();
    renderer_.render(*scene_, view);
}

void Viewport::setSelected(int index)
{
    selected_ = index;
    update();
}

void Viewport::focusSelected()
{
    if (selected_ < 0 || selected_ >= scene_->objectCount())
        return;
    const engine::Aabb bounds = scene_->objectBounds(selected_);
    const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
    const float distance = std::max(3.0f, glm::length(bounds.max - bounds.min) * 1.2f);
    camera_.position = center - camera_.forward() * distance;
    update();
}

void Viewport::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    lastMouse_ = e->position();
    if (e->button() == Qt::RightButton) {
        flying_ = true;
        setCursor(Qt::BlankCursor);
        clock_.start();
        timer_.start(16);
    } else if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        setCursor(Qt::ClosedHandCursor);
    } else if (e->button() == Qt::LeftButton) {
        glm::vec3 origin, dir;
        rayAt(e->position(), origin, dir);
        // 1) A gizmo handle of the selected object.
        int axis = -1;
        float best = 1e30f;
        if (tool_ != Tool::Select && selected_ >= 0)
            for (const Handle& h : gizmoHandles())
                if (auto t = engine::rayAabb(origin, dir, h.pick); t && *t < best) {
                    best = *t;
                    axis = h.axis;
                }
        const int hit = engine::pickObject(*scene_, origin, dir);
        // 2) The selected object itself: free move on the ground plane / yaw / uniform scale.
        if (axis < 0 && tool_ != Tool::Select && hit >= 0 && hit == selected_)
            axis = 3;
        if (axis >= 0) {
            drag_ = Drag{};
            drag_.active = true;
            drag_.axis = axis;
            drag_.startMouse = e->position();
            const engine::Aabb b = scene_->objectBounds(selected_);
            drag_.startPosition = tool_ == Tool::Move ? (b.min + b.max) * 0.5f : gizmoCenter();
            drag_.startPosition = gizmoCenter();
            if (auto t = engine::rayAabb(origin, dir, b))
                drag_.anchor = origin + dir * *t;
            else
                drag_.anchor = gizmoCenter();
            if (axis < 3) {
                glm::vec3 e3(0.0f);
                e3[axis] = 1.0f;
                const glm::vec3 c = gizmoCenter();
                const glm::vec3 w0 = c - origin;
                const float bb = glm::dot(e3, dir), denom = 1.0f - bb * bb;
                drag_.startParam = denom > 1e-6f ? (bb * glm::dot(dir, w0) - glm::dot(e3, w0)) / denom : 0.0f;
            }
            return;
        }
        setSelected(hit);
        emit boxPicked(hit);
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF delta = e->position() - lastMouse_;
    lastMouse_ = e->position();
    if (drag_.active) {
        dragTo(e->position());
        return;
    }
    if (panning_) {
        // Middle mouse: pan in the view plane, faster further away.
        const float k = 0.01f * std::max(1.0f, glm::length(camera_.position - gizmoCenter()) * 0.15f);
        const glm::vec3 up = glm::cross(camera_.right(), camera_.forward());
        camera_.position += (-camera_.right() * static_cast<float>(delta.x()) + up * static_cast<float>(delta.y())) * k;
        update();
        return;
    }
    if (!flying_)
        return;
    camera_.look({static_cast<float>(delta.x()), static_cast<float>(delta.y())});
    update();
}

void Viewport::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {
        flying_ = false;
        unsetCursor();
        timer_.stop();
        keys_.clear();
    } else if (e->button() == Qt::MiddleButton) {
        panning_ = false;
        unsetCursor();
    } else if (e->button() == Qt::LeftButton && drag_.active) {
        drag_.active = false;
        emit dragFinished(selected_);
        update();
    }
}

void Viewport::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_F && !flying_) {
        focusSelected();
        return;
    }
    // Q W E R: select / move / rotate / scale (while not flying).
    if (!flying_ && e->modifiers() == Qt::NoModifier) {
        const std::pair<int, Tool> keys[] = {{Qt::Key_Q, Tool::Select}, {Qt::Key_W, Tool::Move},
                                             {Qt::Key_E, Tool::Rotate}, {Qt::Key_R, Tool::Scale}};
        for (const auto& [key, tool] : keys)
            if (e->key() == key) {
                setTool(tool);
                emit toolChanged(tool);
                return;
            }
    }
    if (!e->isAutoRepeat())
        keys_.insert(e->key());
    QOpenGLWidget::keyPressEvent(e);
}

void Viewport::keyReleaseEvent(QKeyEvent* e)
{
    if (!e->isAutoRepeat())
        keys_.remove(e->key());
    QOpenGLWidget::keyReleaseEvent(e);
}

void Viewport::wheelEvent(QWheelEvent* e)
{
    // While flying the wheel changes speed, otherwise it dollies the camera.
    const float notches = static_cast<float>(e->angleDelta().y()) / 120.0f;
    if (flying_) {
        flySpeed_ = std::clamp(flySpeed_ * std::pow(1.2f, notches), 1.0f, 200.0f);
    } else {
        camera_.position += camera_.forward() * notches * 2.0f;
        update();
    }
}

void Viewport::focusOutEvent(QFocusEvent* e)
{
    keys_.clear();
    QOpenGLWidget::focusOutEvent(e);
}

void Viewport::tick()
{
    const float dt = std::min(0.1f, static_cast<float>(clock_.restart()) / 1000.0f);
    glm::vec3 move(0.0f);
    if (keys_.contains(Qt::Key_W)) move += camera_.forward();
    if (keys_.contains(Qt::Key_S)) move -= camera_.forward();
    if (keys_.contains(Qt::Key_D)) move += camera_.right();
    if (keys_.contains(Qt::Key_A)) move -= camera_.right();
    if (keys_.contains(Qt::Key_E)) move.y += 1.0f;
    if (keys_.contains(Qt::Key_Q)) move.y -= 1.0f;
    if (glm::dot(move, move) > 0.0f) {
        const float boost = keys_.contains(Qt::Key_Shift) ? 3.0f : 1.0f;
        camera_.position += glm::normalize(move) * flySpeed_ * boost * dt;
        update();
    }
}

void Viewport::rayAt(QPointF pos, glm::vec3& origin, glm::vec3& dir) const
{
    // Unproject the position into a world-space ray.
    const float x = static_cast<float>(2.0 * pos.x() / width() - 1.0);
    const float y = static_cast<float>(1.0 - 2.0 * pos.y() / height());
    const glm::mat4 inv = glm::inverse(camera_.projection(static_cast<float>(width()) / static_cast<float>(height())) * camera_.view());
    const glm::vec4 nearPt = inv * glm::vec4(x, y, -1.0f, 1.0f);
    const glm::vec4 farPt = inv * glm::vec4(x, y, 1.0f, 1.0f);
    origin = glm::vec3(nearPt) / nearPt.w;
    dir = glm::normalize(glm::vec3(farPt) / farPt.w - origin);
}

glm::vec3 Viewport::pointAt(QPointF pos, int* object) const
{
    glm::vec3 origin, dir;
    rayAt(pos, origin, dir);
    const int hit = engine::pickObject(*scene_, origin, dir);
    if (object)
        *object = hit;
    if (hit >= 0)
        if (auto t = engine::rayAabb(origin, dir, scene_->objectBounds(hit)))
            return origin + dir * *t;
    // Ground plane y = 0, or 10 m ahead when looking up.
    if (dir.y < -1e-3f)
        return origin + dir * (-origin.y / dir.y);
    return origin + dir * 10.0f;
}

void Viewport::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void Viewport::dragMoveEvent(QDragMoveEvent* e)
{
    // Highlight the object a material would be assigned to.
    int object = -1;
    pointAt(e->position(), &object);
    setSelected(object);
    e->acceptProposedAction();
}

void Viewport::dropEvent(QDropEvent* e)
{
    int object = -1;
    const glm::vec3 point = pointAt(e->position(), &object);
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile())
            emit assetDropped(url.toLocalFile(), point, object);
    e->acceptProposedAction();
    setFocus();
}

void Viewport::setTool(Tool tool)
{
    tool_ = tool;
    update();
}

void Viewport::setSnap(float grid, float angleDegrees, float scaleStep)
{
    gridSnap_ = grid;
    angleSnap_ = angleDegrees;
    scaleSnap_ = scaleStep;
}

glm::vec3 Viewport::gizmoCenter() const
{
    if (selected_ < 0 || selected_ >= scene_->objectCount())
        return camera_.position + camera_.forward() * 10.0f;
    const engine::Aabb b = scene_->objectBounds(selected_);
    return (b.min + b.max) * 0.5f;
}

float Viewport::gizmoSize() const
{
    // Constant size on screen.
    return std::max(0.2f, glm::length(camera_.position - gizmoCenter()) * 0.14f);
}

std::vector<engine::OverlayBox> Viewport::gizmoBoxes() const
{
    std::vector<engine::OverlayBox> boxes;
    if (tool_ == Tool::Select || selected_ < 0 || selected_ >= scene_->objectCount())
        return boxes;
    const glm::vec3 c = gizmoCenter();
    const float len = gizmoSize(), t = len * 0.035f;
    const glm::vec3 colors[3] = {{0.95f, 0.25f, 0.25f}, {0.35f, 0.9f, 0.3f}, {0.3f, 0.5f, 1.0f}};
    const bool active = drag_.active;
    auto col = [&](int axis) { return active && drag_.axis == axis ? glm::vec3(1.0f, 0.9f, 0.2f) : colors[axis]; };
    if (tool_ == Tool::Rotate) {
        // Yaw ring around Y.
        for (int i = 0; i < 32; ++i) {
            const float a = static_cast<float>(i) / 32.0f * 6.2831853f;
            boxes.push_back({c + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * len, glm::vec3(t * 2.2f),
                             active ? glm::vec3(1.0f, 0.9f, 0.2f) : colors[1], true});
        }
        return boxes;
    }
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 e(0.0f), size(t);
        e[axis] = 1.0f;
        size[axis] = len;
        boxes.push_back({c + e * len * 0.5f, size, col(axis), true});
        const float tip = tool_ == Tool::Scale ? t * 3.5f : t * 3.0f;
        glm::vec3 tipSize(tip);
        if (tool_ == Tool::Move)
            tipSize[axis] = tip * 2.0f; // arrow head
        boxes.push_back({c + e * len, tipSize, col(axis), true});
    }
    boxes.push_back({c, glm::vec3(t * 4.0f), active && drag_.axis == 3 ? glm::vec3(1.0f, 0.9f, 0.2f) : glm::vec3(0.9f), true});
    return boxes;
}

std::vector<Viewport::Handle> Viewport::gizmoHandles() const
{
    std::vector<Handle> handles;
    if (tool_ == Tool::Select || selected_ < 0 || selected_ >= scene_->objectCount())
        return handles;
    const glm::vec3 c = gizmoCenter();
    const float len = gizmoSize(), r = len * 0.12f;
    if (tool_ == Tool::Rotate) {
        handles.push_back({{c - glm::vec3(len * 1.15f, r, len * 1.15f), c + glm::vec3(len * 1.15f, r, len * 1.15f)}, 1});
        return handles;
    }
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 lo(-r), hi(r);
        lo[axis] = len * 0.15f;
        hi[axis] = len * 1.15f;
        handles.push_back({{c + lo, c + hi}, axis});
    }
    handles.push_back({{c - glm::vec3(r * 1.5f), c + glm::vec3(r * 1.5f)}, 3});
    return handles;
}

void Viewport::dragTo(QPointF pos)
{
    if (selected_ < 0)
        return;
    const bool snap = !(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier); // Shift = no snapping
    auto snapTo = [snap](float v, float step) { return snap && step > 0.0f ? std::round(v / step) * step : v; };
    if (tool_ == Tool::Move) {
        glm::vec3 origin, dir;
        rayAt(pos, origin, dir);
        glm::vec3 total(0.0f);
        if (drag_.axis < 3) {
            glm::vec3 e(0.0f);
            e[drag_.axis] = 1.0f;
            const glm::vec3 c = drag_.startPosition;
            const glm::vec3 w0 = c - origin;
            const float b = glm::dot(e, dir), denom = 1.0f - b * b;
            if (denom < 1e-6f)
                return;
            const float param = (b * glm::dot(dir, w0) - glm::dot(e, w0)) / denom;
            total[drag_.axis] = param - drag_.startParam;
        } else {
            // Free move on the horizontal plane through the grabbed point.
            if (std::abs(dir.y) < 1e-4f)
                return;
            const float t = (drag_.anchor.y - origin.y) / dir.y;
            if (t <= 0.0f)
                return;
            const glm::vec3 hit = origin + dir * t;
            total = {hit.x - drag_.anchor.x, 0.0f, hit.z - drag_.anchor.z};
        }
        // Snap the object's center to the grid, not the offset.
        for (int i = 0; i < 3; ++i)
            if (total[i] != 0.0f)
                total[i] = snapTo(drag_.startPosition[i] + total[i], gridSnap_) - drag_.startPosition[i];
        const glm::vec3 step = total - drag_.applied;
        if (glm::length(step) > 1e-6f) {
            drag_.applied = total;
            emit moveRequested(selected_, step);
        }
    } else if (tool_ == Tool::Rotate) {
        const float angle = snapTo(static_cast<float>(pos.x() - drag_.startMouse.x()) * 0.5f, angleSnap_);
        if (angle != drag_.appliedAngle) {
            emit rotateRequested(selected_, glm::vec3(0.0f, angle - drag_.appliedAngle, 0.0f));
            drag_.appliedAngle = angle;
        }
    } else if (tool_ == Tool::Scale) {
        float factor = std::pow(2.0f, static_cast<float>(drag_.startMouse.y() - pos.y()) / 150.0f);
        factor = std::max(0.05f, snap && scaleSnap_ > 0.0f ? 1.0f + std::round((factor - 1.0f) / scaleSnap_) * scaleSnap_ : factor);
        if (std::abs(factor - drag_.appliedScale) > 1e-5f) {
            const float f = factor / drag_.appliedScale;
            glm::vec3 v(f);
            if (drag_.axis < 3) {
                v = glm::vec3(1.0f);
                v[drag_.axis] = f;
            }
            emit scaleRequested(selected_, v);
            drag_.appliedScale = factor;
        }
    }
    update();
}
