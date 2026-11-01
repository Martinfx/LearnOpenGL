#include "AnimGraphEditor.h"
#include "AssetPaths.h"
#include <engine/Physics.h>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {

engine::GlProc qtGetProc(const char* name)
{
    return QOpenGLContext::currentContext()->getProcAddress(name);
}

QDoubleSpinBox* number(double min, double max, double step, int decimals, QWidget* parent, const QString& tip = {})
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setKeyboardTracking(false);
    s->setToolTip(tip);
    return s;
}

QString q(const std::string& s)
{
    return QString::fromStdString(s);
}

std::string s(const QString& q)
{
    return q.trimmed().toStdString();
}

// Sets a combo's items, keeping an editable text that is not among them.
void fillCombo(QComboBox* combo, const QStringList& items, const QString& current)
{
    const QSignalBlocker block(combo);
    combo->clear();
    combo->addItems(items);
    if (!current.isEmpty() && combo->findText(current) < 0)
        combo->addItem(current);
    combo->setCurrentText(current);
}

constexpr double kNodeW = 150.0, kNodeH = 46.0;

} // namespace

// ---------------------------------------------------------------- preview

AnimPreview::AnimPreview(QWidget* parent) : QOpenGLWidget(parent)
{
    setMinimumSize(280, 260);
    scene_.boxes.emplace_back("Ground", glm::vec3(0.0f, -0.05f, 0.0f), glm::vec3(10.0f, 0.1f, 10.0f), glm::vec3(0.34f, 0.33f, 0.31f));
    scene_.sunDirection = {-0.5f, -0.8f, -0.4f};
    scene_.environment.fogDensity = 0.0f;
    scene_.entities.emplace_back();
    engine::Entity& e = scene_.entities.back();
    e.name = "Preview";
    e.type = "preview";
    e.yawDegrees = 90.0f; // faces +Z, the camera
    connect(&timer_, &QTimer::timeout, this, [this] { update(); });
    timer_.start(16);
    clock_.start();
}

AnimPreview::~AnimPreview()
{
    cleanup();
}

bool AnimPreview::dropRagdoll(QString* error)
{
    stopRagdoll();
    engine::Entity& e = scene_.entities.front();
    if (!e.animator.graph || e.animator.graph->ragdoll.empty()) {
        if (error)
            *error = tr("No ragdoll bones: add them in the Ragdoll tab");
        return false;
    }
    if (!physics_)
        physics_ = std::make_unique<engine::PhysicsWorld>();
    physics_->setLevel(scene_); // the ground and the step
    // A little push backwards so it does not fold straight down.
    if (!engine::startRagdoll(*physics_, e, ragdoll_, glm::vec3(0.0f, 0.0f, -0.8f))) {
        if (error)
            *error = tr("None of the ragdoll bones is in this character's skeleton");
        return false;
    }
    return true;
}

void AnimPreview::stopRagdoll()
{
    if (physics_)
        engine::stopRagdoll(*physics_, ragdoll_);
    scene_.entities.front().animator.poseOverride.clear();
}

void AnimPreview::cleanup()
{
    if (!ready_)
        return;
    makeCurrent();
    renderer_.shutdown();
    doneCurrent();
    ready_ = false;
}

bool AnimPreview::setModel(const QString& path, QString* error)
{
    std::string err;
    auto model = engine::loadCharacterModel(std::filesystem::path(path.toStdU16String()), &err);
    if (!model || !model->skeleton) {
        if (error)
            *error = err.empty() ? tr("No skeleton in %1").arg(path) : QString::fromStdString(err);
        return false;
    }
    stopRagdoll();
    engine::Entity& e = scene_.entities.front();
    e.modelPath = std::filesystem::path(path.toStdU16String());
    e.model = model;
    e.animator.stop();
    e.animator.graphState = {};
    return true;
}

void AnimPreview::setGraph(std::shared_ptr<engine::AnimGraph> graph)
{
    scene_.entities.front().animator.setGraph(std::move(graph));
}

engine::Animator* AnimPreview::animator()
{
    engine::Entity& e = scene_.entities.front();
    return e.model && e.model->skeleton ? &e.animator : nullptr;
}

const engine::Skeleton* AnimPreview::skeleton() const
{
    const engine::Entity& e = scene_.entities.front();
    return e.model ? e.model->skeleton.get() : nullptr;
}

void AnimPreview::setStep(bool step)
{
    stopRagdoll(); // its collision was built with the old ground
    step_ = step;
    if (step_ && scene_.boxes.size() == 1) // under the character's left foot (it faces +Z: left is +X)
        scene_.boxes.emplace_back("Step", glm::vec3(0.25f, 0.1f, 0.0f), glm::vec3(0.4f, 0.2f, 0.7f), glm::vec3(0.45f, 0.44f, 0.5f));
    else if (!step_ && scene_.boxes.size() > 1)
        scene_.boxes.pop_back();
}

void AnimPreview::restart()
{
    engine::Animator& a = scene_.entities.front().animator;
    const std::vector<float> params = a.graphState.params;
    a.stop();
    a.graphState = {};
    a.graphState.params = params;
}

void AnimPreview::advance(float dt)
{
    if (ragdoll_.handle >= 0 && physics_) {
        engine::Entity& e = scene_.entities.front();
        if (dt > 0.0f)
            physics_->step(std::min(dt, 1.0f / 30.0f));
        if (e.model && e.model->skeleton)
            engine::ragdollPose(*physics_, ragdoll_, *e.model->skeleton, e.animator.poseOverride);
    }
    // Foot IK against the preview's ground (and the step).
    engine::updateAnimations(scene_, dt, [this](const glm::vec3& p) -> std::optional<float> {
        float y = 0.0f;
        for (const engine::Box& b : scene_.boxes) {
            const engine::Aabb a = b.bounds();
            if (p.x >= a.min.x && p.x <= a.max.x && p.z >= a.min.z && p.z <= a.max.z && a.max.y <= p.y)
                y = std::max(y, a.max.y);
        }
        return y;
    });
    emit ticked();
}

void AnimPreview::initializeGL()
{
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &AnimPreview::cleanup, Qt::UniqueConnection);
    ready_ = renderer_.init(qtGetProc);
}

void AnimPreview::paintGL()
{
    if (!ready_ || !isVisible())
        return;
    const qint64 now = clock_.elapsed();
    const float dt = paused_ ? 0.0f : std::clamp(static_cast<float>(now - last_) / 1000.0f, 0.0f, 0.1f) * speed_;
    last_ = now;
    advance(dt);
    const glm::vec3 target(0.0f, 0.95f, 0.0f);
    const glm::vec3 eye = target + distance_ * glm::vec3(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_));
    engine::RenderView view;
    view.view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const float dpr = static_cast<float>(devicePixelRatioF());
    view.viewportWidth = std::max(1, static_cast<int>(width() * dpr));
    view.viewportHeight = std::max(1, static_cast<int>(height() * dpr));
    view.projection = glm::perspective(glm::radians(45.0f), static_cast<float>(width()) / std::max(1, height()), 0.05f, 200.0f);
    view.cameraPosition = eye;
    view.time = static_cast<float>(now) / 1000.0f;
    renderer_.render(scene_, view);
}

void AnimPreview::mousePressEvent(QMouseEvent* e)
{
    dragFrom_ = e->pos();
}

void AnimPreview::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & (Qt::LeftButton | Qt::RightButton)))
        return;
    const QPoint d = e->pos() - dragFrom_;
    dragFrom_ = e->pos();
    yaw_ -= static_cast<float>(d.x()) * 0.01f;
    pitch_ = std::clamp(pitch_ + static_cast<float>(d.y()) * 0.01f, -0.3f, 1.45f);
}

void AnimPreview::wheelEvent(QWheelEvent* e)
{
    distance_ = std::clamp(distance_ * (e->angleDelta().y() > 0 ? 0.88f : 1.0f / 0.88f), 0.6f, 30.0f);
}

// ---------------------------------------------------------------- graph view

AnimGraphView::AnimGraphView(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(360, 240);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setToolTip(tr("Drag states to arrange them, middle or right drag pans, the wheel zooms. Double-click empty space adds a state; "
                  "right-click a state to add a transition from it, make it the entry or delete it."));
}

void AnimGraphView::setGraph(engine::AnimGraph* graph, int layer)
{
    graph_ = graph;
    layer_ = layer;
    state_ = -2;
    transition_ = -1;
    linkFrom_ = -2;
    fitPending_ = true;
    fit();
    update();
}

