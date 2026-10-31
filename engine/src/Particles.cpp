#include "engine/Particles.h"

#include "engine/JobSystem.h"
#include "engine/Material.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/exponential.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>

namespace engine {

namespace {

void setError(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

std::string toUtf8(const std::filesystem::path& p)
{
    const auto u8 = p.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

std::filesystem::path fromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

glm::vec3 srgbToLinear(glm::vec3 c)
{
    return glm::pow(glm::max(c, glm::vec3(0.0f)), glm::vec3(2.2f));
}

} // namespace

const char* emitterShapeName(EmitterShape shape)
{
    switch (shape) {
    case EmitterShape::Sphere: return "sphere";
    case EmitterShape::Box: return "box";
    case EmitterShape::Disc: return "disc";
    default: return "point";
    }
}

bool ParticleEffect::looping() const
{
    return std::any_of(emitters.begin(), emitters.end(), [](const ParticleEmitter& e) { return e.enabled && e.duration <= 0.0f && e.rate > 0.0f; });
}

float evalCurve(const std::vector<ParticleKey>& curve, float t, float fallback)
{
    if (curve.empty())
        return fallback;
    if (t <= curve.front().t)
        return curve.front().value.x;
    for (std::size_t i = 1; i < curve.size(); ++i)
        if (t <= curve[i].t) {
            const float span = std::max(curve[i].t - curve[i - 1].t, 1e-5f);
            return glm::mix(curve[i - 1].value.x, curve[i].value.x, (t - curve[i - 1].t) / span);
        }
    return curve.back().value.x;
}

glm::vec4 evalColorCurve(const std::vector<ParticleKey>& curve, float t)
{
    if (curve.empty())
        return glm::vec4(1.0f);
    if (t <= curve.front().t)
        return curve.front().value;
    for (std::size_t i = 1; i < curve.size(); ++i)
        if (t <= curve[i].t) {
            const float span = std::max(curve[i].t - curve[i - 1].t, 1e-5f);
            return glm::mix(curve[i - 1].value, curve[i].value, (t - curve[i - 1].t) / span);
        }
    return curve.back().value;
}

// ------------------------------------------------------------------ file

bool loadParticleFile(ParticleEffect& effect, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    const std::filesystem::path dir = std::filesystem::absolute(file).parent_path();
    ParticleEffect result;
    result.sourcePath = std::filesystem::absolute(file).lexically_normal();
    ParticleEmitter* e = nullptr;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        bool ok = true;
        if (key == "emitter") {
            result.emitters.emplace_back();
            e = &result.emitters.back();
            ls >> std::quoted(e->name);
            continue;
        }
        if (!e) {
            setError(error, file.string() + ":" + std::to_string(lineNo) + ": '" + key + "' before any emitter");
            return false;
        }
        if (key == "enabled") ok = static_cast<bool>(ls >> e->enabled);
        else if (key == "texture") {
            std::string p;
            ok = static_cast<bool>(ls >> std::quoted(p));
            if (ok && !p.empty()) {
                std::filesystem::path path = fromUtf8(p);
                e->texture = (path.is_relative() ? dir / path : path).lexically_normal();
            }
        } else if (key == "flipbook") ok = static_cast<bool>(ls >> e->columns >> e->rows >> e->frameRate >> e->randomFrame);
        else if (key == "blend") {
            std::string v;
            ok = static_cast<bool>(ls >> v);
            e->blend = v == "additive" ? SpriteBlend::Additive : SpriteBlend::Alpha;
        } else if (key == "lit") ok = static_cast<bool>(ls >> e->lit);
        else if (key == "stretch") ok = static_cast<bool>(ls >> e->stretch >> e->stretchFactor);
        else if (key == "rate") ok = static_cast<bool>(ls >> e->rate);
        else if (key == "burst") ok = static_cast<bool>(ls >> e->burst);
        else if (key == "duration") ok = static_cast<bool>(ls >> e->duration);
        else if (key == "delay") ok = static_cast<bool>(ls >> e->delay);
        else if (key == "max") ok = static_cast<bool>(ls >> e->maxParticles);
        else if (key == "shape") {
            std::string v;
            ok = static_cast<bool>(ls >> v >> e->shapeSize.x >> e->shapeSize.y >> e->shapeSize.z);
            e->shape = v == "sphere" ? EmitterShape::Sphere : v == "box" ? EmitterShape::Box : v == "disc" ? EmitterShape::Disc : EmitterShape::Point;
        } else if (key == "direction") ok = static_cast<bool>(ls >> e->direction.x >> e->direction.y >> e->direction.z);
        else if (key == "spread") ok = static_cast<bool>(ls >> e->spread);
        else if (key == "speed") ok = static_cast<bool>(ls >> e->speedMin >> e->speedMax);
        else if (key == "gravity") ok = static_cast<bool>(ls >> e->gravity);
        else if (key == "drag") ok = static_cast<bool>(ls >> e->drag);
        else if (key == "wind") ok = static_cast<bool>(ls >> e->wind.x >> e->wind.y >> e->wind.z);
        else if (key == "world_space") ok = static_cast<bool>(ls >> e->worldSpace);
        else if (key == "life") ok = static_cast<bool>(ls >> e->lifeMin >> e->lifeMax);
        else if (key == "size") ok = static_cast<bool>(ls >> e->sizeMin >> e->sizeMax);
        else if (key == "size_key") {
            ParticleKey k;
            ok = static_cast<bool>(ls >> k.t >> k.value.x);
            e->sizeCurve.push_back(k);
        } else if (key == "color_key") {
            ParticleKey k;
            ok = static_cast<bool>(ls >> k.t >> k.value.r >> k.value.g >> k.value.b >> k.value.a);
            e->colorCurve.push_back(k);
        } else if (key == "color") ok = static_cast<bool>(ls >> e->color.r >> e->color.g >> e->color.b);
        else if (key == "intensity") ok = static_cast<bool>(ls >> e->intensity);
        else if (key == "rotation") ok = static_cast<bool>(ls >> e->rotationMin >> e->rotationMax);
        else if (key == "spin") ok = static_cast<bool>(ls >> e->spinMin >> e->spinMax);
        // unknown keys: written by a newer editor, skipped
        if (!ok) {
            setError(error, file.string() + ":" + std::to_string(lineNo) + ": bad '" + key + "' line");
            return false;
        }
    }
    for (ParticleEmitter& em : result.emitters) {
        auto byT = [](const ParticleKey& a, const ParticleKey& b) { return a.t < b.t; };
        std::stable_sort(em.sizeCurve.begin(), em.sizeCurve.end(), byT);
        std::stable_sort(em.colorCurve.begin(), em.colorCurve.end(), byT);
        em.columns = std::max(1, em.columns);
        em.rows = std::max(1, em.rows);
        em.maxParticles = std::clamp(em.maxParticles, 1, 10000);
    }
    effect = std::move(result);
    return true;
}

bool saveParticleFile(const ParticleEffect& effect, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    const std::filesystem::path dir = std::filesystem::absolute(file).parent_path();
    out << "# Particle effect (editor: Particle editor). Directions: +Y up, -Z forward.\n";
    for (const ParticleEmitter& e : effect.emitters) {
        out << "emitter " << std::quoted(e.name) << '\n';
        out << "enabled " << e.enabled << '\n';
        if (!e.texture.empty()) {
            const std::filesystem::path rel = std::filesystem::absolute(e.texture).lexically_relative(dir);
            out << "texture " << std::quoted(toUtf8(rel.empty() ? e.texture : rel)) << '\n';
        }
        out << "flipbook " << e.columns << ' ' << e.rows << ' ' << e.frameRate << ' ' << e.randomFrame << '\n';
        out << "blend " << (e.blend == SpriteBlend::Additive ? "additive" : "alpha") << '\n';
        out << "lit " << e.lit << '\n';
        out << "stretch " << e.stretch << ' ' << e.stretchFactor << '\n';
        out << "rate " << e.rate << '\n';
        out << "burst " << e.burst << '\n';
        out << "duration " << e.duration << '\n';
        out << "delay " << e.delay << '\n';
        out << "max " << e.maxParticles << '\n';
        out << "shape " << emitterShapeName(e.shape) << ' ' << e.shapeSize.x << ' ' << e.shapeSize.y << ' ' << e.shapeSize.z << '\n';
        out << "direction " << e.direction.x << ' ' << e.direction.y << ' ' << e.direction.z << '\n';
        out << "spread " << e.spread << '\n';
        out << "speed " << e.speedMin << ' ' << e.speedMax << '\n';
        out << "gravity " << e.gravity << '\n';
        out << "drag " << e.drag << '\n';
        out << "wind " << e.wind.x << ' ' << e.wind.y << ' ' << e.wind.z << '\n';
        out << "world_space " << e.worldSpace << '\n';
        out << "life " << e.lifeMin << ' ' << e.lifeMax << '\n';
        out << "size " << e.sizeMin << ' ' << e.sizeMax << '\n';
        for (const ParticleKey& k : e.sizeCurve)
            out << "size_key " << k.t << ' ' << k.value.x << '\n';
        out << "color " << e.color.r << ' ' << e.color.g << ' ' << e.color.b << '\n';
        out << "intensity " << e.intensity << '\n';
        for (const ParticleKey& k : e.colorCurve)
            out << "color_key " << k.t << ' ' << k.value.r << ' ' << k.value.g << ' ' << k.value.b << ' ' << k.value.a << '\n';
        out << "rotation " << e.rotationMin << ' ' << e.rotationMax << '\n';
        out << "spin " << e.spinMin << ' ' << e.spinMax << '\n';
    }
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    return true;
}

void loadParticleTextures(ParticleEffect& effect)
{
    effect.textures.assign(effect.emitters.size(), nullptr);
    for (std::size_t i = 0; i < effect.emitters.size(); ++i) {
        const std::filesystem::path& p = effect.emitters[i].texture;
        std::error_code ec;
        if (!p.empty() && std::filesystem::is_regular_file(p, ec))
            effect.textures[i] = loadTextureAsset(p, nullptr, true);
    }
}

std::shared_ptr<ParticleEffect> loadParticleEffect(const std::filesystem::path& file, std::string* error)
{
    static std::mutex mutex;
    static std::map<std::filesystem::path, std::weak_ptr<ParticleEffect>> cache;
    const std::filesystem::path key = std::filesystem::absolute(file).lexically_normal();
    {
        std::lock_guard lock(mutex);
        if (auto it = cache.find(key); it != cache.end())
            if (auto alive = it->second.lock())
                return alive;
    }
    auto effect = std::make_shared<ParticleEffect>();
    if (!loadParticleFile(*effect, key, error))
        return nullptr;
    loadParticleTextures(*effect);
    std::lock_guard lock(mutex);
    cache[key] = effect;
    return effect;
}

glm::mat3 basisFromUp(glm::vec3 up)
{
    const float len = glm::length(up);
    up = len > 1e-6f ? up / len : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 helper = std::abs(up.y) < 0.95f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 x = glm::normalize(glm::cross(helper, up));
    const glm::vec3 z = glm::cross(x, up);
    return glm::mat3(x, up, z);
}

// ------------------------------------------------------------------ simulation

namespace {

struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float age = 0.0f, life = 1.0f;
    float size = 0.1f;
    float rotation = 0.0f, spin = 0.0f;
    float frame = 0.0f;
};

struct EmitterState {
    std::vector<Particle> particles;
    float accumulator = 0.0f;
    bool burstDone = false;
};

} // namespace

struct ParticleSystem::Impl {
    struct Instance {
        Handle id = 0;
        std::shared_ptr<const ParticleEffect> effect;
        glm::vec3 position{0.0f};
        glm::mat3 rotation{1.0f};
        float time = 0.0f;
        bool stopped = false;
        float rateScale = 1.0f;
        std::vector<EmitterState> emitters;
        std::mt19937 rng;
    };
    std::vector<Instance> instances;
    Handle nextId = 1;
    std::uint32_t seed = 12345;

