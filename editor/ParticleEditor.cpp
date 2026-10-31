#include "ParticleEditor.h"
#include "AssetPaths.h"
#include "Vec3Editor.h"

#include <engine/Material.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
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

std::filesystem::path toPath(const QString& s)
{
    return s.isEmpty() ? std::filesystem::path() : std::filesystem::path(s.toStdU16String());
}

QString toQString(const std::filesystem::path& p)
{
    return p.empty() ? QString() : QString::fromStdU16String(p.u16string());
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

QWidget* pair(QWidget* a, QWidget* b, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(a);
    l->addWidget(b);
    return w;
}

QColor toColor(glm::vec4 c)
{
    return QColor::fromRgbF(std::clamp(c.r, 0.0f, 1.0f), std::clamp(c.g, 0.0f, 1.0f), std::clamp(c.b, 0.0f, 1.0f),
                            std::clamp(c.a, 0.0f, 1.0f));
}

void paintChecker(QPainter& p, const QRect& r)
{
    const int s = 6;
    for (int y = r.top(); y < r.bottom(); y += s)
        for (int x = r.left(); x < r.right(); x += s)
            p.fillRect(QRect(x, y, s, s).intersected(r), ((x - r.left()) / s + (y - r.top()) / s) % 2 ? QColor(90, 90, 90) : QColor(140, 140, 140));
}

} // namespace

// ---------------------------------------------------------------- preview

ParticlePreview::ParticlePreview(QWidget* parent) : QOpenGLWidget(parent)
{
    setMinimumSize(320, 280);
    scene_.boxes.emplace_back("Ground", glm::vec3(0.0f, -0.05f, 0.0f), glm::vec3(12.0f, 0.1f, 12.0f), glm::vec3(0.32f, 0.31f, 0.29f));
    scene_.boxes.emplace_back("Wall", glm::vec3(0.0f, 1.5f, -3.0f), glm::vec3(6.0f, 3.0f, 0.2f), glm::vec3(0.45f, 0.44f, 0.42f));
    scene_.sunDirection = {-0.5f, -0.8f, -0.4f};
    scene_.environment.fogDensity = 0.0f;
    connect(&timer_, &QTimer::timeout, this, [this] { update(); });
    timer_.start(16);
    clock_.start();
}

ParticlePreview::~ParticlePreview()
{
    cleanup();
}

void ParticlePreview::cleanup()
{
    if (!ready_)
        return;
    makeCurrent();
    renderer_.shutdown();
    doneCurrent();
    ready_ = false;
}

void ParticlePreview::setEffect(std::shared_ptr<engine::ParticleEffect> effect)
{
    effect_ = std::move(effect);
    restart();
}

void ParticlePreview::restart()
{
    particles_.clear();
    handle_ = effect_ ? particles_.spawn(effect_, glm::vec3(0.0f, 0.05f, 0.0f)) : 0;
    idle_ = 0.0f;
}

void ParticlePreview::initializeGL()
{
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &ParticlePreview::cleanup, Qt::UniqueConnection);
    ready_ = renderer_.init(qtGetProc);
}

void ParticlePreview::paintGL()
{
    if (!ready_ || !isVisible())
        return;
    const qint64 now = clock_.elapsed();
    const float dt = std::clamp(static_cast<float>(now - last_) / 1000.0f, 0.0f, 0.1f);
    last_ = now;
    particles_.update(dt);
    if (effect_ && !particles_.alive(handle_)) { // one-shot effects replay after a pause
        idle_ += dt;
        if (idle_ > 0.6f)
            restart();
    }
    const glm::vec3 target(0.0f, 0.8f, 0.0f);
    const glm::vec3 eye = target + distance_ * glm::vec3(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_));
    engine::RenderView view;
    view.view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const float dpr = static_cast<float>(devicePixelRatioF());
    view.viewportWidth = std::max(1, static_cast<int>(width() * dpr));
    view.viewportHeight = std::max(1, static_cast<int>(height() * dpr));
    view.projection = glm::perspective(glm::radians(50.0f), static_cast<float>(width()) / std::max(1, height()), 0.05f, 200.0f);
    view.cameraPosition = eye;
    view.time = static_cast<float>(now) / 1000.0f;
    particles_.appendSprites(eye, glm::mat3(glm::inverse(view.view)), view.sprites);
    renderer_.render(scene_, view);
}