void AnimGraphView::fit()
{
    const engine::AnimGraph::Layer* l = layer();
    if (!l || width() < 50 || height() < 50)
        return;
    QRectF bounds = nodeRect(-1);
    for (int i = 0; i < static_cast<int>(l->states.size()); ++i)
        bounds = bounds.united(nodeRect(i));
    bounds.adjust(-30, -30, 30, 30);
    zoom_ = std::clamp(std::min(width() / bounds.width(), height() / bounds.height()), 0.3, 1.0);
    pan_ = QPointF(width() * 0.5, height() * 0.5) - bounds.center() * zoom_;
}

void AnimGraphView::wheelEvent(QWheelEvent* e)
{
    const QPointF at = toGraph(e->position());
    fitPending_ = false; // the user framed it: resizing no longer refits
    zoom_ = std::clamp(zoom_ * (e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15), 0.25, 2.5);
    pan_ = e->position() - at * zoom_; // the point under the cursor stays
    update();
}

void AnimGraphView::resizeEvent(QResizeEvent*)
{
    if (fitPending_)
        fit();
}

engine::AnimGraph::Layer* AnimGraphView::layer() const
{
    if (!graph_ || layer_ < 0 || layer_ >= static_cast<int>(graph_->layers.size()))
        return nullptr;
    return &graph_->layers[static_cast<std::size_t>(layer_)];
}

void AnimGraphView::select(int state, int transition)
{
    state_ = state;
    transition_ = transition;
    update();
    emit selectionChanged();
}

QRectF AnimGraphView::nodeRect(int state) const
{
    const engine::AnimGraph::Layer* l = layer();
    QPointF p(0.0, -90.0); // "Any state": above the leftmost, topmost corner of the states
    if (l && !l->states.empty()) {
        float minX = 1e9f, minY = 1e9f;
        for (const auto& st : l->states) {
            minX = std::min(minX, st.editorX);
            minY = std::min(minY, st.editorY);
        }
        p = QPointF(minX, minY - kNodeH * 2.0);
    }
    if (state >= 0 && l && state < static_cast<int>(l->states.size()))
        p = QPointF(l->states[static_cast<std::size_t>(state)].editorX, l->states[static_cast<std::size_t>(state)].editorY);
    return QRectF(p - QPointF(kNodeW * 0.5, kNodeH * 0.5), QSizeF(kNodeW, kNodeH));
}

int AnimGraphView::stateAt(QPointF p) const
{
    const engine::AnimGraph::Layer* l = layer();
    if (!l)
        return -2;
    for (int i = static_cast<int>(l->states.size()) - 1; i >= 0; --i)
        if (nodeRect(i).contains(p))
            return i;
    return nodeRect(-1).contains(p) ? -1 : -2;
}

bool AnimGraphView::arrow(int t, QPointF& from, QPointF& to) const
{
    const engine::AnimGraph::Layer* l = layer();
    if (!l || t < 0 || t >= static_cast<int>(l->transitions.size()))
        return false;
    const engine::AnimGraph::Transition& tr = l->transitions[static_cast<std::size_t>(t)];
    from = nodeRect(tr.from).center();
    to = nodeRect(tr.to).center();
    // Two-way pairs: each arrow shifted to its own side.
    QPointF d = to - from;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1.0)
        return false;
    const QPointF side(-d.y() / len * 7.0, d.x() / len * 7.0);
    from += side;
    to += side;
    return true;
}

int AnimGraphView::transitionAt(QPointF p) const
{
    const engine::AnimGraph::Layer* l = layer();
    if (!l)
        return -1;
    int best = -1;
    double bestDist = 8.0;
    for (int t = 0; t < static_cast<int>(l->transitions.size()); ++t) {
        QPointF a, b;
        if (!arrow(t, a, b))
            continue;
        const QPointF ab = b - a;
        const double k = std::clamp(QPointF::dotProduct(p - a, ab) / QPointF::dotProduct(ab, ab), 0.0, 1.0);
        const QPointF c = a + ab * k - p;
        const double d = std::hypot(c.x(), c.y());
        if (d < bestDist) {
            bestDist = d;
            best = t;
        }
    }
    return best;
}

void AnimGraphView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(34, 36, 40));
    // Grid that pans with the graph.
    p.setPen(QColor(44, 47, 52));
    const double g = 40.0 * zoom_;
    for (double x = std::fmod(pan_.x(), g); x < width(); x += g)
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
    for (double y = std::fmod(pan_.y(), g); y < height(); y += g)
        p.drawLine(QPointF(0, y), QPointF(width(), y));
    const engine::AnimGraph::Layer* l = layer();
    if (!l) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("No layer"));
        return;
    }
    const engine::AnimGraphState::Layer* play = nullptr;
    if (playback_ && layer_ < static_cast<int>(playback_->layers.size()) && playback_->resolvedRevision == graph_->revision)
        play = &playback_->layers[static_cast<std::size_t>(layer_)];

    p.translate(pan_);
    p.scale(zoom_, zoom_);
    // Transitions.
    for (int t = 0; t < static_cast<int>(l->transitions.size()); ++t) {
        QPointF a, b;
        if (!arrow(t, a, b))
            continue;
        // Clip the ends to the node borders (approximately: stop at the rectangle's half size).
        const QPointF d = b - a;
        const double len = std::hypot(d.x(), d.y());
        const QPointF u = d / len;
        const double cut = std::min(std::abs(kNodeW * 0.5 / (std::abs(u.x()) + 1e-6)), std::abs(kNodeH * 0.5 / (std::abs(u.y()) + 1e-6)));
        const QPointF s0 = a + u * std::min(cut, len * 0.4), s1 = b - u * std::min(cut + 2.0, len * 0.4);
        const bool selected = t == transition_;
        const bool active = play && play->previous >= 0 && l->transitions[static_cast<std::size_t>(t)].to == play->state &&
                            (l->transitions[static_cast<std::size_t>(t)].from == play->previous || l->transitions[static_cast<std::size_t>(t)].from < 0);
        const QColor c = selected ? QColor(255, 200, 80) : active ? QColor(120, 230, 140) : QColor(170, 175, 185);
        p.setPen(QPen(c, selected ? 2.5 : 1.6));
        p.drawLine(s0, s1);
        const QPointF n(-u.y(), u.x());
        QPainterPath head;
        head.moveTo(s1);
        head.lineTo(s1 - u * 10.0 + n * 5.0);
        head.lineTo(s1 - u * 10.0 - n * 5.0);
        head.closeSubpath();
        p.fillPath(head, c);
    }
    if (linkFrom_ >= -1) {
        p.setPen(QPen(QColor(255, 200, 80), 1.5, Qt::DashLine));
        p.drawLine(nodeRect(linkFrom_).center(), mouse_);
    }

    // Nodes.
    auto drawNode = [&](int i, const QString& name, const QString& sub) {
        const QRectF r = nodeRect(i);
        const bool selected = i == state_;
        const bool current = play && i >= 0 && i == play->state;
        const bool entry = i >= 0 && i == l->entry;
        QColor fill = i < 0 ? QColor(70, 60, 90) : entry ? QColor(70, 82, 60) : QColor(58, 64, 74);
        if (current)
            fill = QColor(50, 110, 70);
        p.setPen(QPen(selected ? QColor(255, 200, 80) : QColor(20, 20, 24), selected ? 2.5 : 1.0));
        p.setBrush(fill);
        p.drawRoundedRect(r, 6, 6);
        if (current) { // progress through the state's clips
            const double k = std::clamp(static_cast<double>(play->phase), 0.0, 1.0);
            p.fillRect(QRectF(r.left() + 6, r.bottom() - 6, (r.width() - 12) * k, 3), QColor(140, 240, 160));
        }
        p.setPen(Qt::white);
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(r.adjusted(6, 4, -6, -20), Qt::AlignLeft | Qt::AlignVCenter, name);
        f.setBold(false);
        f.setPointSizeF(f.pointSizeF() * 0.85);
        p.setFont(f);
        p.setPen(QColor(190, 195, 205));
        p.drawText(r.adjusted(6, 22, -6, -6), Qt::AlignLeft | Qt::AlignVCenter, sub);
        p.setFont(font());
    };
    drawNode(-1, tr("Any state"), tr("transitions from every state"));
    for (int i = 0; i < static_cast<int>(l->states.size()); ++i) {
        const engine::AnimGraph::State& st = l->states[static_cast<std::size_t>(i)];
        QString sub;
        switch (st.kind) {
        case engine::AnimGraph::StateKind::Clip: sub = st.samples.empty() ? tr("no clip") : q(st.samples[0].clip); break;
        case engine::AnimGraph::StateKind::Blend1D: sub = tr("1D blend by %1, %2 clips").arg(q(st.paramX)).arg(st.samples.size()); break;
        case engine::AnimGraph::StateKind::Blend2D: sub = tr("2D blend %1 / %2").arg(q(st.paramX), q(st.paramY)); break;
        case engine::AnimGraph::StateKind::Empty: sub = tr("empty: layer off"); break;
        }
        if (i == l->entry)
            sub = tr("entry · ") + sub;
        drawNode(i, q(st.name), p.fontMetrics().elidedText(sub, Qt::ElideRight, static_cast<int>(kNodeW) - 12));
    }
    p.resetTransform();
    if (linkFrom_ >= -1) {
        p.setPen(QColor(255, 200, 80));
        p.drawText(rect().adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop, tr("Click the target state (Esc or right-click cancels)"));
    }
}

