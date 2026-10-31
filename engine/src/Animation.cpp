#include "engine/Animation.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

int Skeleton::findClip(std::string_view name) const
{
    for (std::size_t i = 0; i < clips.size(); ++i)
        if (clips[i].name == name)
            return static_cast<int>(i);
    return -1;
}

bool Animator::play(const Skeleton& skeleton, std::string_view name, bool loop, float speed, float fade, bool restart)
{
    const int clip = skeleton.findClip(name);
    if (clip < 0)
        return false;
    if (clip == current.clip && !restart) {
        current.loop = loop;
        current.speed = speed;
        return true;
    }
    if (current.clip >= 0 && fade > 0.0f) {
        previous = current;
        fadeTime = 0.0f;
        fadeDuration = fade;
    } else {
        previous = {};
        fadeDuration = 0.0f;
    }
    current = {clip, speed < 0.0f ? skeleton.clips[static_cast<std::size_t>(clip)].duration : 0.0f, speed, loop};
    return true;
}

void Animator::stop()
{
    current = {};
    previous = {};
    fadeDuration = 0.0f;
}

namespace {

void advance(const Skeleton& skeleton, Animator::Track& t, float dt)
{
    if (t.clip < 0)
        return;
    const float duration = skeleton.clips[static_cast<std::size_t>(t.clip)].duration;
    t.time += dt * t.speed;
    if (duration <= 0.0f) {
        t.time = 0.0f;
    } else if (t.loop) {
        t.time = std::fmod(t.time, duration);
        if (t.time < 0.0f)
            t.time += duration;
    } else {
        t.time = std::clamp(t.time, 0.0f, duration);
    }
}

glm::vec4 sample(const Skeleton::Channel& c, float time)
{
    const auto& ts = c.times;
    if (ts.empty())
        return glm::vec4(0.0f);
    if (time <= ts.front() || ts.size() == 1)
        return c.values.front();
    if (time >= ts.back())
        return c.values.back();
    const auto it = std::upper_bound(ts.begin(), ts.end(), time);
    const std::size_t i1 = static_cast<std::size_t>(it - ts.begin());
    const std::size_t i0 = i1 - 1;
    if (c.step)
        return c.values[i0];
    const float span = ts[i1] - ts[i0];
    const float k = span > 0.0f ? (time - ts[i0]) / span : 0.0f;
    const glm::vec4& a = c.values[i0];
    const glm::vec4& b = c.values[i1];
    if (c.path == Skeleton::Path::Rotation) {
        const glm::quat qa(a.w, a.x, a.y, a.z);
        glm::quat qb(b.w, b.x, b.y, b.z);
        const glm::quat q = glm::slerp(qa, qb, k);
        return {q.x, q.y, q.z, q.w};
    }
    return a + (b - a) * k;
}

struct Local {
    glm::vec3 t;
    glm::quat r;
    glm::vec3 s;
};

void applyClip(const Skeleton& skeleton, const Animator::Track& track, std::vector<Local>& locals)
{
    if (track.clip < 0)
        return;
    for (const Skeleton::Channel& c : skeleton.clips[static_cast<std::size_t>(track.clip)].channels) {
        const glm::vec4 v = sample(c, track.time);
        Local& l = locals[static_cast<std::size_t>(c.node)];
        switch (c.path) {
        case Skeleton::Path::Translation: l.t = glm::vec3(v); break;
        case Skeleton::Path::Rotation: l.r = glm::normalize(glm::quat(v.w, v.x, v.y, v.z)); break;
        case Skeleton::Path::Scale: l.s = glm::vec3(v); break;
        }
    }
}

} // namespace

void Animator::update(const Skeleton& skeleton, float dt)
{
    advance(skeleton, current, dt);
    if (fadeDuration > 0.0f) {
        advance(skeleton, previous, dt);
        fadeTime += dt;
        if (fadeTime >= fadeDuration) {
            previous = {};
            fadeDuration = 0.0f;
        }
    }
}

bool Animator::finished(const Skeleton& skeleton) const
{
    if (current.clip < 0)
        return true;
    if (current.loop)
        return false;
    const float duration = skeleton.clips[static_cast<std::size_t>(current.clip)].duration;
    return current.speed >= 0.0f ? current.time >= duration : current.time <= 0.0f;
}

std::string Animator::clipName(const Skeleton& skeleton) const
{
    return current.clip >= 0 ? skeleton.clips[static_cast<std::size_t>(current.clip)].name : std::string();
}

void evaluatePose(const Skeleton& skeleton, const Animator* animator, std::vector<glm::mat4>& globals)
{
    const std::size_t n = skeleton.nodes.size();
    std::vector<Local> locals(n);
    for (std::size_t i = 0; i < n; ++i)
        locals[i] = {skeleton.nodes[i].translation, skeleton.nodes[i].rotation, skeleton.nodes[i].scale};

    if (animator) {
        if (animator->fadeDuration > 0.0f && animator->previous.clip >= 0) {
            std::vector<Local> from = locals;
            applyClip(skeleton, animator->previous, from);
            applyClip(skeleton, animator->current, locals);
            const float w = std::clamp(animator->fadeTime / animator->fadeDuration, 0.0f, 1.0f);
            for (std::size_t i = 0; i < n; ++i) {
                locals[i].t = glm::mix(from[i].t, locals[i].t, w);
                locals[i].r = glm::slerp(from[i].r, locals[i].r, w);
                locals[i].s = glm::mix(from[i].s, locals[i].s, w);
            }
        } else {
            applyClip(skeleton, animator->current, locals);
        }
    }

    globals.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Local& l = locals[i];
        const glm::mat4 local = glm::scale(glm::translate(glm::mat4(1.0f), l.t) * glm::mat4_cast(l.r), l.s);
        const int parent = skeleton.nodes[i].parent;
        globals[i] = parent >= 0 ? globals[static_cast<std::size_t>(parent)] * local : local;
    }
}

void skinMatrices(const Skeleton& skeleton, int skin, const std::vector<glm::mat4>& globals, std::vector<glm::mat4>& out)
{
    const Skeleton::Skin& s = skeleton.skins[static_cast<std::size_t>(skin)];
    out.resize(s.joints.size());
    for (std::size_t j = 0; j < s.joints.size(); ++j)
        out[j] = globals[static_cast<std::size_t>(s.joints[j])] * s.inverseBind[j];
}

} // namespace engine