    Instance* find(Handle h)
    {
        for (Instance& i : instances)
            if (i.id == h)
                return &i;
        return nullptr;
    }
};

ParticleSystem::ParticleSystem() : impl_(std::make_unique<Impl>()) {}
ParticleSystem::~ParticleSystem() = default;

ParticleSystem::Handle ParticleSystem::spawn(std::shared_ptr<const ParticleEffect> effect, glm::vec3 position, const glm::mat3& rotation)
{
    if (!effect || effect->emitters.empty())
        return 0;
    Impl::Instance inst;
    inst.id = impl_->nextId++;
    if (impl_->nextId == 0)
        impl_->nextId = 1;
    inst.effect = std::move(effect);
    inst.position = position;
    inst.rotation = rotation;
    inst.emitters.resize(inst.effect->emitters.size());
    inst.rng.seed(impl_->seed = impl_->seed * 1664525u + 1013904223u);
    const Handle id = inst.id;
    impl_->instances.push_back(std::move(inst));
    return id;
}

void ParticleSystem::setTransform(Handle h, glm::vec3 position, const glm::mat3& rotation)
{
    if (Impl::Instance* i = impl_->find(h)) {
        i->position = position;
        i->rotation = rotation;
    }
}

void ParticleSystem::setRateScale(Handle h, float scale)
{
    if (Impl::Instance* i = impl_->find(h))
        i->rateScale = std::max(0.0f, scale);
}

void ParticleSystem::stop(Handle h)
{
    if (Impl::Instance* i = impl_->find(h))
        i->stopped = true;
}

void ParticleSystem::kill(Handle h)
{
    std::erase_if(impl_->instances, [h](const Impl::Instance& i) { return i.id == h; });
}

void ParticleSystem::clear()
{
    impl_->instances.clear();
}

bool ParticleSystem::alive(Handle h) const
{
    return impl_->find(h) != nullptr;
}

namespace {

glm::vec3 randomInCone(glm::vec3 dir, float spreadDegrees, std::mt19937& rng)
{
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const float len = glm::length(dir);
    dir = len > 1e-6f ? dir / len : glm::vec3(0.0f, 1.0f, 0.0f);
    const float cosMax = std::cos(glm::radians(std::clamp(spreadDegrees, 0.0f, 180.0f)));
    const float cosT = glm::mix(1.0f, cosMax, u(rng)); // uniform over the cap
    const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    const float phi = u(rng) * glm::two_pi<float>();
    const glm::mat3 b = basisFromUp(dir);
    return b * glm::vec3(sinT * std::cos(phi), cosT, sinT * std::sin(phi));
}

glm::vec3 randomInShape(const ParticleEmitter& e, std::mt19937& rng)
{
    std::uniform_real_distribution<float> s(-1.0f, 1.0f);
    switch (e.shape) {
    case EmitterShape::Sphere: {
        glm::vec3 p;
        do
            p = {s(rng), s(rng), s(rng)};
        while (glm::dot(p, p) > 1.0f);
        return p * e.shapeSize.x;
    }
    case EmitterShape::Box: return glm::vec3(s(rng), s(rng), s(rng)) * e.shapeSize;
    case EmitterShape::Disc: {
        glm::vec2 p;
        do
            p = {s(rng), s(rng)};
        while (glm::dot(p, p) > 1.0f);
        return glm::vec3(p.x, 0.0f, p.y) * e.shapeSize.x;
    }
    default: return glm::vec3(0.0f);
    }
}

float range(float a, float b, std::mt19937& rng)
{
    return b > a ? std::uniform_real_distribution<float>(a, b)(rng) : a;
}

} // namespace

void ParticleSystem::update(float dt)
{
    if (dt <= 0.0f)
        return;
    auto simulate = [dt](Impl::Instance& inst) {
        inst.time += dt;
        const ParticleEffect& fx = *inst.effect;
        if (inst.emitters.size() != fx.emitters.size())
            inst.emitters.resize(fx.emitters.size()); // the editor added or removed an emitter
        for (std::size_t k = 0; k < fx.emitters.size() && k < inst.emitters.size(); ++k) {
            const ParticleEmitter& e = fx.emitters[k];
            EmitterState& st = inst.emitters[k];
            // Move and age.
            const glm::vec3 accel = glm::vec3(0.0f, -9.81f * e.gravity, 0.0f);
            const float damp = std::exp(-std::max(0.0f, e.drag) * dt);
            for (Particle& p : st.particles) {
                p.age += dt;
                p.velocity = p.velocity * damp + accel * dt;
                p.position += (p.velocity + e.wind) * dt;
                p.rotation += p.spin * dt;
            }
            std::erase_if(st.particles, [](const Particle& p) { return p.age >= p.life; });
            if (!e.enabled)
                continue;
            // Emit.
            const float t = inst.time - e.delay;
            if (t < 0.0f)
                continue;
            const bool running = !inst.stopped && (e.duration <= 0.0f || t <= e.duration);
            int count = 0;
            if (!st.burstDone) {
                count += e.burst;
                st.burstDone = true;
            }
            if (running && e.rate > 0.0f) {
                st.accumulator += e.rate * inst.rateScale * dt;
                const int n = static_cast<int>(st.accumulator);
                st.accumulator -= static_cast<float>(n);
                count += n;
            }
            count = std::min(count, e.maxParticles - static_cast<int>(st.particles.size()));
            for (int i = 0; i < count; ++i) {
                Particle p;
                const glm::vec3 local = randomInShape(e, inst.rng);
                const glm::vec3 dir = randomInCone(e.direction, e.spread, inst.rng);
                p.position = e.worldSpace ? inst.position + inst.rotation * local : local;
                p.velocity = (e.worldSpace ? inst.rotation * dir : dir) * range(e.speedMin, e.speedMax, inst.rng);
                p.life = std::max(0.01f, range(e.lifeMin, e.lifeMax, inst.rng));
                p.size = range(e.sizeMin, e.sizeMax, inst.rng);
                p.rotation = glm::radians(range(e.rotationMin, e.rotationMax, inst.rng));
                p.spin = glm::radians(range(e.spinMin, e.spinMax, inst.rng));
                const int cells = e.columns * e.rows;
                p.frame = e.randomFrame && cells > 1 ? static_cast<float>(std::uniform_int_distribution<int>(0, cells - 1)(inst.rng)) : 0.0f;
                // Spread the spawn over the step so a fast emitter makes a continuous stream.
                const float back = count > 1 ? dt * static_cast<float>(i) / static_cast<float>(count) : 0.0f;
                p.age = back;
                p.position += p.velocity * back;
                st.particles.push_back(p);
            }
        }
    };
    auto& list = impl_->instances;
    if (list.size() >= 8)
        JobSystem::global().parallelFor(list.size(), 2, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i)
                simulate(list[i]);
        });
    else
        for (auto& inst : list)
            simulate(inst);
    // Finished effects: stopped (or out of emission time) and no particles left.
    std::erase_if(list, [](const Impl::Instance& inst) {
        const ParticleEffect& fx = *inst.effect;
        for (std::size_t k = 0; k < fx.emitters.size() && k < inst.emitters.size(); ++k) {
            const ParticleEmitter& e = fx.emitters[k];
            if (!inst.emitters[k].particles.empty())
                return false;
            const bool emitting = e.enabled && !inst.stopped && (e.duration <= 0.0f ? e.rate > 0.0f : inst.time - e.delay <= e.duration);
            if (emitting || (e.enabled && !inst.emitters[k].burstDone))
                return false;
        }
        return true;
    });
}