void AnimGraphView::mousePressEvent(QMouseEvent* e)
{
    dragFrom_ = e->position();
    moved_ = false;
    if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        return;
    }
    if (e->button() == Qt::RightButton) {
        if (linkFrom_ >= -1) {
            linkFrom_ = -2;
            update();
        }
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    engine::AnimGraph::Layer* l = layer();
    if (!l)
        return;
    const QPointF at = toGraph(e->position());
    const int hit = stateAt(at);
    if (linkFrom_ >= -1) {
        if (hit >= 0 && hit != linkFrom_) {
            engine::AnimGraph::Transition t;
            t.from = linkFrom_;
            t.to = hit;
            l->transitions.push_back(t);
            linkFrom_ = -2;
            emit edited(true);
            select(-2, static_cast<int>(l->transitions.size()) - 1);
        }
        return;
    }
    if (hit >= -1) {
        dragging_ = hit >= 0;
        select(hit, -1);
        return;
    }
    const int t = transitionAt(at);
    if (t >= 0) {
        select(-2, t);
        return;
    }
    panning_ = true; // empty space: pan
    select(-2, -1);
}

void AnimGraphView::mouseMoveEvent(QMouseEvent* e)
{
    mouse_ = toGraph(e->position());
    const QPointF d = e->position() - dragFrom_;
    if (panning_ || (e->buttons() & Qt::RightButton && std::hypot(d.x(), d.y()) > 3.0)) {
        fitPending_ = false;
        pan_ += d;
        dragFrom_ = e->position();
        update();
        return;
    }
    if (dragging_ && state_ >= 0) {
        engine::AnimGraph::State& st = layer()->states[static_cast<std::size_t>(state_)];
        st.editorX += static_cast<float>(d.x() / zoom_);
        st.editorY += static_cast<float>(d.y() / zoom_);
        dragFrom_ = e->position();
        moved_ = true;
        update();
        return;
    }
    if (linkFrom_ >= -1)
        update();
}

void AnimGraphView::mouseReleaseEvent(QMouseEvent*)
{
    if (dragging_ && moved_) { // snap to a 10 px grid, then it counts as an edit
        engine::AnimGraph::State& st = layer()->states[static_cast<std::size_t>(state_)];
        st.editorX = std::round(st.editorX / 10.0f) * 10.0f;
        st.editorY = std::round(st.editorY / 10.0f) * 10.0f;
        emit edited(false);
    }
    dragging_ = panning_ = moved_ = false;
    update();
}

void AnimGraphView::mouseDoubleClickEvent(QMouseEvent* e)
{
    const QPointF at = toGraph(e->position());
    if (e->button() == Qt::LeftButton && stateAt(at) < -1 && transitionAt(at) < 0)
        addState(at);
}

void AnimGraphView::addState(QPointF at)
{
    engine::AnimGraph::Layer* l = layer();
    if (!l)
        return;
    engine::AnimGraph::State st;
    int n = static_cast<int>(l->states.size()) + 1;
    do
        st.name = "State " + std::to_string(n++);
    while (graph_->findState(layer_, st.name) >= 0);
    st.editorX = static_cast<float>(std::round(at.x() / 10.0) * 10.0);
    st.editorY = static_cast<float>(std::round(at.y() / 10.0) * 10.0);
    l->states.push_back(st);
    emit edited(true);
    select(static_cast<int>(l->states.size()) - 1, -1);
}

void AnimGraphView::deleteState(int index)
{
    engine::AnimGraph::Layer* l = layer();
    if (!l || index < 0 || index >= static_cast<int>(l->states.size()))
        return;
    l->states.erase(l->states.begin() + index);
    auto& ts = l->transitions;
    ts.erase(std::remove_if(ts.begin(), ts.end(), [index](const auto& t) { return t.from == index || t.to == index; }), ts.end());
    for (auto& t : ts) {
        if (t.from > index)
            --t.from;
        if (t.to > index)
            --t.to;
    }
    if (l->entry > index || l->entry >= static_cast<int>(l->states.size()))
        l->entry = std::max(0, l->entry - 1);
    emit edited(true);
    select(-2, -1);
}

void AnimGraphView::contextMenuEvent(QContextMenuEvent* e)
{
    if (std::hypot((e->pos() - dragFrom_).x(), (e->pos() - dragFrom_).y()) > 3.0 || !layer())
        return; // the right drag panned
    engine::AnimGraph::Layer* l = layer();
    const QPointF at = toGraph(e->pos());
    const int hit = stateAt(at);
    const int t = hit < -1 ? transitionAt(at) : -1;
    QMenu menu(this);
    if (hit >= -1) {
        select(hit, -1);
        menu.addAction(tr("Add transition from here"), this, [this, hit] {
            linkFrom_ = hit;
            update();
        });
        if (hit >= 0) {
            menu.addAction(tr("Set as entry state"), this, [this, l, hit] {
                l->entry = hit;
                emit edited(false);
                update();
            });
            menu.addSeparator();
            menu.addAction(tr("Delete state"), this, [this, hit] { deleteState(hit); });
        }
    } else if (t >= 0) {
        select(-2, t);
        menu.addAction(tr("Delete transition"), this, [this, l, t] {
            l->transitions.erase(l->transitions.begin() + t);
            emit edited(true);
            select(-2, -1);
        });
    } else {
        menu.addAction(tr("Add state"), this, [this, at] { addState(at); });
    }
    menu.exec(e->globalPos());
}

// ---------------------------------------------------------------- timeline

AnimTimeline::AnimTimeline(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(64);
    setMouseTracking(true);
    setToolTip(tr("Clip of the selected layer's state. Click or drag to scrub (pauses), double-click to add an event "
                  "(footstep, mag_out...), drag an event to move it, right-click it to remove it."));
}

void AnimTimeline::setClip(engine::AnimGraph* graph, const QString& clip, float duration, float time)
{
    graph_ = graph;
    clip_ = clip;
    duration_ = duration;
    time_ = time;
    update();
}

double AnimTimeline::xAt(float t) const
{
    const double w = width() - 24.0;
    return 12.0 + (duration_ > 0.0f ? t / duration_ : 0.0) * w;
}

float AnimTimeline::timeAt(double x) const
{
    const double w = std::max(1.0, width() - 24.0);
    return static_cast<float>(std::clamp((x - 12.0) / w, 0.0, 1.0)) * duration_;
}

int AnimTimeline::eventAt(QPointF p) const
{
    if (!graph_ || p.y() < 18)
        return -1;
    for (int i = 0; i < static_cast<int>(graph_->events.size()); ++i) {
        const auto& ev = graph_->events[static_cast<std::size_t>(i)];
        if (q(ev.clip) == clip_ && std::abs(xAt(ev.time) - p.x()) < 6.0)
            return i;
    }
    return -1;
}

void AnimTimeline::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(30, 32, 36));
    if (clip_.isEmpty() || duration_ <= 0.0f) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("No clip playing in this layer"));
        return;
    }
    p.setPen(QColor(200, 205, 215));
    p.drawText(QRectF(12, 2, width() - 24, 16), Qt::AlignLeft | Qt::AlignVCenter,
               tr("%1  ·  %2 s  ·  %3 s").arg(clip_).arg(static_cast<double>(time_), 0, 'f', 2).arg(static_cast<double>(duration_), 0, 'f', 2));
    const QRectF bar(12, 22, width() - 24, height() - 30);
    p.fillRect(bar, QColor(46, 50, 56));
    // Ticks every 0.1 s, labels every 0.5 s.
    p.setPen(QColor(80, 85, 95));
    for (float t = 0.0f; t <= duration_ + 1e-4f; t += 0.1f) {
        const double x = xAt(t);
        const bool major = std::fmod(t + 1e-4f, 0.5f) < 2e-4f;
        p.drawLine(QPointF(x, bar.bottom()), QPointF(x, bar.bottom() - (major ? 10 : 5)));
    }
    if (graph_)
        for (const auto& ev : graph_->events) {
            if (q(ev.clip) != clip_)
                continue;
            const double x = xAt(ev.time);
            QPainterPath diamond;
            diamond.moveTo(x, bar.top() + 2);
            diamond.lineTo(x + 5, bar.top() + 8);
            diamond.lineTo(x, bar.top() + 14);
            diamond.lineTo(x - 5, bar.top() + 8);
            diamond.closeSubpath();
            p.fillPath(diamond, QColor(240, 180, 70));
            p.setPen(QColor(240, 200, 120));
            p.drawText(QPointF(x + 7, bar.top() + 13), q(ev.name));
        }
    const double x = xAt(time_);
    p.setPen(QPen(QColor(120, 230, 140), 2));
    p.drawLine(QPointF(x, bar.top()), QPointF(x, bar.bottom()));
}

