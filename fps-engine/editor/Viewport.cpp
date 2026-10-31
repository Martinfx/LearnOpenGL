#include "Viewport.h"

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
    setMouseTracking(false);
    camera_.position = {0.0f, 12.0f, 22.0f};
    camera_.yawDegrees = -90.0f;
    camera_.pitchDegrees = -28.0f;
    camera_.fovDegrees = 70.0f;
    camera_.sensitivity = 0.2f;
    connect(&timer_, &QTimer::timeout, this, &Viewport::tick);
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
    if (ready_)
        emit rendererReady(QString::fromStdString(renderer_.glInfo()));
    else
        emit rendererFailed(QString::fromStdString(error));
}

void Viewport::resizeGL(int, int) {}

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
    view.selectedBox = selected_;
    view.drawPlayerStart = true;
    renderer_.render(*scene_, view);
}

void Viewport::setSelected(int index)
{
    selected_ = index;
    update();
}

void Viewport::focusSelected()
{
    if (selected_ < 0 || selected_ >= static_cast<int>(scene_->boxes.size()))
        return;
    const engine::Box& b = scene_->boxes[static_cast<std::size_t>(selected_)];
    const float distance = std::max(3.0f, glm::length(b.size) * 1.2f);
    camera_.position = b.position - camera_.forward() * distance;
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
    } else if (e->button() == Qt::LeftButton) {
        // Unproject the click into a world-space ray.
        const float x = static_cast<float>(2.0 * e->position().x() / width() - 1.0);
        const float y = static_cast<float>(1.0 - 2.0 * e->position().y() / height());
        const glm::mat4 inv = glm::inverse(camera_.projection(static_cast<float>(width()) / static_cast<float>(height())) * camera_.view());
        glm::vec4 nearPt = inv * glm::vec4(x, y, -1.0f, 1.0f);
        glm::vec4 farPt = inv * glm::vec4(x, y, 1.0f, 1.0f);
        const glm::vec3 origin = glm::vec3(nearPt) / nearPt.w;
        const glm::vec3 dir = glm::normalize(glm::vec3(farPt) / farPt.w - origin);
        const int hit = engine::pickBox(*scene_, origin, dir);
        setSelected(hit);
        emit boxPicked(hit);
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* e)
{
    if (!flying_)
        return;
    const QPointF delta = e->position() - lastMouse_;
    lastMouse_ = e->position();
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
    }
}

void Viewport::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_F && !flying_) {
        focusSelected();
        return;
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
