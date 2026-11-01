#include "Viewport.h"
#include <engine/Profile.h>
#include <engine/EntityIO.h>

#include <engine/Anomaly.h>
#include <engine/Lights.h>
#include <engine/Water.h>

#include <filesystem>

#include <engine/Geometry.h>
#include <engine/Gizmo.h>

#include <QDragEnterEvent>
#include <QGuiApplication>
#include <QDropEvent>
#include <QDir>
#include <QMimeData>
#include <QUrl>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QApplication>
#include <QPointer>
#include <engine/JobSystem.h>
#include "AssetPaths.h"
#include <glm/gtc/matrix_transform.hpp>
#include <QOpenGLFunctions>
#include <QWheelEvent>

#include <algorithm>
#include <optional>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
    setMouseTracking(true); // gizmo handles light up under the cursor
    camera_.position = {0.0f, 12.0f, 22.0f};
    camera_.yawDegrees = -90.0f;
    camera_.pitchDegrees = -28.0f;
    camera_.fovDegrees = 70.0f;
    camera_.sensitivity = 0.2f;
    connect(&timer_, &QTimer::timeout, this, &Viewport::tick);
    // Repaint ~30 times a second while the level has something animated (effects, rain).
    connect(&particleTimer_, &QTimer::timeout, this, [this] {
        if (isVisible() && (!liveEffects_.empty() || scene_->environment.rain > 0.0f ||
                            std::any_of(scene_->entities.begin(), scene_->entities.end(),
                                        [](const engine::Entity& e) {
                                            return e.type == "particle" || e.type == "anomaly" ||
                                                   (e.type == "light" && e.properties.count("flicker"));
                                        })))
            update();
    });
    particleTimer_.start(33);
    particleClock_.start();

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
    connect(&brushTimer_, &QTimer::timeout, this, [this] {
        if (painting_ && brushHover_)
            emit brushStroke(brushPoint_, QGuiApplication::keyboardModifiers(), false);
    });
}

Viewport::~Viewport()
{
    cleanup();
}

void Viewport::bakeStep(engine::ProbeBaker& baker, double budgetMs)
{
    if (!ready_ || !scene_)
        return;
    makeCurrent();
    baker.step(renderer_, *scene_, budgetMs);
    doneCurrent();
}