void AnimTimeline::mousePressEvent(QMouseEvent* e)
{
    if (!graph_ || duration_ <= 0.0f)
        return;
    const int ev = eventAt(e->position());
    if (e->button() == Qt::RightButton && ev >= 0) {
        graph_->events.erase(graph_->events.begin() + ev);
        emit eventsEdited();
        update();
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (ev >= 0) {
        dragEvent_ = ev;
        return;
    }
    scrubbing_ = true;
    time_ = timeAt(e->position().x());
    emit scrubbed(time_);
    update();
}

void AnimTimeline::mouseMoveEvent(QMouseEvent* e)
{
    if (dragEvent_ >= 0 && graph_) {
        graph_->events[static_cast<std::size_t>(dragEvent_)].time = std::round(timeAt(e->position().x()) * 100.0f) / 100.0f;
        update();
    } else if (scrubbing_) {
        time_ = timeAt(e->position().x());
        emit scrubbed(time_);
        update();
    }
}

void AnimTimeline::mouseReleaseEvent(QMouseEvent*)
{
    if (dragEvent_ >= 0)
        emit eventsEdited();
    dragEvent_ = -1;
    scrubbing_ = false;
}

void AnimTimeline::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (!graph_ || duration_ <= 0.0f || e->button() != Qt::LeftButton || eventAt(e->position()) >= 0)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Clip event"), tr("Event name (scripts check it with self:anim_event):"),
                                               QLineEdit::Normal, QStringLiteral("footstep"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    graph_->events.push_back({s(clip_), std::round(timeAt(e->position().x()) * 100.0f) / 100.0f, s(name)});
    emit eventsEdited();
    update();
}

// ---------------------------------------------------------------- editor

AnimGraphEditor::AnimGraphEditor(QWidget* parent) : QWidget(parent)
{
    // Toolbar: layer, preview model, playback.
    layerCombo_ = new QComboBox(this);
    layerCombo_->setToolTip(tr("Layer shown in the graph. Layers below the first override the bones under their mask."));
    auto* addLayer = new QPushButton(tr("+ Layer"), this);
    auto* removeLayer = new QPushButton(tr("- Layer"), this);
    modelCombo_ = new QComboBox(this);
    modelCombo_->setToolTip(tr("Character the preview plays the graph on (clip names come from it)"));
    QDirIterator it(assetPath("models/characters"), {"*.glb", "*.gltf"}, QDir::Files, QDirIterator::Subdirectories);
    QStringList models;
    while (it.hasNext())
        models << QDir(assetRoot()).relativeFilePath(it.next());
    models.sort();
    models.erase(std::remove_if(models.begin(), models.end(), [](const QString& m) { return m.contains("_lod"); }), models.end());
    modelCombo_->addItems(models);
    playButton_ = new QPushButton(tr("Pause"), this);
    auto* restart = new QPushButton(tr("Restart"), this);
    auto* speed = new QComboBox(this);
    for (const char* t : {"0.1x", "0.25x", "0.5x", "1x", "2x"})
        speed->addItem(t);
    speed->setCurrentText("1x");
    auto* step = new QCheckBox(tr("Step (foot IK)"), this);
    step->setToolTip(tr("A 20 cm step under the left foot: the foot IK chains put each foot on its own ground"));
    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Layer"), this));
    bar->addWidget(layerCombo_, 1);
    bar->addWidget(addLayer);
    bar->addWidget(removeLayer);
    bar->addSpacing(16);
    bar->addWidget(new QLabel(tr("Preview"), this));
    bar->addWidget(modelCombo_, 1);
    bar->addWidget(playButton_);
    bar->addWidget(restart);
    bar->addWidget(speed);
    bar->addWidget(step);

    // Left: parameters and IK.
    auto* leftTabs = new QTabWidget(this);
    {
        auto* page = new QWidget(leftTabs);
        auto* v = new QVBoxLayout(page);
        params_ = new QTableWidget(0, 4, page);
        params_->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Default"), tr("Preview")});
        params_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int c = 1; c < 4; ++c)
            params_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
        params_->verticalHeader()->hide();
        params_->setToolTip(tr("Scripts set these (self:set_anim, self:anim_trigger); the Preview column drives the preview"));
        auto* add = new QPushButton(tr("Add"), page);
        auto* remove = new QPushButton(tr("Remove"), page);
        auto* row = new QHBoxLayout;
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch();
        v->addWidget(params_);
        v->addLayout(row);
        leftTabs->addTab(page, tr("Parameters"));
        connect(add, &QPushButton::clicked, this, [this] {
            engine::AnimGraph::Param p;
            int n = static_cast<int>(graph_->params.size()) + 1;
            do
                p.name = "param" + std::to_string(n++);
            while (graph_->findParam(p.name) >= 0);
            graph_->params.push_back(p);
            edited(true);
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int r = params_->currentRow();
            if (r < 0 || r >= static_cast<int>(graph_->params.size()))
                return;
            graph_->params.erase(graph_->params.begin() + r);
            edited(true);
        });
        connect(params_, &QTableWidget::cellChanged, this, [this](int r, int c) {
            if (updating_ || c != 0 || r >= static_cast<int>(graph_->params.size()))
                return;
            const std::string name = s(params_->item(r, 0)->text());
            if (name.empty() || graph_->findParam(name) >= 0) {
                refreshParams();
                return;
            }
            renameParam(graph_->params[static_cast<std::size_t>(r)].name, name);
            edited(true);
        });
    }
    {
        auto* page = new QWidget(leftTabs);
        auto* v = new QVBoxLayout(page);
        ikList_ = new QListWidget(page);
        auto* add = new QPushButton(tr("Add"), page);
        auto* remove = new QPushButton(tr("Remove"), page);
        auto* row = new QHBoxLayout;
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch();
        auto* form = new QFormLayout;
        ikKind_ = new QComboBox(page);
        ikKind_->addItems({tr("Foot (reach the ground)"), tr("Hand (keep a grip)")});
        ikName_ = new QLineEdit(page);
        const char* boneLabels[] = {"Upper bone", "Middle bone", "End bone", "Grip target"};
        const char* boneTips[] = {"Thigh / upper arm", "Calf / forearm (the bending joint)", "Foot / hand: the bone that reaches",
                                  "Hand chains: the bone the hand keeps its reference offset to (e.g. the other hand)"};
        form->addRow(tr("Kind"), ikKind_);
        form->addRow(tr("Name"), ikName_);
        for (int i = 0; i < 4; ++i) {
            ikBones_[i] = new QComboBox(page);
            ikBones_[i]->setEditable(true);
            ikBones_[i]->setToolTip(tr(boneTips[i]));
            form->addRow(tr(boneLabels[i]), ikBones_[i]);
        }
        ikWeight_ = number(0.0, 1.0, 0.1, 2, page, tr("Strength of the chain"));
        ikWeightParam_ = new QComboBox(page);
        ikWeightParam_->setToolTip(tr("Float parameter 0..1 that scales the weight (eased over 0.2 s); empty = always"));
        ikClip_ = new QComboBox(page);
        ikClip_->setEditable(true);
        ikClip_->setToolTip(tr("Hand chains: clip whose grip (hand relative to the target) is kept"));
        ikTime_ = number(0.0, 60.0, 0.05, 2, page, tr("Seconds into the reference clip"));
        footMax_ = number(0.0, 2.0, 0.05, 2, page, tr("Largest step up or down the feet adapt to, m (all foot chains)"));
        form->addRow(tr("Weight"), ikWeight_);
        form->addRow(tr("Weight parameter"), ikWeightParam_);
        form->addRow(tr("Reference clip"), ikClip_);
        form->addRow(tr("Reference time"), ikTime_);
        form->addRow(tr("Foot max adjust"), footMax_);
        v->addWidget(ikList_, 1);
        v->addLayout(row);
        v->addLayout(form);
        leftTabs->addTab(page, tr("IK"));
        connect(ikList_, &QListWidget::currentRowChanged, this, [this] {
            if (!updating_)
                refreshIk();
        });
        connect(add, &QPushButton::clicked, this, [this] {
            engine::AnimGraph::IkChain c;
            c.name = "Chain " + std::to_string(graph_->ik.size() + 1);
            graph_->ik.push_back(c);
            edited(true);
            ikList_->setCurrentRow(static_cast<int>(graph_->ik.size()) - 1);
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int r = ikList_->currentRow();
            if (r < 0 || r >= static_cast<int>(graph_->ik.size()))
                return;
            graph_->ik.erase(graph_->ik.begin() + r);
            edited(true);
        });
        auto ikEdited = [this] {
            const int r = ikList_->currentRow();
            if (updating_ || r < 0 || r >= static_cast<int>(graph_->ik.size()))
                return;
            engine::AnimGraph::IkChain& c = graph_->ik[static_cast<std::size_t>(r)];
            c.kind = ikKind_->currentIndex() == 1 ? engine::AnimGraph::IkKind::Hand : engine::AnimGraph::IkKind::Foot;
            c.name = s(ikName_->text());
            c.root = s(ikBones_[0]->currentText());
            c.mid = s(ikBones_[1]->currentText());
            c.end = s(ikBones_[2]->currentText());
            c.target = s(ikBones_[3]->currentText());
            c.weight = static_cast<float>(ikWeight_->value());
            c.weightParam = s(ikWeightParam_->currentText());
            c.referenceClip = s(ikClip_->currentText());
            c.referenceTime = static_cast<float>(ikTime_->value());
            graph_->footMaxAdjust = static_cast<float>(footMax_->value());
            if (auto* item = ikList_->item(r))
                item->setText(q(c.name));
            edited(false);
        };
        connect(ikKind_, &QComboBox::currentIndexChanged, this, ikEdited);
        connect(ikName_, &QLineEdit::editingFinished, this, ikEdited);
        for (QComboBox* b : ikBones_)
            connect(b, &QComboBox::currentTextChanged, this, ikEdited);
        connect(ikWeight_, &QDoubleSpinBox::valueChanged, this, ikEdited);
        connect(ikWeightParam_, &QComboBox::currentTextChanged, this, ikEdited);
        connect(ikClip_, &QComboBox::currentTextChanged, this, ikEdited);
        connect(ikTime_, &QDoubleSpinBox::valueChanged, this, ikEdited);
        connect(footMax_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            if (updating_)
                return;
            graph_->footMaxAdjust = static_cast<float>(v);
            edited(false);
        });
    }

    {
        auto* page = new QWidget(leftTabs);
        auto* v = new QVBoxLayout(page);
        ragdollOnDeath_ = new QCheckBox(tr("Ragdoll when the character dies"), page);
        ragdollOnDeath_->setToolTip(tr("The game replaces the animation with physics bodies at the moment of death"));
        ragdoll_ = new QTableWidget(0, 6, page);
        ragdoll_->setHorizontalHeaderLabels({tr("Bone"), tr("Radius"), tr("Length"), tr("Mass"), tr("Swing°"), tr("Twist°")});
        ragdoll_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        ragdoll_->verticalHeader()->hide();
        ragdoll_->setToolTip(tr("One capsule body per bone, from the bone to the next listed bone below it (Length 0) or Length metres; "
                                "jointed to the nearest listed bone above with a swing cone and a twist limit"));
        auto* add = new QPushButton(tr("Add"), page);
        auto* remove = new QPushButton(tr("Remove"), page);
        dropButton_ = new QPushButton(tr("Drop in preview"), page);
        dropButton_->setToolTip(tr("Test: the preview character goes limp from its current pose (click again to stand up)"));
        auto* row = new QHBoxLayout;
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch();
        row->addWidget(dropButton_);
        v->addWidget(ragdollOnDeath_);
        v->addWidget(ragdoll_, 1);
        v->addLayout(row);
        leftTabs->addTab(page, tr("Ragdoll"));
        connect(ragdollOnDeath_, &QCheckBox::toggled, this, [this](bool on) {
            if (updating_)
                return;
            graph_->ragdollOnDeath = on;
            edited(false);
        });
        connect(add, &QPushButton::clicked, this, [this] {
            engine::AnimGraph::RagdollBone b;
            const QStringList bones = boneNames();
            b.bone = bones.isEmpty() ? std::string() : s(bones.front());
            graph_->ragdoll.push_back(b);
            edited(true);
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int r = ragdoll_->currentRow();
            if (r < 0 || r >= static_cast<int>(graph_->ragdoll.size()))
                return;
            graph_->ragdoll.erase(graph_->ragdoll.begin() + r);
            edited(true);
        });
        connect(dropButton_, &QPushButton::clicked, this, [this] {
            if (preview_->ragdollActive()) {
                preview_->stopRagdoll();
                dropButton_->setText(tr("Drop in preview"));
                return;
            }
            QString error;
            if (!preview_->dropRagdoll(&error)) {
                status_->setText(error);
                return;
            }
            dropButton_->setText(tr("Stand up"));
        });
    }

    // Centre: graph and timeline.
    view_ = new AnimGraphView(this);
    timeline_ = new AnimTimeline(this);
    status_ = new QLabel(this);
    status_->setStyleSheet("color: #9aa;");
    auto* centre = new QWidget(this);
    {
        auto* v = new QVBoxLayout(centre);
        v->setContentsMargins(0, 0, 0, 0);
        v->addWidget(view_, 1);
        v->addWidget(timeline_);
        v->addWidget(status_);
    }

    // Right: preview and details.
    preview_ = new AnimPreview(this);
    details_ = new QStackedWidget(this);
    {
        // 0: layer
        auto* page = new QWidget(details_);
        auto* f = new QFormLayout(page);
        f->addRow(new QLabel(tr("<b>Layer</b> (select a state or a transition to edit it)"), page));
        layerName_ = new QLineEdit(page);
        layerMask_ = new QComboBox(page);
        layerMask_->setEditable(true);
        layerMask_->setToolTip(tr("This bone and everything below it (e.g. Bip01 Spine1 = upper body); empty = all bones"));
        layerWeight_ = number(0.0, 1.0, 0.1, 2, page, tr("How strongly the layer overrides the layers before it"));
        layerWeightParam_ = new QComboBox(page);
        layerWeightParam_->setToolTip(tr("Float parameter 0..1 used instead of Weight; empty = Weight"));
        f->addRow(tr("Name"), layerName_);
        f->addRow(tr("Mask bone"), layerMask_);
        f->addRow(tr("Weight"), layerWeight_);
        f->addRow(tr("Weight parameter"), layerWeightParam_);
        details_->addWidget(page);
        auto layerEdited = [this] {
            if (updating_ || layer_ >= static_cast<int>(graph_->layers.size()))
                return;
            engine::AnimGraph::Layer& l = graph_->layers[static_cast<std::size_t>(layer_)];
            l.name = s(layerName_->text());
            l.maskBone = s(layerMask_->currentText());
            l.weight = static_cast<float>(layerWeight_->value());
            l.weightParam = s(layerWeightParam_->currentText());
            layerCombo_->setItemText(layer_, q(l.name));
            edited(false);
        };
        connect(layerName_, &QLineEdit::editingFinished, this, layerEdited);
        connect(layerMask_, &QComboBox::currentTextChanged, this, layerEdited);
        connect(layerWeight_, &QDoubleSpinBox::valueChanged, this, layerEdited);
        connect(layerWeightParam_, &QComboBox::currentTextChanged, this, layerEdited);
    }
    {
        // 1: state
        auto* page = new QWidget(details_);
        auto* v = new QVBoxLayout(page);
        auto* f = new QFormLayout;
        stateName_ = new QLineEdit(page);
        stateKind_ = new QComboBox(page);
        stateKind_->addItems({tr("Clip"), tr("Blend space 1D"), tr("Blend space 2D"), tr("Empty (layer off)")});
        stateParamX_ = new QComboBox(page);
        stateParamY_ = new QComboBox(page);
        stateParamX_->setToolTip(tr("Float parameter along the blend space's X axis (e.g. speed)"));
        stateParamY_->setToolTip(tr("Float parameter along the Y axis (2D only)"));
        stateLoop_ = new QCheckBox(tr("Loop"), page);
        stateSpeed_ = number(0.05, 10.0, 0.1, 2, page, tr("Playback speed of the whole state"));
        f->addRow(new QLabel(tr("<b>State</b>"), page));
        f->addRow(tr("Name"), stateName_);
        f->addRow(tr("Kind"), stateKind_);
        f->addRow(tr("Parameter X"), stateParamX_);
        f->addRow(tr("Parameter Y"), stateParamY_);
        f->addRow(QString(), stateLoop_);
        f->addRow(tr("Speed"), stateSpeed_);
        samples_ = new QTableWidget(0, 4, page);
        samples_->setHorizontalHeaderLabels({tr("Clip"), tr("X"), tr("Y"), tr("Speed")});
        samples_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        samples_->verticalHeader()->hide();
        samples_->setToolTip(tr("Clips of the state; in a blend space each sits at its X (and Y) position"));
        auto* add = new QPushButton(tr("Add clip"), page);
        auto* remove = new QPushButton(tr("Remove clip"), page);
        auto* row = new QHBoxLayout;
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch();
        v->addLayout(f);
        v->addWidget(samples_, 1);
        v->addLayout(row);
        details_->addWidget(page);
        auto stateEdited = [this] {
            const int i = view_->selectedState();
            if (updating_ || i < 0 || layer_ >= static_cast<int>(graph_->layers.size()))
                return;
            engine::AnimGraph::State& st = graph_->layers[static_cast<std::size_t>(layer_)].states[static_cast<std::size_t>(i)];
            const std::string name = s(stateName_->text());
            if (!name.empty() && (name == st.name || graph_->findState(layer_, name) < 0))
                st.name = name;
            st.kind = static_cast<engine::AnimGraph::StateKind>(stateKind_->currentIndex());
            st.paramX = s(stateParamX_->currentText());
            st.paramY = s(stateParamY_->currentText());
            st.loop = stateLoop_->isChecked();
            st.speed = static_cast<float>(stateSpeed_->value());
            stateParamX_->setEnabled(st.kind == engine::AnimGraph::StateKind::Blend1D || st.kind == engine::AnimGraph::StateKind::Blend2D);
            stateParamY_->setEnabled(st.kind == engine::AnimGraph::StateKind::Blend2D);
            edited(false);
        };
        connect(stateName_, &QLineEdit::editingFinished, this, stateEdited);
        connect(stateKind_, &QComboBox::currentIndexChanged, this, stateEdited);
        connect(stateParamX_, &QComboBox::currentTextChanged, this, stateEdited);
        connect(stateParamY_, &QComboBox::currentTextChanged, this, stateEdited);
        connect(stateLoop_, &QCheckBox::toggled, this, stateEdited);
        connect(stateSpeed_, &QDoubleSpinBox::valueChanged, this, stateEdited);
        connect(add, &QPushButton::clicked, this, [this] {
            const int i = view_->selectedState();
            if (i < 0)
                return;
            auto& st = graph_->layers[static_cast<std::size_t>(layer_)].states[static_cast<std::size_t>(i)];
            engine::AnimGraph::Sample smp;
            const QStringList clips = clipNames();
            smp.clip = clips.isEmpty() ? std::string() : s(clips.front());
            if (!st.samples.empty())
                smp.x = st.samples.back().x + 1.0f;
            st.samples.push_back(smp);
            edited(true);
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int i = view_->selectedState();
            const int r = samples_->currentRow();
            if (i < 0 || r < 0)
                return;
            auto& st = graph_->layers[static_cast<std::size_t>(layer_)].states[static_cast<std::size_t>(i)];
            if (r < static_cast<int>(st.samples.size()))
                st.samples.erase(st.samples.begin() + r);
            edited(true);
        });
    }
    {
        // 2: transition
        auto* page = new QWidget(details_);
        auto* f = new QFormLayout(page);
        transitionLabel_ = new QLabel(page);
        transitionDuration_ = number(0.0, 5.0, 0.05, 2, page, tr("Crossfade time, s"));
        transitionExit_ = new QCheckBox(tr("Wait for exit time"), page);
        transitionExitTime_ = number(0.0, 10.0, 0.05, 2, page, tr("Fraction of the source state's clip (1 = its end; above 1 = later loops)"));
        transitionConditions_ = new QLineEdit(page);
        transitionConditions_->setPlaceholderText(tr("e.g. dead   !aim   speed > 2"));
        transitionConditions_->setToolTip(tr("All must hold. A name alone: bool or trigger set; !name: not set; name > value, <, >=, <=, ==, !=. "
                                             "Empty: leaves at the exit time."));
        f->addRow(new QLabel(tr("<b>Transition</b>"), page));
        f->addRow(transitionLabel_);
        f->addRow(tr("Crossfade"), transitionDuration_);
        f->addRow(transitionExit_);
        f->addRow(tr("Exit time"), transitionExitTime_);
        f->addRow(tr("Conditions"), transitionConditions_);
        details_->addWidget(page);
        auto transitionEdited = [this] {
            const int t = view_->selectedTransition();
            if (updating_ || t < 0)
                return;
            auto& tr = graph_->layers[static_cast<std::size_t>(layer_)].transitions[static_cast<std::size_t>(t)];
            tr.duration = static_cast<float>(transitionDuration_->value());
            tr.exitTime = transitionExit_->isChecked() ? static_cast<float>(transitionExitTime_->value()) : -1.0f;
            transitionExitTime_->setEnabled(transitionExit_->isChecked());
            std::vector<engine::AnimGraph::Condition> conditions;
            bool ok = engine::parseAnimConditions(s(transitionConditions_->text()), conditions);
            for (const auto& c : conditions)
                ok = ok && graph_->findParam(c.param) >= 0;
            transitionConditions_->setStyleSheet(ok ? QString() : QStringLiteral("background: #5a2a2a;"));
            if (ok)
                tr.conditions = conditions;
            edited(false);
        };
        connect(transitionDuration_, &QDoubleSpinBox::valueChanged, this, transitionEdited);
        connect(transitionExit_, &QCheckBox::toggled, this, transitionEdited);
        connect(transitionExitTime_, &QDoubleSpinBox::valueChanged, this, transitionEdited);
        connect(transitionConditions_, &QLineEdit::editingFinished, this, transitionEdited);
    }
    auto* detailsScroll = new QScrollArea(this);
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setWidget(details_);
    auto* right = new QSplitter(Qt::Vertical, this);
    right->addWidget(preview_);
    right->addWidget(detailsScroll);
    right->setStretchFactor(0, 3);
    right->setStretchFactor(1, 2);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(leftTabs);
    split->addWidget(centre);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    split->setSizes({330, 640, 420});
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(bar);
    layout->addWidget(split, 1);

    connect(layerCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (updating_ || i < 0)
            return;
        layer_ = i;
        view_->setGraph(graph_.get(), layer_);
        refreshDetails();
    });
    connect(addLayer, &QPushButton::clicked, this, [this] {
        engine::AnimGraph::Layer l;
        l.name = "Layer " + std::to_string(graph_->layers.size() + 1);
        engine::AnimGraph::State off;
        off.name = "Off";
        off.kind = engine::AnimGraph::StateKind::Empty;
        l.states.push_back(off);
        graph_->layers.push_back(l);
        layer_ = static_cast<int>(graph_->layers.size()) - 1;
        edited(true);
    });
    connect(removeLayer, &QPushButton::clicked, this, [this] {
        if (graph_->layers.size() <= 1)
            return;
        graph_->layers.erase(graph_->layers.begin() + layer_);
        layer_ = std::max(0, layer_ - 1);
        edited(true);
    });
    connect(modelCombo_, &QComboBox::currentTextChanged, this, [this](const QString& m) {
        QString error;
        if (!preview_->setModel(QDir(assetRoot()).absoluteFilePath(m), &error))
            status_->setText(error);
        preview_->setGraph(graph_);
        refreshClipLists();
    });
    connect(playButton_, &QPushButton::clicked, this, [this] {
        preview_->setPaused(!preview_->paused());
        playButton_->setText(preview_->paused() ? tr("Play") : tr("Pause"));
    });
    connect(restart, &QPushButton::clicked, this, [this] { preview_->restart(); });
    connect(speed, &QComboBox::currentTextChanged, this, [this](const QString& t) { preview_->setSpeed(QString(t).remove('x').toFloat()); });
    connect(step, &QCheckBox::toggled, preview_, &AnimPreview::setStep);
    connect(view_, &AnimGraphView::selectionChanged, this, [this] { refreshDetails(); });
    connect(view_, &AnimGraphView::edited, this, [this](bool structure) { edited(structure); });
    connect(preview_, &AnimPreview::ticked, this, [this] { tick(); });
    connect(timeline_, &AnimTimeline::eventsEdited, this, [this] { edited(false); });
    connect(timeline_, &AnimTimeline::scrubbed, this, [this](float time) {
        engine::Animator* a = preview_->animator();
        if (!a || layer_ >= static_cast<int>(a->graphState.layers.size()))
            return;
        preview_->setPaused(true);
        playButton_->setText(tr("Play"));
        // The state's clips share the phase: scrubbing the dominant clip moves them all.
        auto& l = a->graphState.layers[static_cast<std::size_t>(layer_)];
        float duration = 0.0f;
        if (const engine::Skeleton* sk = preview_->skeleton()) {
            const auto& st = graph_->layers[static_cast<std::size_t>(layer_)].states[static_cast<std::size_t>(l.state)];
            if (!st.samples.empty())
                if (const int c = sk->findClip(st.samples[0].clip); c >= 0)
                    duration = sk->clips[static_cast<std::size_t>(c)].duration;
        }
        l.phase = duration > 0.0f ? time / duration : 0.0f;
        preview_->advance(0.0f);
    });
}