void ParticlePreview::mousePressEvent(QMouseEvent* e)
{
    dragFrom_ = e->pos();
}

void ParticlePreview::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & (Qt::LeftButton | Qt::RightButton)))
        return;
    const QPoint d = e->pos() - dragFrom_;
    dragFrom_ = e->pos();
    yaw_ -= static_cast<float>(d.x()) * 0.01f;
    pitch_ = std::clamp(pitch_ + static_cast<float>(d.y()) * 0.01f, -0.2f, 1.45f);
}

void ParticlePreview::wheelEvent(QWheelEvent* e)
{
    distance_ = std::clamp(distance_ * (e->angleDelta().y() > 0 ? 0.88f : 1.0f / 0.88f), 0.8f, 80.0f);
}

// ---------------------------------------------------------------- curve

CurveWidget::CurveWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(90);
    setToolTip(tr("Size over the particle's life (multiplier). Drag points, double-click to add, right-click to remove."));
}

void CurveWidget::setKeys(const std::vector<engine::ParticleKey>& keys)
{
    keys_ = keys;
    scale_ = maxValue();
    update();
}

float CurveWidget::maxValue() const
{
    float m = 1.0f;
    for (const auto& k : keys_)
        m = std::max(m, k.value.x);
    return std::ceil(m * 1.25f);
}

QPointF CurveWidget::toScreen(float t, float v) const
{
    const QRectF r = QRectF(rect()).adjusted(8, 8, -8, -16);
    return {r.left() + t * r.width(), r.bottom() - v / scale_ * r.height()};
}

std::pair<float, float> CurveWidget::fromScreen(QPointF p) const
{
    const QRectF r = QRectF(rect()).adjusted(8, 8, -8, -16);
    return {std::clamp(static_cast<float>((p.x() - r.left()) / r.width()), 0.0f, 1.0f),
            std::max(0.0f, static_cast<float>((r.bottom() - p.y()) / r.height()) * scale_)};
}

int CurveWidget::keyAt(QPointF p) const
{
    for (std::size_t i = 0; i < keys_.size(); ++i)
        if (QLineF(toScreen(keys_[i].t, keys_[i].value.x), p).length() < 7.0)
            return static_cast<int>(i);
    return -1;
}

void CurveWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(32, 33, 36));
    p.setPen(QColor(60, 62, 66));
    for (int i = 0; i <= 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        p.drawLine(toScreen(t, 0.0f), toScreen(t, scale_));
    }
    p.setPen(QColor(90, 92, 96));
    p.drawLine(toScreen(0.0f, 1.0f), toScreen(1.0f, 1.0f)); // 1x
    p.setPen(QColor(150, 152, 156));
    p.drawText(QRectF(rect()).adjusted(10, 0, -10, 0), Qt::AlignBottom | Qt::AlignLeft, tr("birth"));
    p.drawText(QRectF(rect()).adjusted(10, 0, -10, 0), Qt::AlignBottom | Qt::AlignRight, tr("death"));
    p.drawText(toScreen(0.0f, scale_) + QPointF(4, 12), QString("x%1").arg(scale_));
    QPainterPath path;
    for (int i = 0; i <= 64; ++i) {
        const float t = static_cast<float>(i) / 64.0f;
        const QPointF q = toScreen(t, engine::evalCurve(keys_, t));
        i == 0 ? path.moveTo(q) : path.lineTo(q);
    }
    p.setPen(QPen(QColor(110, 180, 255), 2));
    p.drawPath(path);
    p.setBrush(QColor(230, 235, 245));
    for (const auto& k : keys_)
        p.drawEllipse(toScreen(k.t, k.value.x), 4, 4);
}

void CurveWidget::mousePressEvent(QMouseEvent* e)
{
    const int k = keyAt(e->position());
    if (e->button() == Qt::RightButton && k >= 0) {
        keys_.erase(keys_.begin() + k);
        update();
        emit edited();
        return;
    }
    dragging_ = k;
}

void CurveWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (dragging_ < 0)
        return;
    auto [t, v] = fromScreen(e->position());
    keys_[static_cast<std::size_t>(dragging_)].t = t;
    keys_[static_cast<std::size_t>(dragging_)].value.x = std::round(v * 100.0f) / 100.0f;
    // Keep the order by time (the dragged key follows its new place).
    const engine::ParticleKey moved = keys_[static_cast<std::size_t>(dragging_)];
    std::stable_sort(keys_.begin(), keys_.end(), [](const auto& a, const auto& b) { return a.t < b.t; });
    dragging_ = static_cast<int>(std::find_if(keys_.begin(), keys_.end(), [&](const auto& k) { return k.t == moved.t && k.value == moved.value; }) - keys_.begin());
    update();
    emit edited();
}