void Viewport::bakeStop(engine::ProbeBaker& baker)
{
    if (ready_)
        baker.stop(renderer_);
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

void Viewport::setShowReference(bool on)
{
    showReference_ = on;
    if (on && !reference_ && !referenceLoading_) {
        // Loaded on a worker thread; the view shows it once it is ready.
        referenceLoading_ = true;
        QPointer<Viewport> self(this);
        const std::filesystem::path path(assetPath("models/characters/humans/stalker.glb").toStdU16String());
        engine::JobSystem::global().run([self, path] {
            auto model = std::make_shared<engine::Model>();
            if (!engine::loadModelFile(*model, path))
                model.reset();
            QMetaObject::invokeMethod(qApp, [self, model] {
                if (!self)
                    return;
                self->reference_ = model;
                self->update();
            });
        });
    }
    update();
}

// Lights: the selected light's reach (point: three circles of its range; spot:
// the cone out to its range) and a short axis line on every spot light.
void Viewport::addLightHelpers(engine::RenderView& view) const
{
    lightHelpers_ = engine::DebugGeometry{};
    lightHelpers_.lineColors.clear();
    auto line = [this](glm::vec3 a, glm::vec3 b, glm::vec4 c) {
        lightHelpers_.lines.push_back(a);
        lightHelpers_.lines.push_back(b);
        lightHelpers_.lineColors.push_back(c);
        lightHelpers_.lineColors.push_back(c);
    };
    auto circle = [&](glm::vec3 center, glm::vec3 u, glm::vec3 v, float r, glm::vec4 c) {
        const int n = 48;
        for (int k = 0; k < n; ++k) {
            const float a0 = 6.2831853f * static_cast<float>(k) / n, a1 = 6.2831853f * static_cast<float>(k + 1) / n;
            line(center + (u * std::cos(a0) + v * std::sin(a0)) * r, center + (u * std::cos(a1) + v * std::sin(a1)) * r, c);
        }
    };
    for (std::size_t i = 0; i < scene_->entities.size(); ++i) {
        const engine::Entity& e = scene_->entities[i];
        if (e.type != "light")
            continue;
        const engine::LightSettings s = engine::lightSettings(e);
        const bool selected = selected_ == scene_->firstEntity() + static_cast<int>(i);
        const glm::vec4 color(glm::mix(e.color, glm::vec3(1.0f), 0.3f), selected ? 0.9f : 0.5f);
        const float yaw = glm::radians(e.yawDegrees), pitch = glm::radians(s.pitch);
        const glm::vec3 dir(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
        if (s.type == engine::LightType::Spot)
            line(e.position, e.position + dir * std::min(1.0f, s.range), color);
        if (!selected)
            continue;
        if (s.type == engine::LightType::Point) {
            circle(e.position, {1, 0, 0}, {0, 0, 1}, s.range, color);
            circle(e.position, {1, 0, 0}, {0, 1, 0}, s.range, color);
            circle(e.position, {0, 1, 0}, {0, 0, 1}, s.range, color);
        } else {
            const glm::vec3 u = glm::normalize(glm::cross(dir, std::abs(dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
            const glm::vec3 v = glm::cross(dir, u);
            for (float angle : {s.outerAngle, s.innerAngle}) {
                const float a = glm::radians(angle);
                const float r = s.range * std::sin(a), d = s.range * std::cos(a);
                const glm::vec4 c = angle == s.outerAngle ? color : glm::vec4(glm::vec3(color), color.a * 0.45f);
                circle(e.position + dir * d, u, v, r, c);
                for (int k = 0; k < 4; ++k) {
                    const float t = 1.5707963f * static_cast<float>(k);
                    line(e.position, e.position + dir * d + (u * std::cos(t) + v * std::sin(t)) * r, c);
                }
            }
        }
    }
    if (!lightHelpers_.lines.empty())
        view.debug.push_back(&lightHelpers_);
}

void Viewport::addIoHelpers(engine::RenderView& view) const
{
    ioHelpers_ = engine::DebugGeometry{};
    ioArrows_ = engine::DebugGeometry{};
    ioArrows_.xray = true;
    auto lineTo = [](engine::DebugGeometry& g, glm::vec3 a, glm::vec3 b, glm::vec4 c) {
        g.lines.push_back(a);
        g.lines.push_back(b);
        g.lineColors.push_back(c);
        g.lineColors.push_back(c);
    };
    auto line = [&](glm::vec3 a, glm::vec3 b, glm::vec4 c) { lineTo(ioHelpers_, a, b, c); };
    auto arrow = [&](glm::vec3 a, glm::vec3 b, glm::vec4 c) {
        auto line = [&](glm::vec3 p, glm::vec3 q, glm::vec4 col) { lineTo(ioArrows_, p, q, col); };
        line(a, b, c);
        const glm::vec3 d = b - a;
        const float len = glm::length(d);
        if (len < 0.05f)
            return;
        const glm::vec3 dir = d / len;
        const glm::vec3 side = glm::normalize(glm::cross(dir, std::abs(dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
        const float head = std::min(0.4f, len * 0.25f);
        line(b, b - dir * head + side * head * 0.5f, c);
        line(b, b - dir * head - side * head * 0.5f, c);
    };
    const int first = scene_->firstEntity();
    auto centre = [](const engine::Entity& e) {
        const engine::Aabb m = e.markerBounds();
        return (m.min + m.max) * 0.5f;
    };
    for (std::size_t i = 0; i < scene_->entities.size(); ++i) {
        const engine::Entity& e = scene_->entities[i];
        if (e.type != "trigger" && e.type != "reverb" && e.type != "ambience" && e.type != "water" && e.type != "interior" &&
            e.type != "reflection_probe" && e.type != "irradiance_volume") // volumes: triggers, zones, water
            continue;
        const bool selected = selected_ == first + static_cast<int>(i);
        const glm::vec4 c = selected ? glm::vec4(1.0f, 0.75f, 0.2f, 0.95f) : glm::vec4(e.type == "water" ? glm::vec3(0.3f, 0.7f, 1.0f) : e.color, 0.6f);
        if (e.type == "water") { // turned box: the water volume
            const engine::WaterBody w = engine::waterBody(e);
            glm::vec3 q[8];
            for (int k = 0; k < 4; ++k) {
                const glm::vec2 corner(k == 1 || k == 2 ? 0.5f : -0.5f, k >= 2 ? 0.5f : -0.5f);
                q[k + 4] = glm::vec3(w.surface * glm::vec4(corner.x, 0.0f, corner.y, 1.0f));
                q[k] = q[k + 4] - glm::vec3(0.0f, w.level - w.bottom, 0.0f);
            }
            for (int k = 0; k < 4; ++k) {
                line(q[k], q[(k + 1) % 4], c);
                line(q[k + 4], q[(k + 1) % 4 + 4], c);
                line(q[k], q[k + 4], c);
            }
            if (glm::length(w.flow) > 0.0f) { // flow direction on the surface
                const glm::vec3 mid(w.surface * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                arrow(mid, mid + glm::vec3(w.flow.x, 0.0f, w.flow.y) / glm::length(w.flow) * 2.0f, c);
            }
            continue;
        }
        if (e.type == "interior" || e.type == "reflection_probe" || e.type == "irradiance_volume") { // turned box like the entity
            glm::mat4 m = glm::translate(glm::mat4(1.0f), e.position);
            m = glm::rotate(m, -glm::radians(e.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 q[8];
            for (int k = 0; k < 8; ++k) {
                const glm::vec3 corner((k & 3) == 1 || (k & 3) == 2 ? 0.5f : -0.5f, k >= 4 ? 1.0f : 0.0f, (k & 3) >= 2 ? 0.5f : -0.5f);
                q[k] = glm::vec3(m * glm::vec4(corner * e.size, 1.0f));
            }
            for (int k = 0; k < 4; ++k) {
                line(q[k], q[(k + 1) % 4], c);
                line(q[k + 4], q[(k + 1) % 4 + 4], c);
                line(q[k], q[k + 4], c);
            }
            continue;
        }
        const engine::Aabb b = e.bounds();
        const glm::vec3 p[8] = {{b.min.x, b.min.y, b.min.z}, {b.max.x, b.min.y, b.min.z}, {b.max.x, b.min.y, b.max.z}, {b.min.x, b.min.y, b.max.z},
                                {b.min.x, b.max.y, b.min.z}, {b.max.x, b.max.y, b.min.z}, {b.max.x, b.max.y, b.max.z}, {b.min.x, b.max.y, b.max.z}};
        for (int k = 0; k < 4; ++k) {
            line(p[k], p[(k + 1) % 4], c);
            line(p[k + 4], p[(k + 1) % 4 + 4], c);
            line(p[k], p[k + 4], c);
        }
    }
    const int sel = selected_ - first;
    if (selected_ >= first && sel < static_cast<int>(scene_->entities.size())) {
        const engine::Entity& s = scene_->entities[static_cast<std::size_t>(sel)];
        for (const engine::EntityOutput& o : s.outputs) // to its targets
            for (int t : engine::findEntitiesByName(*scene_, o.target == "!self" ? s.name : o.target))
                arrow(centre(s), centre(scene_->entities[static_cast<std::size_t>(t)]), {1.0f, 0.85f, 0.2f, 0.95f});
        for (const engine::Entity& other : scene_->entities) // from the entities that target it
            for (const engine::EntityOutput& o : other.outputs)
                if (o.target == s.name && &other != &s) {
                    arrow(centre(other), centre(s), {0.3f, 0.85f, 1.0f, 0.95f});
                    break;
                }
    }
    if (!ioHelpers_.lines.empty())
        view.debug.push_back(&ioHelpers_);
    if (!ioArrows_.lines.empty())
        view.debug.push_back(&ioArrows_);
}

// Anomaly fields: rings at the bottom and top of the field (and the vent's fire
// column), in the anomaly's color; in the perspective view also its idle warp.
void Viewport::addAnomalyHelpers(engine::RenderView& view, bool warp) const
{
    for (std::size_t i = 0; i < scene_->entities.size(); ++i) {
        const engine::Entity& e = scene_->entities[i];
        if (e.type != "anomaly")
            continue;
        const engine::AnomalySettings cfg = engine::anomalySettings(e);
        const float r = std::max(0.1f, std::max(e.size.x, e.size.z) * 0.5f);
        const bool selected = selected_ == scene_->firstEntity() + static_cast<int>(i);
        const glm::vec3 color = selected ? glm::vec3(1.0f, 0.85f, 0.3f) : e.color;
        const int n = std::clamp(static_cast<int>(r * 12.0f), 16, 96);
        const float s = std::clamp(r * 0.025f, 0.04f, 0.15f);
        std::vector<float> heights = {0.02f, e.size.y};
        if (cfg.kind == engine::AnomalyKind::Vent)
            heights.push_back(cfg.get("column"));
        for (float h : heights)
            for (int k = 0; k < n; ++k) {
                const float a = 6.2831853f * static_cast<float>(k) / static_cast<float>(n);
                view.worldBoxes.push_back({e.position + glm::vec3(std::cos(a) * r, h, std::sin(a) * r), glm::vec3(s), color, true});
            }
        if (cfg.kind == engine::AnomalyKind::Crusher) // core
            for (int k = 0; k < 16; ++k) {
                const float a = 6.2831853f * static_cast<float>(k) / 16.0f, c = cfg.get("core");
                view.worldBoxes.push_back({e.position + glm::vec3(std::cos(a) * c, 0.05f, std::sin(a) * c), glm::vec3(s * 0.8f),
                                           glm::vec3(1.0f, 0.3f, 0.3f), true});
            }
        if (!warp)
            continue;
        engine::ScreenDistortion d;
        d.position = e.position + glm::vec3(0.0f, std::min(1.0f, e.size.y * 0.5f), 0.0f);
        d.radius = r;
        switch (cfg.kind) {
        case engine::AnomalyKind::Crusher: d.strength = 0.45f; d.lens = true; break;
        case engine::AnomalyKind::Rift: d.strength = 0.55f; d.lens = true; break;
        case engine::AnomalyKind::Vent:
            d.strength = 0.3f;
            d.radius = std::max(r, 1.2f) * 1.3f;
            d.position = e.position + glm::vec3(0.0f, d.radius * 0.8f, 0.0f);
            break;
        case engine::AnomalyKind::Acid: d.strength = 0.12f; break;
        default: d.strength = 0.0f; break;
        }
        if (d.strength > 0.0f)
            view.distortions.push_back(d);
    }
}

void Viewport::syncParticles(float dt)
{
    // Match the running effects to the level's "particle" entities (added, moved, removed, effect changed).
    std::vector<LiveEffect> next;
    for (const engine::Entity& e : scene_->entities) {
        if (e.type != "particle" && e.type != "anomaly")
            continue;
        std::string effectRel;
        glm::mat3 yaw(glm::rotate(glm::mat4(1.0f), glm::radians(-e.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)));
        if (e.type == "anomaly") { // idle look of the anomaly, stretched over its field like in the game
            effectRel = engine::anomalySettings(e).idleEffect;
            const float r = std::max(0.1f, std::max(e.size.x, e.size.z) * 0.5f);
            yaw = glm::mat3(glm::vec3(r, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, r));
        } else if (const auto it = e.properties.find("effect"); it != e.properties.end()) {
            effectRel = it->second;
        }
        if (effectRel.empty())
            continue;
        const std::filesystem::path file =
            std::filesystem::path(QDir(assetRoot()).absoluteFilePath(QString::fromStdString(effectRel)).toStdU16String()).lexically_normal();
        auto live = std::find_if(liveEffects_.begin(), liveEffects_.end(),
                                 [&](const LiveEffect& l) { return l.entity == e.name && l.file == file; });
        // A finished one-shot effect (an impact) starts again, so it can be seen while placing it.
        if (live != liveEffects_.end() && particles_.alive(live->handle)) {
            particles_.setTransform(live->handle, e.position, yaw);
            next.push_back(*live);
            live->handle = 0; // kept
        } else {
            auto fx = engine::loadParticleEffect(file);
            next.push_back({e.name, file, fx ? particles_.spawn(fx, e.position, yaw) : 0});
            if (e.type == "anomaly" && next.back().handle)
                particles_.setRateScale(next.back().handle, std::clamp(yaw[0][0] * yaw[0][0], 0.5f, 12.0f));
        }
    }
    for (const LiveEffect& l : liveEffects_)
        if (l.handle)
            particles_.kill(l.handle);
    liveEffects_ = std::move(next);
    // Rain around the camera, as much as Environment > Weather > Rain says.
    if (scene_->environment.rain > 0.0f) {
        if (!particles_.alive(rain_)) {
            const std::filesystem::path file = scene_->environment.rainEffect.empty()
                                                   ? std::filesystem::path(assetPath("particles/rain.particle").toStdU16String())
                                                   : scene_->environment.rainEffect;
            rain_ = particles_.spawn(engine::loadParticleEffect(file), camera_.position);
        }
        particles_.setTransform(rain_, camera_.position + glm::vec3(0.0f, 9.0f, 0.0f));
        particles_.setRateScale(rain_, scene_->environment.rain);
    } else if (rain_) {
        particles_.kill(rain_);
        rain_ = 0;
    }
    particles_.update(dt);
}

void Viewport::paintGL()
{
    if (!ready_)
        return;
    ENGINE_FRAME_MARK; // the editor draws on demand: one Tracy frame per repaint
    ENGINE_ZONE("Viewport::paintGL");
    const qreal dpr = devicePixelRatioF();
    while (context()->functions()->glGetError() != GL_NO_ERROR) {
    } // errors left by Qt's own drawing are not ours
    {
        const qint64 now = particleClock_.elapsed();
        const float dt = std::clamp(static_cast<float>(now - particleLast_) / 1000.0f, 0.0f, 0.1f);
        particleLast_ = now;
        syncParticles(dt);
    }
    const std::vector<Pane> list = panes();
    for (const Pane& pane : list) {
        paneMode_ = pane.mode;
        paneRect_ = pane.rect;
        engine::RenderView view;
        view.viewportWidth = std::max(1, static_cast<int>(pane.rect.width() * dpr));
        view.viewportHeight = std::max(1, static_cast<int>(pane.rect.height() * dpr));
        view.viewportX = static_cast<int>(pane.rect.x() * dpr);
        view.time = static_cast<float>(particleClock_.elapsed()) / 1000.0f; // light flicker, clouds
        view.viewportY = static_cast<int>((height() - pane.rect.bottom()) * dpr); // GL origin is bottom left
        const float aspect = static_cast<float>(view.viewportWidth) / static_cast<float>(view.viewportHeight);
        view.view = paneView(pane.mode);
        view.projection = paneProjection(pane.mode, aspect);
        view.cameraPosition = paneEye(pane.mode);
        view.fog = pane.mode == ViewMode::Perspective;
        // Orthographic views are wireframe in Lit mode (like most level editors); other modes apply everywhere.
        view.shading = pane.mode != ViewMode::Perspective && shading_ == engine::Shading::Lit && orthoWireframe_
                           ? engine::Shading::Wireframe
                           : shading_;
        view.selectedObject = selected_;
        view.drawPlayerStart = true;
        view.editorHelpers = true;
        view.grid = showGrid_;
        view.gridAxis = pane.mode == ViewMode::Front ? 2 : (pane.mode == ViewMode::Side ? 0 : 1);
        view.gridFade = pane.mode == ViewMode::Perspective ? 150.0f : orthoSize_ * 8.0f;
        // Reference human (1.8 m) beside the player start, to judge the scale of assets.
        if (showReference_ && reference_)
            view.extraModels.emplace_back(reference_.get(),
                                          glm::translate(glm::mat4(1.0f), scene_->playerStart + glm::vec3(1.0f, 0.0f, 0.0f)));
        view.gizmoBoxes = gizmoBoxes();
        if (debug_)
            view.debug.push_back(debug_);
        if (overlay_)
            view.debug.push_back(overlay_);
        if (brush_ && brushHover_ && brushModel_ >= 0) {
            // Mesh paint: a ring facing the camera around the cursor on the model.
            const glm::vec3 forward = glm::normalize(brushPoint_ - view.cameraPosition);
            const glm::vec3 side = glm::normalize(glm::cross(forward, std::abs(forward.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
            const glm::vec3 up = glm::cross(side, forward);
            const float s = std::clamp(brushRadius_ * 0.03f, 0.01f, 0.3f);
            for (int k = 0; k < 40; ++k) {
                const float a = 6.2831853f * static_cast<float>(k) / 40.0f;
                const glm::vec3 p = brushPoint_ - forward * 0.02f + (side * std::cos(a) + up * std::sin(a)) * brushRadius_;
                view.worldBoxes.push_back({p, glm::vec3(s), brushColor_, true});
            }
            view.worldBoxes.push_back({brushPoint_ - forward * 0.02f, glm::vec3(s * 1.5f), brushColor_, true});
        } else if (brush_ && brushHover_ && scene_->terrain) {
            // Brush ring: small markers on the ground around the cursor.
            const int n = 48;
            const float s = std::clamp(brushRadius_ * 0.03f, 0.06f, 0.6f);
            for (int k = 0; k < n; ++k) {
                const float a = 6.2831853f * static_cast<float>(k) / static_cast<float>(n);
                glm::vec3 p = brushPoint_ + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * brushRadius_;
                p.y = scene_->terrain->heightAt(p.x, p.z) + s * 0.5f;
                view.worldBoxes.push_back({p, glm::vec3(s), brushColor_, true});
            }
            view.worldBoxes.push_back({brushPoint_ + glm::vec3(0.0f, s, 0.0f), glm::vec3(s * 1.5f), brushColor_, true});
        }
        if (pane.mode == ViewMode::Perspective && shading_ != engine::Shading::Wireframe)
            particles_.appendSprites(view.cameraPosition, glm::mat3(glm::inverse(view.view)), view.sprites);
        addAnomalyHelpers(view, pane.mode == ViewMode::Perspective && shading_ != engine::Shading::Wireframe);
        addLightHelpers(view);
        addIoHelpers(view);
        renderer_.render(*scene_, view);
    }
    // Report driver errors instead of silently showing a broken view.
    if (const GLenum error = context()->functions()->glGetError(); error != GL_NO_ERROR && error != lastGlError_) {
        lastGlError_ = error;
        emit renderWarning(tr("OpenGL error 0x%1 while drawing the viewport (tool %2, object %3)")
                               .arg(error, 0, 16).arg(static_cast<int>(tool_)).arg(selected_));
    }
    // Pane names and borders.
    QPainter painter(this);
    const char* names[4] = {"Perspective", "Top", "Front", "Side"};
    for (const Pane& pane : list) {
        painter.setPen(QColor(230, 232, 236));
        painter.drawText(pane.rect.adjusted(8, 6, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr(names[static_cast<int>(pane.mode)]));
        if (list.size() > 1) {
            painter.setPen(QPen(QColor(30, 31, 34), 2));
            painter.drawRect(pane.rect);
        }
    }
    // The far shadow cascades are refreshed every few frames: draw a few more after each
    // change so a static view does not keep a stale shadow.
    if (!settling_ && quality_.shadowCache)
        settleFrames_ = 4;
    settling_ = false;
    if (settleFrames_ > 0) {
        --settleFrames_;
        QTimer::singleShot(0, this, [this] { settling_ = true; update(); });
    }
}

std::vector<Viewport::Pane> Viewport::panes() const
{
    const QRectF all(0, 0, width(), height());
    if (!quad_)
        return {{mode_, all}};
    const qreal w = width() * 0.5, h = height() * 0.5;
    return {{ViewMode::Top, QRectF(0, 0, w, h)},
            {ViewMode::Perspective, QRectF(w, 0, width() - w, h)},
            {ViewMode::Front, QRectF(0, h, w, height() - h)},
            {ViewMode::Side, QRectF(w, h, width() - w, height() - h)}};
}

void Viewport::selectPane(QPointF pos) const
{
    for (const Pane& pane : panes())
        if (pane.rect.contains(pos) || !quad_) {
            paneMode_ = pane.mode;
            paneRect_ = pane.rect;
            return;
        }
}

glm::vec3 Viewport::paneEye(ViewMode mode) const
{
    switch (mode) {
    case ViewMode::Top: return orthoCenter_ + glm::vec3(0.0f, 200.0f, 0.0f);
    case ViewMode::Front: return orthoCenter_ + glm::vec3(0.0f, 0.0f, 200.0f);
    case ViewMode::Side: return orthoCenter_ + glm::vec3(200.0f, 0.0f, 0.0f);
    default: return camera_.position;
    }
}

glm::mat4 Viewport::paneView(ViewMode mode) const
{
    if (mode == ViewMode::Perspective)
        return camera_.view();
    const glm::vec3 up = mode == ViewMode::Top ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::lookAt(paneEye(mode), orthoCenter_, up);
}

glm::mat4 Viewport::paneProjection(ViewMode mode, float aspect) const
{
    if (mode == ViewMode::Perspective)
        return camera_.projection(aspect);
    const float h = orthoSize_, w = orthoSize_ * aspect;
    return glm::ortho(-w, w, -h, h, 1.0f, 450.0f);
}

void Viewport::setViewMode(ViewMode mode)
{
    mode_ = mode;
    quad_ = false;
    update();
}

void Viewport::setQuadView(bool on)
{
    quad_ = on;
    update();
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
    orthoCenter_ = center;
    orthoSize_ = std::max(2.0f, glm::length(bounds.max - bounds.min) * 0.8f);
    update();
}

void Viewport::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    lastMouse_ = e->position();
    selectPane(e->position());
    const bool ortho = paneMode_ != ViewMode::Perspective;
    if (e->button() == Qt::RightButton && ortho) {
        panning_ = true; // orthographic views pan instead of flying
        setCursor(Qt::ClosedHandCursor);
    } else if (e->button() == Qt::RightButton) {
        flying_ = true;
        setCursor(Qt::BlankCursor);
        clock_.start();
        timer_.start(16);
    } else if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        setCursor(Qt::ClosedHandCursor);
    } else if (e->button() == Qt::LeftButton && brush_) {
        glm::vec3 p;
        brushHover_ = brushHit(e->position(), p);
        if (brushHover_) {
            brushPoint_ = p;
            painting_ = true;
            emit brushStroke(p, e->modifiers(), true);
            brushTimer_.start(33);
        } else if (brushModel_ >= 0) {
            // Mesh paint: a click beside the painted model selects another one.
            glm::vec3 origin, dir;
            rayAt(e->position(), origin, dir);
            emit boxPicked(engine::pickObject(*scene_, origin, dir));
        }
    } else if (e->button() == Qt::LeftButton) {
        glm::vec3 origin, dir;
        rayAt(e->position(), origin, dir);
        // 1) A gizmo handle of the selected object.
        int axis = handleAt(e->position());
        const int hit = engine::pickObject(*scene_, origin, dir);
        // 2) The selected object itself: free move on the ground plane / yaw / uniform scale.
        if (axis < 0 && tool_ != Tool::Select && hit >= 0 && hit == selected_)
            axis = 3;
        if (axis >= 0) {
            drag_ = Drag{};
            drag_.active = true;
            drag_.axis = axis;
            drag_.startMouse = e->position();
            drag_.lastMouse = e->position();
            const engine::Aabb b = scene_->objectBounds(selected_);
            drag_.startPosition = tool_ == Tool::Move ? (b.min + b.max) * 0.5f : gizmoCenter();
            drag_.startPosition = gizmoCenter();
            if (auto t = engine::rayAabb(origin, dir, b))
                drag_.anchor = origin + dir * *t;
            else
                drag_.anchor = gizmoCenter();
            if (tool_ == Tool::Rotate && axis < 3) {
                const auto a = engine::ringAngle(origin, dir, drag_.startPosition, engine::gizmoAxis(axis));
                drag_.haveAngle = a.has_value();
                drag_.lastAngle = a.value_or(0.0f);
            } else if (axis < 3) {
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
    if (panning_ && paneMode_ != ViewMode::Perspective) {
        // Orthographic pan: the grabbed point stays under the cursor.
        const float perPixel = 2.0f * orthoSize_ / static_cast<float>(std::max(1.0, paneRect_.height()));
        const glm::mat4 v = paneView(paneMode_);
        const glm::vec3 right(v[0][0], v[1][0], v[2][0]), up(v[0][1], v[1][1], v[2][1]);
        orthoCenter_ += (-right * static_cast<float>(delta.x()) + up * static_cast<float>(delta.y())) * perPixel;
        update();
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
    if (!flying_ && brush_) {
        glm::vec3 p;
        const bool hover = brushHit(e->position(), p);
        if (hover || hover != brushHover_) {
            brushHover_ = hover;
            brushPoint_ = hover ? p : brushPoint_;
            update();
        }
        return;
    }
    if (!flying_) {
        if (tool_ != Tool::Select && selected_ >= 0) {
            const int axis = handleAt(e->position());
            if (axis != hoverAxis_) {
                hoverAxis_ = axis;
                setCursor(axis >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
                update();
            }
        }
        return;
    }
    camera_.look({static_cast<float>(delta.x()), static_cast<float>(delta.y())});
    update();
}

void Viewport::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton && panning_) {
        panning_ = false;
        unsetCursor();
    } else if (e->button() == Qt::RightButton) {
        flying_ = false;
        unsetCursor();
        timer_.stop();
        keys_.clear();
    } else if (e->button() == Qt::MiddleButton) {
        panning_ = false;
        unsetCursor();
    } else if (e->button() == Qt::LeftButton && painting_) {
        painting_ = false;
        brushTimer_.stop();
        emit brushFinished();
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
    selectPane(e->position());
    if (paneMode_ != ViewMode::Perspective) {
        // Zoom toward the cursor.
        glm::vec3 before, dir;
        rayAt(e->position(), before, dir);
        orthoSize_ = std::clamp(orthoSize_ * std::pow(0.85f, notches), 0.5f, 400.0f);
        glm::vec3 after;
        rayAt(e->position(), after, dir);
        glm::vec3 shift = before - after;
        shift -= dir * glm::dot(shift, dir);
        orthoCenter_ += shift;
        update();
        return;
    }
    if (flying_) {
        flySpeed_ = std::clamp(flySpeed_ * std::pow(1.2f, notches), 1.0f, 200.0f);
        emit cameraSpeedChanged(flySpeed_);
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
    // Unproject the position into a world-space ray, in the pane under the cursor
    // (the pane stays fixed while dragging or panning).
    if (!drag_.active && !panning_)
        selectPane(pos);
    const QRectF r = paneRect_.isEmpty() ? QRectF(0, 0, width(), height()) : paneRect_;
    const float x = static_cast<float>(2.0 * (pos.x() - r.x()) / r.width() - 1.0);
    const float y = static_cast<float>(1.0 - 2.0 * (pos.y() - r.y()) / r.height());
    const float aspect = static_cast<float>(r.width() / std::max(1.0, r.height()));
    const glm::mat4 inv = glm::inverse(paneProjection(paneMode_, aspect) * paneView(paneMode_));
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
    if (paneMode_ != ViewMode::Perspective)
        return orthoSize_ * 0.2f;
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
    const int lit = active ? drag_.axis : hoverAxis_; // grabbed or under the cursor
    auto col = [&](int axis) { return lit == axis ? glm::vec3(1.0f, 0.9f, 0.2f) : colors[axis]; };
    if (tool_ == Tool::Rotate) {
        // One ring per axis (X red, Y green, Z blue); grab a ring and drag around it.
        for (int axis = 0; axis < 3; ++axis) {
            const float thick = t * (lit == axis ? 2.6f : 2.0f);
            for (const glm::vec3& p : engine::ringPoints(c, engine::gizmoAxis(axis), len, 80))
                boxes.push_back({p, glm::vec3(thick), col(axis), true});
        }
        boxes.push_back({c, glm::vec3(t * 3.0f), lit == 3 ? glm::vec3(1.0f, 0.9f, 0.2f) : glm::vec3(0.9f), true});
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
    boxes.push_back({c, glm::vec3(t * 4.0f), lit == 3 ? glm::vec3(1.0f, 0.9f, 0.2f) : glm::vec3(0.9f), true});
    return boxes;
}

int Viewport::handleAt(QPointF pos, float* distance) const
{
    if (tool_ == Tool::Select || selected_ < 0 || selected_ >= scene_->objectCount())
        return -1;
    glm::vec3 origin, dir;
    rayAt(pos, origin, dir);
    int axis = -1;
    float best = 1e30f;
    if (tool_ == Tool::Rotate) {
        const glm::vec3 c = gizmoCenter();
        const float len = gizmoSize();
        for (int a = 0; a < 3; ++a)
            if (auto t = engine::pickRing(origin, dir, c, engine::gizmoAxis(a), len, len * 0.1f); t && *t < best) {
                best = *t;
                axis = a;
            }
    } else {
        for (const Handle& h : gizmoHandles())
            if (auto t = engine::rayAabb(origin, dir, h.pick); t && *t < best) {
                best = *t;
                axis = h.axis;
            }
    }
    if (distance)
        *distance = best;
    return axis;
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
        // A ring follows the cursor around its axis; the object itself (or a ring
        // seen edge-on) turns with the horizontal mouse movement.
        const int axis = drag_.axis < 3 ? drag_.axis : 1;
        std::optional<float> a;
        if (drag_.axis < 3) {
            glm::vec3 origin, dir;
            rayAt(pos, origin, dir);
            a = engine::ringAngle(origin, dir, drag_.startPosition, engine::gizmoAxis(axis));
        }
        if (a && drag_.haveAngle)
            drag_.totalAngle += engine::wrapAngle(*a - drag_.lastAngle);
        else if (!a)
            drag_.totalAngle += glm::radians(static_cast<float>(pos.x() - drag_.lastMouse.x()) * 0.5f);
        drag_.haveAngle = a.has_value();
        drag_.lastAngle = a.value_or(0.0f);
        drag_.lastMouse = pos;
        const float angle = snapTo(glm::degrees(drag_.totalAngle), angleSnap_);
        if (angle != drag_.appliedAngle) {
            glm::vec3 degrees(0.0f);
            degrees[axis] = angle - drag_.appliedAngle;
            emit rotateRequested(selected_, degrees);
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

void Viewport::setBrush(bool on, float radius, glm::vec3 color)
{
    brush_ = on;
    brushRadius_ = radius;
    brushColor_ = color;
    setMouseTracking(on || hasMouseTracking());
    if (!on && painting_) {
        painting_ = false;
        brushTimer_.stop();
        emit brushFinished();
    }
    update();
}

bool Viewport::brushHit(QPointF pos, glm::vec3& point) const
{
    if (brushModel_ >= 0) {
        if (!scene_->isModel(brushModel_))
            return false;
        glm::vec3 origin, dir;
        rayAt(pos, origin, dir);
        float t = 0.0f;
        if (!engine::raycastModel(scene_->model(brushModel_), origin, dir, 5000.0f, t))
            return false;
        point = origin + dir * t;
        return true;
    }
    if (!scene_->terrain || !scene_->terrain->valid())
        return false;
    glm::vec3 origin, dir;
    rayAt(pos, origin, dir);
    float t = 0.0f;
    if (!scene_->terrain->raycast(origin, dir, 5000.0f, t))
        return false;
    point = origin + dir * t;
    return true;
}

std::size_t Viewport::particleMemoryBytes() const { return particles_.memoryBytes(); }