QString AnimGraphEditor::title() const
{
    return QFileInfo(path_).fileName() + (modified_ ? "*" : "");
}

bool AnimGraphEditor::load(const QString& path, QString* error)
{
    std::string err;
    auto graph = engine::loadAnimGraph(std::filesystem::path(path.toStdU16String()), &err);
    if (!graph) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    path_ = QFileInfo(path).absoluteFilePath();
    graph_ = graph;
    layer_ = 0;
    // Preview on a character whose clips the graph uses (the bundled humans by default).
    QString model;
    for (const QString& m : {QStringLiteral("models/characters/humans/soldier.glb"), QStringLiteral("models/characters/library/ual_mannequin.glb")})
        if (modelCombo_->findText(m) >= 0) {
            model = m;
            break;
        }
    if (!model.isEmpty() && modelCombo_->currentText() != model)
        modelCombo_->setCurrentText(model); // loads the model and sets the graph
    else {
        QString e;
        preview_->setModel(QDir(assetRoot()).absoluteFilePath(modelCombo_->currentText()), &e);
        preview_->setGraph(graph_);
    }
    refreshLayers();
    refreshParams();
    refreshClipLists();
    return true;
}

bool AnimGraphEditor::save(QString* error)
{
    std::string err;
    if (!engine::saveAnimGraphFile(*graph_, std::filesystem::path(path_.toStdU16String()), &err)) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    modified_ = false;
    emit modificationChanged();
    return true;
}