void CurveWidget::mouseReleaseEvent(QMouseEvent*)
{
    dragging_ = -1;
    scale_ = maxValue();
    update();
}

void CurveWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    auto [t, v] = fromScreen(e->position());
    engine::ParticleKey k;
    k.t = t;
    k.value = glm::vec4(std::round(v * 100.0f) / 100.0f);
    keys_.push_back(k);
    std::stable_sort(keys_.begin(), keys_.end(), [](const auto& a, const auto& b) { return a.t < b.t; });
    update();
    emit edited();
}

// ---------------------------------------------------------------- gradient

GradientWidget::GradientWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(44);
    setToolTip(tr("Color and opacity over the particle's life. Click to add a key, drag to move, double-click to pick its "
                  "color and opacity, right-click to remove."));
}

void GradientWidget::setKeys(const std::vector<engine::ParticleKey>& keys)
{
    keys_ = keys;
    update();
}

float GradientWidget::tAt(QPointF p) const
{
    return std::clamp(static_cast<float>((p.x() - 8.0) / std::max(1, width() - 16)), 0.0f, 1.0f);
}

int GradientWidget::keyAt(QPointF p) const
{
    for (std::size_t i = 0; i < keys_.size(); ++i)
        if (std::abs(8.0 + keys_[i].t * (width() - 16) - p.x()) < 6.0 && p.y() > height() - 18)
            return static_cast<int>(i);
    return -1;
}

void GradientWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect bar(8, 4, width() - 16, height() - 22);
    paintChecker(p, bar);
    for (int x = 0; x < bar.width(); ++x) {
        const float t = static_cast<float>(x) / std::max(1, bar.width() - 1);
        p.fillRect(QRect(bar.left() + x, bar.top(), 1, bar.height()), toColor(engine::evalColorCurve(keys_, t)));
    }
    p.setPen(QColor(20, 20, 20));
    p.drawRect(bar);
    p.setRenderHint(QPainter::Antialiasing);
    for (const auto& k : keys_) {
        const float x = 8.0f + k.t * static_cast<float>(bar.width());
        QPolygonF marker;
        marker << QPointF(x, bar.bottom() + 2) << QPointF(x - 6, height() - 2) << QPointF(x + 6, height() - 2);
        QColor c = toColor(k.value);
        c.setAlpha(255);
        p.setBrush(c);
        p.setPen(QColor(230, 230, 230));
        p.drawPolygon(marker);
    }
}

void GradientWidget::mousePressEvent(QMouseEvent* e)
{
    const int k = keyAt(e->position());
    if (e->button() == Qt::RightButton) {
        if (k >= 0) {
            keys_.erase(keys_.begin() + k);
            update();
            emit edited();
        }
        return;
    }
    if (k >= 0) {
        dragging_ = k;
        return;
    }
    engine::ParticleKey key;
    key.t = tAt(e->position());
    key.value = engine::evalColorCurve(keys_, key.t);
    keys_.push_back(key);
    std::stable_sort(keys_.begin(), keys_.end(), [](const auto& a, const auto& b) { return a.t < b.t; });
    update();
    emit edited();
}

void GradientWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (dragging_ < 0)
        return;
    keys_[static_cast<std::size_t>(dragging_)].t = tAt(e->position());
    const engine::ParticleKey moved = keys_[static_cast<std::size_t>(dragging_)];
    std::stable_sort(keys_.begin(), keys_.end(), [](const auto& a, const auto& b) { return a.t < b.t; });
    dragging_ = static_cast<int>(std::find_if(keys_.begin(), keys_.end(), [&](const auto& k) { return k.t == moved.t && k.value == moved.value; }) - keys_.begin());
    update();
    emit edited();
}

void GradientWidget::mouseReleaseEvent(QMouseEvent*)
{
    dragging_ = -1;
}

void GradientWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    int k = keyAt(e->position());
    if (k < 0) { // the press already added one here
        float best = 1e9f;
        for (std::size_t i = 0; i < keys_.size(); ++i)
            if (const float d = std::abs(keys_[i].t - tAt(e->position())); d < best) {
                best = d;
                k = static_cast<int>(i);
            }
    }
    if (k < 0)
        return;
    const QColor picked = QColorDialog::getColor(toColor(keys_[static_cast<std::size_t>(k)].value), this, tr("Key color"),
                                                 QColorDialog::ShowAlphaChannel);
    if (!picked.isValid())
        return;
    keys_[static_cast<std::size_t>(k)].value = {picked.redF(), picked.greenF(), picked.blueF(), picked.alphaF()};
    update();
    emit edited();
}

// ---------------------------------------------------------------- editor

ParticleEditor::ParticleEditor(QWidget* parent) : QWidget(parent)
{
    auto* split = new QSplitter(this);
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(split);

    // Left: emitters.
    auto* left = new QWidget(split);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(new QLabel(tr("<b>Emitters</b>"), left));
    list_ = new QListWidget(left);
    list_->setToolTip(tr("Every emitter plays at once; untick to mute one"));
    leftLayout->addWidget(list_, 1);
    auto* add = new QPushButton(tr("Add"), left);
    auto* dup = new QPushButton(tr("Duplicate"), left);
    auto* remove = new QPushButton(tr("Remove"), left);
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(add);
    buttons->addWidget(dup);
    buttons->addWidget(remove);
    leftLayout->addLayout(buttons);
    auto* save = new QPushButton(tr("Save"), left);
    auto* revertButton = new QPushButton(tr("Revert"), left);
    auto* fileButtons = new QHBoxLayout();
    fileButtons->addWidget(save);
    fileButtons->addWidget(revertButton);
    leftLayout->addLayout(fileButtons);

    // Middle: preview.
    auto* middle = new QWidget(split);
    auto* midLayout = new QVBoxLayout(middle);
    preview_ = new ParticlePreview(middle);
    midLayout->addWidget(preview_, 1);
    auto* replay = new QPushButton(tr("Restart"), middle);
    stats_ = new QLabel(middle);
    auto* bar = new QHBoxLayout();
    bar->addWidget(replay);
    bar->addWidget(stats_, 1);
    midLayout->addLayout(bar);
    auto* statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, [this] {
        stats_->setText(tr("%1 particles · drag to orbit, wheel to zoom").arg(preview_->particleCount()));
    });
    statsTimer->start(250);

    // Right: settings.
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    form_ = new QWidget(scroll);
    scroll->setWidget(form_);
    auto* form = new QFormLayout(form_);
    auto section = [&](const QString& t) { form->addRow(new QLabel("<b>" + t + "</b>", form_)); };

    name_ = new QLineEdit(form_);
    form->addRow(tr("Name"), name_);

    section(tr("Look"));
    texture_ = new QLineEdit(form_);
    texture_->setReadOnly(true);
    texture_->setPlaceholderText(tr("soft dot"));
    auto* browse = new QPushButton(tr("..."), form_);
    browse->setMaximumWidth(32);
    auto* clearTex = new QPushButton(tr("x"), form_);
    clearTex->setMaximumWidth(28);
    auto* texRow = new QWidget(form_);
    auto* texLayout = new QHBoxLayout(texRow);
    texLayout->setContentsMargins(0, 0, 0, 0);
    texLayout->addWidget(texture_, 1);
    texLayout->addWidget(browse);
    texLayout->addWidget(clearTex);
    form->addRow(tr("Texture"), texRow);
    columns_ = new QSpinBox(form_);
    rows_ = new QSpinBox(form_);
    for (QSpinBox* s : {columns_, rows_}) {
        s->setRange(1, 16);
        s->setKeyboardTracking(false);
    }
    form->addRow(tr("Flipbook cols × rows"), pair(columns_, rows_, form_));
    frameRate_ = number(0, 120, 1, 1, form_, tr("Cells per second; 0 = the whole flipbook once over the particle's life"));
    randomFrame_ = new QCheckBox(tr("Random cell"), form_);
    randomFrame_->setToolTip(tr("Each particle shows one random cell (debris, variants)"));
    form->addRow(tr("Frame rate"), pair(frameRate_, randomFrame_, form_));
    blend_ = new QComboBox(form_);
    blend_->addItems({tr("Alpha (smoke, dust)"), tr("Additive (fire, sparks, glow)")});
    form->addRow(tr("Blend"), blend_);
    lit_ = new QCheckBox(tr("Lit by sun and sky"), form_);
    form->addRow(QString(), lit_);
    stretch_ = new QCheckBox(tr("Stretch along motion"), form_);
    stretchFactor_ = number(0, 1, 0.01, 3, form_, tr("Seconds of travel the streak shows"));
    form->addRow(tr("Streaks"), pair(stretch_, stretchFactor_, form_));

    section(tr("Emission"));
    rate_ = number(0, 10000, 1, 1, form_, tr("Particles per second"));
    form->addRow(tr("Rate /s"), rate_);
    burst_ = new QSpinBox(form_);
    burst_->setRange(0, 10000);
    burst_->setKeyboardTracking(false);
    burst_->setToolTip(tr("Particles at once when the effect starts"));
    form->addRow(tr("Burst"), burst_);
    duration_ = number(0, 600, 0.1, 2, form_, tr("Seconds the emitter runs; 0 = until the effect is stopped (loops)"));
    form->addRow(tr("Duration s"), duration_);
    delay_ = number(0, 60, 0.05, 2, form_);
    form->addRow(tr("Delay s"), delay_);
    max_ = new QSpinBox(form_);
    max_->setRange(1, 10000);
    max_->setKeyboardTracking(false);
    form->addRow(tr("Max particles"), max_);
    shape_ = new QComboBox(form_);
    shape_->addItems({tr("Point"), tr("Sphere"), tr("Box"), tr("Disc")});
    form->addRow(tr("Shape"), shape_);
    shapeSize_ = new Vec3Editor(0, 1000, 0.05, form_);
    shapeSize_->setToolTip(tr("Sphere/disc: X = radius; box: half size (m)"));
    form->addRow(tr("Shape size"), shapeSize_);

    section(tr("Motion"));
    direction_ = new Vec3Editor(-1, 1, 0.1, form_);
    direction_->setToolTip(tr("+Y = up (or out of the surface for impacts, along the barrel for muzzle effects)"));
    form->addRow(tr("Direction"), direction_);
    spread_ = number(0, 180, 1, 1, form_, tr("Cone half-angle in degrees; 180 = all around"));
    form->addRow(tr("Spread °"), spread_);
    speedMin_ = number(0, 500, 0.1, 2, form_);
    speedMax_ = number(0, 500, 0.1, 2, form_);
    form->addRow(tr("Speed m/s"), pair(speedMin_, speedMax_, form_));
    gravity_ = number(-5, 5, 0.05, 2, form_, tr("Multiples of earth gravity; negative = rises (smoke)"));
    form->addRow(tr("Gravity"), gravity_);
    drag_ = number(0, 50, 0.1, 2, form_, tr("Speed lost per second"));
    form->addRow(tr("Drag"), drag_);
    wind_ = new Vec3Editor(-50, 50, 0.1, form_);
    form->addRow(tr("Wind m/s"), wind_);
    attract_ = number(-100, 100, 0.5, 2, form_, tr("m/s² toward the effect's vertical axis; negative pushes away (vortex, anomaly pull)"));
    form->addRow(tr("Attract"), attract_);
    orbit_ = number(-100, 100, 0.5, 2, form_, tr("m/s² around the effect's vertical axis (swirl)"));
    form->addRow(tr("Orbit"), orbit_);
    worldSpace_ = new QCheckBox(tr("World space (stays behind when the effect moves)"), form_);
    form->addRow(QString(), worldSpace_);

    section(tr("Over life"));
    lifeMin_ = number(0.01, 120, 0.1, 2, form_);
    lifeMax_ = number(0.01, 120, 0.1, 2, form_);
    form->addRow(tr("Life s"), pair(lifeMin_, lifeMax_, form_));
    sizeMin_ = number(0.001, 100, 0.01, 3, form_);
    sizeMax_ = number(0.001, 100, 0.01, 3, form_);
    form->addRow(tr("Size m"), pair(sizeMin_, sizeMax_, form_));
    sizeCurve_ = new CurveWidget(form_);
    form->addRow(tr("Size curve"), sizeCurve_);
    color_ = new QPushButton(form_);
    intensity_ = number(0, 100, 0.1, 2, form_, tr("Brightness; above 1 glows with bloom (fire, sparks)"));
    form->addRow(tr("Tint / intensity"), pair(color_, intensity_, form_));
    colorCurve_ = new GradientWidget(form_);
    form->addRow(tr("Color curve"), colorCurve_);
    rotationMin_ = number(-360, 360, 5, 0, form_);
    rotationMax_ = number(-360, 360, 5, 0, form_);
    form->addRow(tr("Rotation °"), pair(rotationMin_, rotationMax_, form_));
    spinMin_ = number(-3600, 3600, 10, 0, form_);
    spinMax_ = number(-3600, 3600, 10, 0, form_);
    form->addRow(tr("Spin °/s"), pair(spinMin_, spinMax_, form_));

    split->addWidget(left);
    split->addWidget(middle);
    split->addWidget(scroll);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    split->setSizes({200, 600, 420});

    // Wiring: every field writes the emitter.
    auto changed = [this] { edited(); };
    connect(name_, &QLineEdit::editingFinished, this, changed);
    for (QSpinBox* s : {columns_, rows_, burst_, max_})
        connect(s, &QSpinBox::valueChanged, this, changed);
    for (QDoubleSpinBox* s : {frameRate_, stretchFactor_, rate_, duration_, delay_, spread_, speedMin_, speedMax_, gravity_, drag_, attract_, orbit_,
                              lifeMin_, lifeMax_, sizeMin_, sizeMax_, intensity_, rotationMin_, rotationMax_, spinMin_, spinMax_})
        connect(s, &QDoubleSpinBox::valueChanged, this, changed);
    for (QCheckBox* c : {randomFrame_, lit_, stretch_, worldSpace_})
        connect(c, &QCheckBox::toggled, this, changed);
    for (QComboBox* c : {blend_, shape_})
        connect(c, &QComboBox::activated, this, changed);
    for (Vec3Editor* v : {shapeSize_, direction_, wind_})
        connect(v, &Vec3Editor::valueEdited, this, changed);
    connect(sizeCurve_, &CurveWidget::edited, this, changed);
    connect(colorCurve_, &GradientWidget::edited, this, changed);
    connect(color_, &QPushButton::clicked, this, [this] {
        engine::ParticleEmitter* e = current();
        if (!e)
            return;
        const QColor picked = QColorDialog::getColor(QColor::fromRgbF(e->color.r, e->color.g, e->color.b), this, tr("Tint"));
        if (!picked.isValid())
            return;
        e->color = {picked.redF(), picked.greenF(), picked.blueF()};
        showEmitter();
        edited();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        engine::ParticleEmitter* e = current();
        if (!e)
            return;
        const QString start = e->texture.empty() ? assetPath("textures/particles") : QFileInfo(toQString(e->texture)).absolutePath();
        const QString file = QFileDialog::getOpenFileName(this, tr("Particle texture"), start, tr("Images (*.png *.jpg *.jpeg *.tga)"));
        if (file.isEmpty())
            return;
        e->texture = toPath(file);
        // Flipbook layout from names like smoke_8x8.png.
        const QRegularExpressionMatch m = QRegularExpression("(\\d+)x(\\d+)").match(QFileInfo(file).completeBaseName());
        if (m.hasMatch()) {
            e->columns = std::clamp(m.captured(1).toInt(), 1, 16);
            e->rows = std::clamp(m.captured(2).toInt(), 1, 16);
        }
        engine::loadParticleTextures(*effect_);
        showEmitter();
        edited();
    });
    connect(clearTex, &QPushButton::clicked, this, [this] {
        if (engine::ParticleEmitter* e = current()) {
            e->texture.clear();
            engine::loadParticleTextures(*effect_);
            showEmitter();
            edited();
        }
    });
    connect(list_, &QListWidget::currentRowChanged, this, [this] { showEmitter(); });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (updating_ || !effect_)
            return;
        const int row = list_->row(item);
        if (row < 0 || row >= static_cast<int>(effect_->emitters.size()))
            return;
        effect_->emitters[static_cast<std::size_t>(row)].enabled = item->checkState() == Qt::Checked;
        modified_ = true;
        emit modificationChanged();
        preview_->restart();
    });
    connect(add, &QPushButton::clicked, this, [this] {
        if (!effect_)
            return;
        engine::ParticleEmitter e;
        e.name = tr("Emitter %1").arg(effect_->emitters.size() + 1).toStdString();
        effect_->emitters.push_back(e);
        structureChanged();
        list_->setCurrentRow(static_cast<int>(effect_->emitters.size()) - 1);
    });
    connect(dup, &QPushButton::clicked, this, [this] {
        engine::ParticleEmitter* e = current();
        if (!e)
            return;
        engine::ParticleEmitter copy = *e;
        copy.name += " copy";
        const int row = list_->currentRow() + 1;
        effect_->emitters.insert(effect_->emitters.begin() + row, copy);
        structureChanged();
        list_->setCurrentRow(row);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (!effect_ || row < 0 || row >= static_cast<int>(effect_->emitters.size()))
            return;
        effect_->emitters.erase(effect_->emitters.begin() + row);
        structureChanged();
        list_->setCurrentRow(std::min(row, static_cast<int>(effect_->emitters.size()) - 1));
    });
    connect(replay, &QPushButton::clicked, preview_, &ParticlePreview::restart);
    connect(save, &QPushButton::clicked, this, [this] {
        QString error;
        if (!this->save(&error))
            QMessageBox::warning(this, tr("Particles"), error);
    });
    connect(revertButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!revert(&error))
            QMessageBox::warning(this, tr("Particles"), error);
    });
}