void ParticleSystem::appendSprites(glm::vec3 eye, const glm::mat3& camera, std::vector<SpriteBatch>& out) const
{
    const glm::vec3 right = glm::normalize(camera[0]);
    const glm::vec3 up = glm::normalize(camera[1]);
    for (const Impl::Instance& inst : impl_->instances) {
        const ParticleEffect& fx = *inst.effect;
        for (std::size_t k = 0; k < fx.emitters.size() && k < inst.emitters.size(); ++k) {
            const ParticleEmitter& e = fx.emitters[k];
            const EmitterState& st = inst.emitters[k];
            if (st.particles.empty())
                continue;
            SpriteBatch b;
            b.texture = k < fx.textures.size() ? fx.textures[k] : nullptr;
            b.columns = e.columns;
            b.rows = e.rows;
            b.blend = e.blend;
            b.lit = e.lit;
            b.instances.reserve(st.particles.size());
            const glm::vec3 tint = srgbToLinear(e.color) * e.intensity;
            const int cells = e.columns * e.rows;
            glm::vec3 center(0.0f);
            for (const Particle& p : st.particles) {
                const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
                const float half = 0.5f * p.size * evalCurve(e.sizeCurve, t);
                const glm::vec4 c = evalColorCurve(e.colorCurve, t);
                SpriteInstance s;
                s.position = e.worldSpace ? p.position : inst.position + inst.rotation * p.position;
                const glm::vec3 vel = e.worldSpace ? p.velocity : inst.rotation * p.velocity;
                if (e.stretch && glm::dot(vel, vel) > 1e-4f) {
                    const glm::vec3 toEye = glm::normalize(eye - s.position);
                    glm::vec3 along = vel - toEye * glm::dot(vel, toEye); // on the screen plane
                    const float len = glm::length(along);
                    if (len > 1e-5f) {
                        along /= len;
                        s.axisY = along * (half + 0.5f * len * e.stretchFactor);
                        s.axisX = glm::normalize(glm::cross(along, toEye)) * half;
                    } else {
                        s.axisX = right * half;
                        s.axisY = up * half;
                    }
                } else {
                    const float cs = std::cos(p.rotation), sn = std::sin(p.rotation);
                    s.axisX = (right * cs + up * sn) * half;
                    s.axisY = (up * cs - right * sn) * half;
                }
                float frame = p.frame;
                if (cells > 1 && !e.randomFrame)
                    frame = e.frameRate > 0.0f ? std::fmod(p.age * e.frameRate, static_cast<float>(cells))
                                               : t * static_cast<float>(cells - 1);
                s.frame = frame;
                s.color = glm::vec4(tint * glm::vec3(c), c.a);
                b.instances.push_back(s);
                center += s.position;
            }
            center /= static_cast<float>(b.instances.size());
            b.distance = glm::distance(center, eye);
            if (e.blend == SpriteBlend::Alpha) // back to front within the batch
                std::sort(b.instances.begin(), b.instances.end(), [&](const SpriteInstance& a, const SpriteInstance& c) {
                    return glm::dot(a.position - eye, a.position - eye) > glm::dot(c.position - eye, c.position - eye);
                });
            out.push_back(std::move(b));
        }
    }
}

int ParticleSystem::instanceCount() const
{
    return static_cast<int>(impl_->instances.size());
}

int ParticleSystem::particleCount() const
{
    int n = 0;
    for (const auto& inst : impl_->instances)
        for (const EmitterState& e : inst.emitters)
            n += static_cast<int>(e.particles.size());
    return n;
}

} // namespace engine