bool AnimGraphEditor::revert(QString* error)
{
    engine::AnimGraph fresh;
    std::string err;
    if (!engine::loadAnimGraphFile(fresh, std::filesystem::path(path_.toStdU16String()), &err)) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    const unsigned revision = graph_->revision;
    *graph_ = std::move(fresh); // the shared instance: characters in the level use it
    graph_->revision = revision + 1;
    modified_ = false;
    emit modificationChanged();
    return true;
}

void AnimGraphEditor::edited(bool structure)
{
    ++graph_->revision;
    if (!modified_) {
        modified_ = true;
        emit modificationChanged();
    }
    if (structure) {
        refreshLayers();
        refreshParams();
        refreshIk();
        refreshRagdoll();
        refreshDetails();
    }
    view_->update();
}

QStringList AnimGraphEditor::clipNames() const
{
    QStringList out;
    if (const engine::Skeleton* sk = preview_->skeleton())
        for (const auto& c : sk->clips)
            out << q(c.name);
    return out;
}

QStringList AnimGraphEditor::boneNames() const
{
    QStringList out;
    if (const engine::Skeleton* sk = preview_->skeleton())
        for (const auto& n : sk->nodes)
            if (!n.name.empty())
                out << q(n.name);
    return out;
}

QStringList AnimGraphEditor::paramNames(bool floatsOnly) const
{
    QStringList out{QString()};
    for (const auto& p : graph_->params)
        if (!floatsOnly || p.type == engine::AnimGraph::ParamType::Float)
            out << q(p.name);
    return out;
}