QString ParticleEditor::title() const
{
    return QFileInfo(path_).completeBaseName() + (modified_ ? "*" : "");
}

engine::ParticleEmitter* ParticleEditor::current()
{
    const int row = list_->currentRow();
    if (!effect_ || row < 0 || row >= static_cast<int>(effect_->emitters.size()))
        return nullptr;
    return &effect_->emitters[static_cast<std::size_t>(row)];
}

bool ParticleEditor::load(const QString& path, QString* error)
{
    path_ = QFileInfo(path).absoluteFilePath();
    std::string e;
    // The shared (cached) effect: edits show at once wherever the effect plays, e.g. the level view.
    effect_ = engine::loadParticleEffect(toPath(path_), &e);
    if (!effect_) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    refreshList();
    list_->setCurrentRow(effect_->emitters.empty() ? -1 : 0);
    preview_->setEffect(effect_);
    modified_ = false;
    emit modificationChanged();
    return true;
}

bool ParticleEditor::save(QString* error)
{
    std::string e;
    if (!effect_ || !engine::saveParticleFile(*effect_, toPath(path_), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    modified_ = false;
    emit modificationChanged();
    return true;
}

bool ParticleEditor::revert(QString* error)
{
    if (!effect_)
        return false;
    engine::ParticleEffect fresh;
    std::string e;
    if (!engine::loadParticleFile(fresh, toPath(path_), &e)) {
        if (error)
            *error = QString::fromStdString(e);
        return false;
    }
    effect_->emitters = std::move(fresh.emitters);
    engine::loadParticleTextures(*effect_);
    refreshList();
    list_->setCurrentRow(effect_->emitters.empty() ? -1 : 0);
    preview_->restart();
    modified_ = false;
    emit modificationChanged();
    return true;
}

void ParticleEditor::refreshList()
{
    updating_ = true;
    const int row = list_->currentRow();
    list_->clear();
    if (effect_)
        for (const engine::ParticleEmitter& e : effect_->emitters) {
            auto* item = new QListWidgetItem(QString::fromStdString(e.name), list_);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
        }
    list_->setCurrentRow(std::min(row, list_->count() - 1));
    updating_ = false;
}

void ParticleEditor::structureChanged()
{
    engine::loadParticleTextures(*effect_);
    refreshList();
    modified_ = true;
    emit modificationChanged();
    preview_->restart();
}

void ParticleEditor::showEmitter()
{
    const engine::ParticleEmitter* e = current();
    form_->setEnabled(e != nullptr);
    if (!e)
        return;
    updating_ = true;
    name_->setText(QString::fromStdString(e->name));
    texture_->setText(e->texture.empty() ? QString() : QDir(assetRoot()).relativeFilePath(toQString(e->texture)));
    columns_->setValue(e->columns);
    rows_->setValue(e->rows);
    frameRate_->setValue(e->frameRate);
    randomFrame_->setChecked(e->randomFrame);
    blend_->setCurrentIndex(e->blend == engine::SpriteBlend::Additive ? 1 : 0);
    lit_->setChecked(e->lit);
    stretch_->setChecked(e->stretch);
    stretchFactor_->setValue(e->stretchFactor);
    rate_->setValue(e->rate);
    burst_->setValue(e->burst);
    duration_->setValue(e->duration);
    delay_->setValue(e->delay);
    max_->setValue(e->maxParticles);
    shape_->setCurrentIndex(static_cast<int>(e->shape));
    shapeSize_->setValue(e->shapeSize);
    direction_->setValue(e->direction);
    spread_->setValue(e->spread);
    speedMin_->setValue(e->speedMin);
    speedMax_->setValue(e->speedMax);
    gravity_->setValue(e->gravity);
    attract_->setValue(e->attract);
    orbit_->setValue(e->orbit);
    drag_->setValue(e->drag);
    wind_->setValue(e->wind);
    worldSpace_->setChecked(e->worldSpace);
    lifeMin_->setValue(e->lifeMin);
    lifeMax_->setValue(e->lifeMax);
    sizeMin_->setValue(e->sizeMin);
    sizeMax_->setValue(e->sizeMax);
    sizeCurve_->setKeys(e->sizeCurve);
    color_->setStyleSheet(QString("background-color: %1; min-height: 20px;").arg(QColor::fromRgbF(e->color.r, e->color.g, e->color.b).name()));
    intensity_->setValue(e->intensity);
    colorCurve_->setKeys(e->colorCurve);
    rotationMin_->setValue(e->rotationMin);
    rotationMax_->setValue(e->rotationMax);
    spinMin_->setValue(e->spinMin);
    spinMax_->setValue(e->spinMax);
    stretchFactor_->setEnabled(e->stretch);
    frameRate_->setEnabled(!e->randomFrame);
    updating_ = false;
}

void ParticleEditor::edited()
{
    engine::ParticleEmitter* e = current();
    if (updating_ || !e)
        return;
    e->name = name_->text().toStdString();
    e->columns = columns_->value();
    e->rows = rows_->value();
    e->frameRate = static_cast<float>(frameRate_->value());
    e->randomFrame = randomFrame_->isChecked();
    e->blend = blend_->currentIndex() == 1 ? engine::SpriteBlend::Additive : engine::SpriteBlend::Alpha;
    e->lit = lit_->isChecked();
    e->stretch = stretch_->isChecked();
    e->stretchFactor = static_cast<float>(stretchFactor_->value());
    e->rate = static_cast<float>(rate_->value());
    e->burst = burst_->value();
    e->duration = static_cast<float>(duration_->value());
    e->delay = static_cast<float>(delay_->value());
    e->maxParticles = max_->value();
    e->shape = static_cast<engine::EmitterShape>(shape_->currentIndex());
    e->shapeSize = shapeSize_->value();
    e->direction = direction_->value();
    e->spread = static_cast<float>(spread_->value());
    e->speedMin = static_cast<float>(speedMin_->value());
    e->speedMax = static_cast<float>(std::max(speedMin_->value(), speedMax_->value()));
    e->gravity = static_cast<float>(gravity_->value());
    e->attract = static_cast<float>(attract_->value());
    e->orbit = static_cast<float>(orbit_->value());
    e->drag = static_cast<float>(drag_->value());
    e->wind = wind_->value();
    e->worldSpace = worldSpace_->isChecked();
    e->lifeMin = static_cast<float>(lifeMin_->value());
    e->lifeMax = static_cast<float>(std::max(lifeMin_->value(), lifeMax_->value()));
    e->sizeMin = static_cast<float>(sizeMin_->value());
    e->sizeMax = static_cast<float>(std::max(sizeMin_->value(), sizeMax_->value()));
    e->sizeCurve = sizeCurve_->keys();
    e->intensity = static_cast<float>(intensity_->value());
    e->colorCurve = colorCurve_->keys();
    e->rotationMin = static_cast<float>(rotationMin_->value());
    e->rotationMax = static_cast<float>(std::max(rotationMin_->value(), rotationMax_->value()));
    e->spinMin = static_cast<float>(spinMin_->value());
    e->spinMax = static_cast<float>(std::max(spinMin_->value(), spinMax_->value()));
    if (QListWidgetItem* item = list_->currentItem(); item && item->text() != name_->text()) {
        const QSignalBlocker block(list_);
        item->setText(name_->text());
    }
    stretchFactor_->setEnabled(e->stretch);
    frameRate_->setEnabled(!e->randomFrame);
    if (!modified_) {
        modified_ = true;
        emit modificationChanged();
    }
}