void AnimGraphEditor::renameParam(const std::string& from, const std::string& to)
{
    for (auto& p : graph_->params)
        if (p.name == from)
            p.name = to;
    auto fix = [&](std::string& n) {
        if (n == from)
            n = to;
    };
    for (auto& l : graph_->layers) {
        fix(l.weightParam);
        for (auto& st : l.states) {
            fix(st.paramX);
            fix(st.paramY);
        }
        for (auto& t : l.transitions)
            for (auto& c : t.conditions)
                fix(c.param);
    }
    for (auto& c : graph_->ik)
        fix(c.weightParam);
}

void AnimGraphEditor::refreshLayers()
{
    updating_ = true;
    layerCombo_->clear();
    for (const auto& l : graph_->layers)
        layerCombo_->addItem(q(l.name));
    layer_ = std::clamp(layer_, 0, std::max(0, static_cast<int>(graph_->layers.size()) - 1));
    layerCombo_->setCurrentIndex(layer_);
    updating_ = false;
    const int state = view_->selectedState(), transition = view_->selectedTransition();
    view_->setGraph(graph_.get(), layer_);
    const auto* l = layer_ < static_cast<int>(graph_->layers.size()) ? &graph_->layers[static_cast<std::size_t>(layer_)] : nullptr;
    if (l && state < static_cast<int>(l->states.size()) && transition < static_cast<int>(l->transitions.size()))
        view_->select(state, transition);
}

void AnimGraphEditor::refreshParams()
{
    updating_ = true;
    params_->setRowCount(static_cast<int>(graph_->params.size()));
    engine::Animator* a = preview_->animator();
    for (int r = 0; r < static_cast<int>(graph_->params.size()); ++r) {
        const engine::AnimGraph::Param& p = graph_->params[static_cast<std::size_t>(r)];
        params_->setItem(r, 0, new QTableWidgetItem(q(p.name)));
        auto* type = new QComboBox(params_);
        type->addItems({tr("float"), tr("bool"), tr("trigger")});
        type->setCurrentIndex(static_cast<int>(p.type));
        type->setMaximumWidth(78);
        params_->setCellWidget(r, 1, type);
        connect(type, &QComboBox::currentIndexChanged, this, [this, r](int i) {
            graph_->params[static_cast<std::size_t>(r)].type = static_cast<engine::AnimGraph::ParamType>(i);
            edited(true);
        });
        auto* def = number(-1000.0, 1000.0, 0.1, 2, params_);
        def->setButtonSymbols(QAbstractSpinBox::NoButtons);
        def->setMaximumWidth(60);
        def->setValue(p.value);
        params_->setCellWidget(r, 2, def);
        connect(def, &QDoubleSpinBox::valueChanged, this, [this, r](double v) {
            graph_->params[static_cast<std::size_t>(r)].value = static_cast<float>(v);
            edited(false);
        });
        const std::string name = p.name;
        if (p.type == engine::AnimGraph::ParamType::Trigger) {
            auto* fire = new QPushButton(tr("Fire"), params_);
            params_->setCellWidget(r, 3, fire);
            connect(fire, &QPushButton::clicked, this, [this, name] {
                if (engine::Animator* an = preview_->animator())
                    an->trigger(name);
            });
        } else if (p.type == engine::AnimGraph::ParamType::Bool) {
            auto* box = new QCheckBox(params_);
            box->setChecked(a && a->param(name) != 0.0f);
            params_->setCellWidget(r, 3, box);
            connect(box, &QCheckBox::toggled, this, [this, name](bool on) {
                if (engine::Animator* an = preview_->animator())
                    an->setParam(name, on ? 1.0f : 0.0f);
            });
        } else {
            auto* value = number(-1000.0, 1000.0, 0.1, 2, params_);
            value->setKeyboardTracking(true);
            value->setValue(a ? a->param(name) : p.value);
            params_->setCellWidget(r, 3, value);
            connect(value, &QDoubleSpinBox::valueChanged, this, [this, name](double v) {
                if (engine::Animator* an = preview_->animator())
                    an->setParam(name, static_cast<float>(v));
            });
        }
    }
    updating_ = false;
}

void AnimGraphEditor::refreshIk()
{
    updating_ = true;
    const int row = ikList_->currentRow();
    {
        const QSignalBlocker block(ikList_);
        ikList_->clear();
        for (const auto& c : graph_->ik)
            ikList_->addItem(q(c.name));
        ikList_->setCurrentRow(std::min(row < 0 ? 0 : row, ikList_->count() - 1));
    }
    const int r = ikList_->currentRow();
    const bool has = r >= 0 && r < static_cast<int>(graph_->ik.size());
    for (QWidget* w : std::initializer_list<QWidget*>{ikKind_, ikName_, ikBones_[0], ikBones_[1], ikBones_[2], ikBones_[3], ikWeight_,
                                                      ikWeightParam_, ikClip_, ikTime_})
        w->setEnabled(has);
    footMax_->setValue(graph_->footMaxAdjust);
    const QStringList bones = boneNames();
    if (has) {
        const auto& c = graph_->ik[static_cast<std::size_t>(r)];
        ikKind_->setCurrentIndex(c.kind == engine::AnimGraph::IkKind::Hand ? 1 : 0);
        ikName_->setText(q(c.name));
        const std::string* names[] = {&c.root, &c.mid, &c.end, &c.target};
        for (int i = 0; i < 4; ++i)
            fillCombo(ikBones_[i], bones, q(*names[i]));
        ikWeight_->setValue(c.weight);
        fillCombo(ikWeightParam_, paramNames(true), q(c.weightParam));
        fillCombo(ikClip_, clipNames(), q(c.referenceClip));
        ikTime_->setValue(c.referenceTime);
        ikBones_[3]->setEnabled(c.kind == engine::AnimGraph::IkKind::Hand);
        ikClip_->setEnabled(c.kind == engine::AnimGraph::IkKind::Hand);
        ikTime_->setEnabled(c.kind == engine::AnimGraph::IkKind::Hand);
    }
    updating_ = false;
}

void AnimGraphEditor::refreshClipLists()
{
    refreshIk();
    refreshRagdoll();
    refreshDetails();
}

void AnimGraphEditor::refreshRagdoll()
{
    updating_ = true;
    ragdollOnDeath_->setChecked(graph_->ragdollOnDeath);
    ragdoll_->setRowCount(static_cast<int>(graph_->ragdoll.size()));
    const QStringList bones = boneNames();
    for (int r = 0; r < static_cast<int>(graph_->ragdoll.size()); ++r) {
        const auto& b = graph_->ragdoll[static_cast<std::size_t>(r)];
        auto* bone = new QComboBox(ragdoll_);
        bone->setEditable(true);
        fillCombo(bone, bones, q(b.bone));
        ragdoll_->setCellWidget(r, 0, bone);
        QDoubleSpinBox* spins[5] = {number(0.01, 0.5, 0.01, 3, ragdoll_), number(0.0, 2.0, 0.02, 2, ragdoll_), number(0.1, 100.0, 0.5, 1, ragdoll_),
                                    number(0.0, 170.0, 5.0, 0, ragdoll_), number(0.0, 170.0, 5.0, 0, ragdoll_)};
        const float values[5] = {b.radius, b.length, b.mass, b.swing, b.twist};
        for (int c = 0; c < 5; ++c) {
            spins[c]->setButtonSymbols(QAbstractSpinBox::NoButtons);
            spins[c]->setValue(values[c]);
            ragdoll_->setCellWidget(r, c + 1, spins[c]);
        }
        auto boneEdited = [this, r, bone, spins] {
            if (updating_ || r >= static_cast<int>(graph_->ragdoll.size()))
                return;
            auto& rb = graph_->ragdoll[static_cast<std::size_t>(r)];
            rb.bone = s(bone->currentText());
            rb.radius = static_cast<float>(spins[0]->value());
            rb.length = static_cast<float>(spins[1]->value());
            rb.mass = static_cast<float>(spins[2]->value());
            rb.swing = static_cast<float>(spins[3]->value());
            rb.twist = static_cast<float>(spins[4]->value());
            edited(false);
        };
        connect(bone, &QComboBox::currentTextChanged, this, boneEdited);
        for (QDoubleSpinBox* sp : spins)
            connect(sp, &QDoubleSpinBox::valueChanged, this, boneEdited);
    }
    ragdoll_->resizeColumnsToContents();
    ragdoll_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    updating_ = false;
}

void AnimGraphEditor::refreshDetails()
{
    updating_ = true;
    const int st = view_->selectedState(), tr = view_->selectedTransition();
    const auto* l = layer_ < static_cast<int>(graph_->layers.size()) ? &graph_->layers[static_cast<std::size_t>(layer_)] : nullptr;
    if (l && st >= 0 && st < static_cast<int>(l->states.size())) {
        details_->setCurrentIndex(1);
        const auto& s0 = l->states[static_cast<std::size_t>(st)];
        stateName_->setText(q(s0.name));
        stateKind_->setCurrentIndex(static_cast<int>(s0.kind));
        fillCombo(stateParamX_, paramNames(true), q(s0.paramX));
        fillCombo(stateParamY_, paramNames(true), q(s0.paramY));
        stateParamX_->setEnabled(s0.kind == engine::AnimGraph::StateKind::Blend1D || s0.kind == engine::AnimGraph::StateKind::Blend2D);
        stateParamY_->setEnabled(s0.kind == engine::AnimGraph::StateKind::Blend2D);
        stateLoop_->setChecked(s0.loop);
        stateSpeed_->setValue(s0.speed);
        samples_->setRowCount(static_cast<int>(s0.samples.size()));
        const QStringList clips = clipNames();
        for (int r = 0; r < static_cast<int>(s0.samples.size()); ++r) {
            const auto& smp = s0.samples[static_cast<std::size_t>(r)];
            auto* clip = new QComboBox(samples_);
            clip->setEditable(true);
            fillCombo(clip, clips, q(smp.clip));
            samples_->setCellWidget(r, 0, clip);
            QDoubleSpinBox* spins[3] = {number(-100, 100, 0.1, 2, samples_), number(-100, 100, 0.1, 2, samples_), number(0.05, 10, 0.1, 2, samples_)};
            spins[0]->setValue(smp.x);
            spins[1]->setValue(smp.y);
            spins[2]->setValue(smp.speed);
            for (int c = 0; c < 3; ++c)
                samples_->setCellWidget(r, c + 1, spins[c]);
            auto sampleEdited = [this, st, r, clip, spins] {
                if (updating_)
                    return;
                auto& state = graph_->layers[static_cast<std::size_t>(layer_)].states[static_cast<std::size_t>(st)];
                if (r >= static_cast<int>(state.samples.size()))
                    return;
                auto& smp2 = state.samples[static_cast<std::size_t>(r)];
                smp2.clip = s(clip->currentText());
                smp2.x = static_cast<float>(spins[0]->value());
                smp2.y = static_cast<float>(spins[1]->value());
                smp2.speed = static_cast<float>(spins[2]->value());
                edited(false);
            };
            connect(clip, &QComboBox::currentTextChanged, this, sampleEdited);
            for (QDoubleSpinBox* sp : spins)
                connect(sp, &QDoubleSpinBox::valueChanged, this, sampleEdited);
        }
    } else if (l && tr >= 0 && tr < static_cast<int>(l->transitions.size())) {
        details_->setCurrentIndex(2);
        const auto& t = l->transitions[static_cast<std::size_t>(tr)];
        const auto name = [&](int i) { return i >= 0 ? q(l->states[static_cast<std::size_t>(i)].name) : this->tr("Any state"); };
        transitionLabel_->setText(name(t.from) + "  →  " + name(t.to));
        transitionDuration_->setValue(t.duration);
        transitionExit_->setChecked(t.exitTime >= 0.0f);
        transitionExitTime_->setValue(t.exitTime >= 0.0f ? t.exitTime : 1.0);
        transitionExitTime_->setEnabled(t.exitTime >= 0.0f);
        transitionConditions_->setText(q(engine::animConditionsText(t.conditions)));
        transitionConditions_->setStyleSheet(QString());
    } else if (l) {
        details_->setCurrentIndex(0);
        layerName_->setText(q(l->name));
        fillCombo(layerMask_, QStringList{QString()} + boneNames(), q(l->maskBone));
        layerWeight_->setValue(l->weight);
        fillCombo(layerWeightParam_, paramNames(true), q(l->weightParam));
    }
    updating_ = false;
}

void AnimGraphEditor::tick()
{
    engine::Animator* a = preview_->animator();
    view_->setPlayback(a ? &a->graphState : nullptr);
    const engine::Skeleton* sk = preview_->skeleton();
    if (!a || !sk || layer_ >= static_cast<int>(a->graphState.layers.size()) || layer_ >= static_cast<int>(graph_->layers.size()) ||
        a->graphState.resolvedRevision != graph_->revision) {
        timeline_->setClip(graph_.get(), QString(), 0.0f, 0.0f);
        return;
    }
    const auto& l = a->graphState.layers[static_cast<std::size_t>(layer_)];
    const auto& states = graph_->layers[static_cast<std::size_t>(layer_)].states;
    if (l.state < 0 || l.state >= static_cast<int>(states.size()) || states[static_cast<std::size_t>(l.state)].samples.empty()) {
        timeline_->setClip(graph_.get(), QString(), 0.0f, 0.0f);
        return;
    }
    // The clip with the most weight right now (the one whose events fire).
    const auto& st = states[static_cast<std::size_t>(l.state)];
    std::string clip = st.samples[0].clip;
    if (st.kind == engine::AnimGraph::StateKind::Blend1D && !st.paramX.empty()) {
        const float x = a->param(st.paramX);
        float best = 1e9f;
        for (const auto& smp : st.samples)
            if (std::abs(smp.x - x) < best) {
                best = std::abs(smp.x - x);
                clip = smp.clip;
            }
    }
    const int c = sk->findClip(clip);
    const float duration = c >= 0 ? sk->clips[static_cast<std::size_t>(c)].duration : 0.0f;
    timeline_->setClip(graph_.get(), q(clip), duration, l.phase * duration);
    QStringList fired;
    for (const auto& e : a->graphState.events)
        fired << q(e);
    if (!fired.isEmpty())
        status_->setText(tr("Events: %1").arg(fired.join(", ")));
}
