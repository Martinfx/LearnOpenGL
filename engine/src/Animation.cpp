#include "engine/Animation.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <array>

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

float clipDuration(const Skeleton& skeleton, int clip)
{
    return clip >= 0 ? skeleton.clips[static_cast<std::size_t>(clip)].duration : 0.0f;
}

float paramValue(const AnimGraph& g, const AnimGraphState& st, const std::string& name)
{
    const int i = g.findParam(name);
    return i >= 0 && static_cast<std::size_t>(i) < st.params.size() ? st.params[static_cast<std::size_t>(i)] : 0.0f;
}

// Weight of every sample of a state for the current parameters.
void stateWeights(const AnimGraph& g, const AnimGraphState& st, const AnimGraph::State& s, std::vector<float>& w)
{
    const std::size_t n = s.samples.size();
    w.assign(n, 0.0f);
    if (n == 0)
        return;
    if (s.kind == AnimGraph::StateKind::Clip || n == 1) {
        w[0] = 1.0f;
        return;
    }
    const float x = paramValue(g, st, s.paramX);
    if (s.kind == AnimGraph::StateKind::Blend1D) {
        // Bracketing pair along x (samples in any order).
        int lo = -1, hi = -1;
        for (std::size_t i = 0; i < n; ++i) {
            const float sx = s.samples[i].x;
            if (sx <= x && (lo < 0 || sx > s.samples[static_cast<std::size_t>(lo)].x))
                lo = static_cast<int>(i);
            if (sx >= x && (hi < 0 || sx < s.samples[static_cast<std::size_t>(hi)].x))
                hi = static_cast<int>(i);
        }
        if (lo < 0)
            w[static_cast<std::size_t>(hi)] = 1.0f;
        else if (hi < 0 || hi == lo)
            w[static_cast<std::size_t>(lo)] = 1.0f;
        else {
            const float a = s.samples[static_cast<std::size_t>(lo)].x, b = s.samples[static_cast<std::size_t>(hi)].x;
            const float k = b > a ? (x - a) / (b - a) : 0.0f;
            w[static_cast<std::size_t>(lo)] = 1.0f - k;
            w[static_cast<std::size_t>(hi)] = k;
        }
        return;
    }
    // 2D: gradient band interpolation (each sample's influence fades toward every other sample).
    const glm::vec2 p(x, paramValue(g, st, s.paramY));
    float sum = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 pi(s.samples[i].x, s.samples[i].y);
        float wi = 1.0f;
        for (std::size_t j = 0; j < n && wi > 0.0f; ++j) {
            if (j == i)
                continue;
            const glm::vec2 d = glm::vec2(s.samples[j].x, s.samples[j].y) - pi;
            const float len2 = glm::dot(d, d);
            if (len2 <= 1e-8f)
                continue;
            wi = std::min(wi, std::clamp(1.0f - glm::dot(p - pi, d) / len2, 0.0f, 1.0f));
        }
        w[i] = wi;
        sum += wi;
    }
    if (sum > 0.0f)
        for (float& v : w)
            v /= sum;
    else
        w[0] = 1.0f;
}

// Seconds one pass through the state takes (weighted sample lengths over their speeds).
float stateDuration(const Skeleton& skeleton, const AnimGraphState& st, int layer, int state, const AnimGraph::State& s,
                    const std::vector<float>& w)
{
    const std::vector<int>& clips = st.clips[static_cast<std::size_t>(st.stateBase[static_cast<std::size_t>(layer)] + state)];
    float d = 0.0f;
    for (std::size_t i = 0; i < s.samples.size(); ++i)
        d += w[i] * clipDuration(skeleton, clips[i]) / std::max(0.01f, std::abs(s.samples[i].speed));
    return d / std::max(0.01f, std::abs(s.speed));
}

float advancePhase(float phase, float dt, float duration, bool loop)
{
    if (duration <= 0.0f)
        return loop ? 0.0f : 1.0f;
    phase += dt / duration;
    return loop ? phase - std::floor(phase) : std::min(phase, 1.0f);
}

void resolveGraph(const Skeleton& skeleton, const AnimGraph& g, AnimGraphState& st)
{
    const void* previousGraph = st.resolvedGraph;
    const bool sameGraph = previousGraph == &g;
    st.resolvedFor = &skeleton;
    st.resolvedGraph = &g;
    st.resolvedRevision = g.revision;
    st.stateBase.clear();
    st.clips.clear();
    st.maskNode.clear();
    for (const AnimGraph::Layer& l : g.layers) {
        st.stateBase.push_back(static_cast<int>(st.clips.size()));
        for (const AnimGraph::State& s : l.states) {
            std::vector<int> c;
            for (const AnimGraph::Sample& smp : s.samples)
                c.push_back(skeleton.findClip(smp.clip));
            st.clips.push_back(std::move(c));
        }
        int mask = -1;
        for (std::size_t n = 0; n < skeleton.nodes.size() && !l.maskBone.empty(); ++n)
            if (skeleton.nodes[n].name == l.maskBone)
                mask = static_cast<int>(n);
        st.maskNode.push_back(mask);
    }
    st.eventClip.clear();
    for (const AnimGraph::Event& e : g.events)
        st.eventClip.push_back(skeleton.findClip(e.clip));
    // Parameters keep their values (set before the first update, or the graph was only edited).
    const bool keep = sameGraph || previousGraph == nullptr;
    std::vector<float> params;
    for (std::size_t i = 0; i < g.params.size(); ++i)
        params.push_back(keep && i < st.params.size() ? st.params[i] : g.params[i].value);
    st.params = std::move(params);
    auto node = [&skeleton](const std::string& name) {
        for (std::size_t n = 0; n < skeleton.nodes.size() && !name.empty(); ++n)
            if (skeleton.nodes[n].name == name)
                return static_cast<int>(n);
        return -1;
    };
    st.ikNodes.clear();
    st.ikGrip.assign(g.ik.size(), glm::mat4(1.0f));
    st.ikWeight.resize(g.ik.size(), 0.0f);
    st.footLift.resize(g.ik.size(), 0.0f);
    for (std::size_t i = 0; i < g.ik.size(); ++i) {
        const AnimGraph::IkChain& c = g.ik[i];
        st.ikNodes.push_back({node(c.root), node(c.mid), node(c.end), node(c.target)});
        const auto& n = st.ikNodes.back();
        if (c.kind != AnimGraph::IkKind::Hand || n[2] < 0 || n[3] < 0)
            continue;
        Animator reference;
        if (!reference.play(skeleton, c.referenceClip, false, 1.0f, 0.0f))
            continue;
        reference.current.time = c.referenceTime;
        std::vector<glm::mat4> pose;
        evaluatePose(skeleton, &reference, pose);
        st.ikGrip[i] = glm::inverse(pose[static_cast<std::size_t>(n[3])]) * pose[static_cast<std::size_t>(n[2])];
    }
    st.layers.resize(g.layers.size());
    for (std::size_t l = 0; l < g.layers.size(); ++l) {
        AnimGraphState::Layer& sl = st.layers[l];
        const int count = static_cast<int>(g.layers[l].states.size());
        if (!sameGraph || sl.state < 0 || sl.state >= count)
            sl = {std::min(g.layers[l].entry, count - 1), -1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        if (sl.previous >= count)
            sl.previous = -1;
    }
}

bool conditionHolds(const AnimGraph& g, const AnimGraphState& st, const AnimGraph::Condition& c)
{
    const float v = paramValue(g, st, c.param);
    switch (c.op) {
    case AnimGraph::Op::IsTrue: return v != 0.0f;
    case AnimGraph::Op::IsFalse: return v == 0.0f;
    case AnimGraph::Op::Greater: return v > c.value;
    case AnimGraph::Op::Less: return v < c.value;
    case AnimGraph::Op::GreaterEq: return v >= c.value;
    case AnimGraph::Op::LessEq: return v <= c.value;
    case AnimGraph::Op::Equal: return v == c.value;
    case AnimGraph::Op::NotEqual: return v != c.value;
    }
    return false;
}

// Clip events between two phases of a clip (wrapping when the state loops).
void fireEvents(const Skeleton& skeleton, const AnimGraph& g, AnimGraphState& st, int clip, float from, float to, bool wrapped)
{
    const float duration = clipDuration(skeleton, clip);
    for (std::size_t i = 0; i < g.events.size(); ++i) {
        if (st.eventClip[i] != clip)
            continue;
        const float t = duration > 0.0f ? g.events[i].time / duration : 0.0f;
        const bool crossed = wrapped ? (t > from || t <= to) : (t > from && t <= to);
        if (crossed)
            st.events.push_back(g.events[i].name);
    }
}

void updateGraph(const Skeleton& skeleton, const AnimGraph& g, AnimGraphState& st, float dt)
{
    if (st.resolvedFor != &skeleton || st.resolvedGraph != &g || st.resolvedRevision != g.revision)
        resolveGraph(skeleton, g, st);
    st.events.clear();
    std::vector<float> w;
    for (std::size_t li = 0; li < g.layers.size(); ++li) {
        const AnimGraph::Layer& layer = g.layers[li];
        AnimGraphState::Layer& sl = st.layers[li];
        const int layerIndex = static_cast<int>(li);
        if (sl.state < 0)
            continue;
        // Advance the current state (and the one fading out) in sync across their samples.
        const AnimGraph::State& s = layer.states[static_cast<std::size_t>(sl.state)];
        stateWeights(g, st, s, w);
        const float duration = stateDuration(skeleton, st, layerIndex, sl.state, s, w);
        const float before = sl.phase;
        sl.phase = advancePhase(sl.phase, dt, duration, s.loop);
        sl.time += duration > 0.0f ? dt / duration : 1.0f;
        if (!w.empty()) {
            const std::size_t dominant = static_cast<std::size_t>(std::max_element(w.begin(), w.end()) - w.begin());
            const int clip = st.clips[static_cast<std::size_t>(st.stateBase[li] + sl.state)][dominant];
            if (clip >= 0 && dt > 0.0f)
                fireEvents(skeleton, g, st, clip, before, sl.phase, s.loop && sl.phase < before);
        }
        if (sl.previous >= 0) {
            const AnimGraph::State& ps = layer.states[static_cast<std::size_t>(sl.previous)];
            stateWeights(g, st, ps, w);
            sl.previousPhase = advancePhase(sl.previousPhase, dt, stateDuration(skeleton, st, layerIndex, sl.previous, ps, w), ps.loop);
            sl.fade += dt;
            if (sl.fade >= sl.fadeDuration)
                sl.previous = -1;
        }
        // Transitions (after advancing, so a clip ending this update leaves now): from-any ones may
        // interrupt a crossfade, others wait for it to end. The new state starts next update.
        for (const AnimGraph::Transition& t : layer.transitions) {
            if (t.to < 0 || t.to >= static_cast<int>(layer.states.size()))
                continue;
            if (t.from >= 0 ? (t.from != sl.state || sl.previous >= 0) : t.to == sl.state)
                continue;
            if (t.exitTime >= 0.0f && sl.time < t.exitTime)
                continue;
            bool ok = true;
            for (const AnimGraph::Condition& c : t.conditions)
                ok = ok && conditionHolds(g, st, c);
            if (!ok)
                continue;
            for (const AnimGraph::Condition& c : t.conditions) {
                const int p = g.findParam(c.param);
                if (p >= 0 && g.params[static_cast<std::size_t>(p)].type == AnimGraph::ParamType::Trigger)
                    st.params[static_cast<std::size_t>(p)] = 0.0f;
            }
            if (t.duration > 0.0f) {
                sl.previous = sl.state;
                sl.previousPhase = sl.phase;
                sl.fade = 0.0f;
                sl.fadeDuration = t.duration;
            } else {
                sl.previous = -1;
            }
            sl.state = t.to;
            sl.phase = 0.0f;
            sl.time = 0.0f;
            break;
        }
    }
    for (std::size_t i = 0; i < g.ik.size(); ++i) {
        const AnimGraph::IkChain& c = g.ik[i];
        const float target = c.weightParam.empty() ? c.weight : std::clamp(paramValue(g, st, c.weightParam), 0.0f, 1.0f) * c.weight;
        const float step = dt / 0.2f; // 0.2 s to blend IK in or out
        st.ikWeight[i] = dt > 0.0f ? st.ikWeight[i] + std::clamp(target - st.ikWeight[i], -step, step) : st.ikWeight[i];
    }
}

glm::quat rotationBetween(glm::vec3 from, glm::vec3 to)
{
    from = glm::normalize(from);
    to = glm::normalize(to);
    const float d = glm::dot(from, to);
    if (d < -0.9999f) { // opposite: any perpendicular axis
        glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), from);
        if (glm::dot(axis, axis) < 1e-6f)
            axis = glm::cross(glm::vec3(0, 1, 0), from);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    const glm::vec3 c = glm::cross(from, to);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

glm::vec3 position(const glm::mat4& m)
{
    return glm::vec3(m[3]);
}

// Rotates node `root` and everything below it about `pivot` (model space).
void rotateSubtree(const Skeleton& skeleton, std::vector<glm::mat4>& globals, int root, glm::vec3 pivot, const glm::quat& q,
                   std::vector<char>& inTree)
{
    const glm::mat4 m = glm::translate(glm::mat4(1.0f), pivot) * glm::mat4_cast(q) * glm::translate(glm::mat4(1.0f), -pivot);
    inTree.assign(globals.size(), 0);
    for (std::size_t n = static_cast<std::size_t>(root); n < globals.size(); ++n) {
        const int parent = skeleton.nodes[n].parent;
        inTree[n] = static_cast<int>(n) == root || (parent >= 0 && inTree[static_cast<std::size_t>(parent)]);
        if (inTree[n])
            globals[n] = m * globals[n];
    }
}

// Two-bone IK: bends root -> mid -> end so the end reaches `target`, keeping the joint's bend direction.
void solveTwoBone(const Skeleton& skeleton, std::vector<glm::mat4>& globals, const std::array<int, 4>& n, glm::vec3 target,
                  std::vector<char>& scratch)
{
    const glm::vec3 a = position(globals[static_cast<std::size_t>(n[0])]);
    const glm::vec3 b = position(globals[static_cast<std::size_t>(n[1])]);
    const glm::vec3 c = position(globals[static_cast<std::size_t>(n[2])]);
    const float l1 = glm::length(b - a), l2 = glm::length(c - b);
    const glm::vec3 toTarget = target - a;
    const float reach = glm::length(toTarget);
    if (l1 < 1e-5f || l2 < 1e-5f || reach < 1e-5f)
        return;
    const float dist = std::clamp(reach, std::abs(l1 - l2) + 1e-4f, l1 + l2 - 1e-4f);
    const glm::vec3 dir = toTarget / reach;
    glm::vec3 pole = (b - a) - glm::dot(b - a, dir) * dir;
    if (glm::dot(pole, pole) < 1e-10f) {
        pole = glm::cross(dir, glm::vec3(1, 0, 0));
        if (glm::dot(pole, pole) < 1e-10f)
            pole = glm::cross(dir, glm::vec3(0, 0, 1));
    }
    pole = glm::normalize(pole);
    const float cosA = std::clamp((l1 * l1 + dist * dist - l2 * l2) / (2.0f * l1 * dist), -1.0f, 1.0f);
    const glm::vec3 knee = a + dir * (l1 * cosA) + pole * (l1 * std::sqrt(1.0f - cosA * cosA));
    rotateSubtree(skeleton, globals, n[0], a, rotationBetween(b - a, knee - a), scratch);
    const glm::vec3 c1 = position(globals[static_cast<std::size_t>(n[2])]);
    rotateSubtree(skeleton, globals, n[1], knee, rotationBetween(c1 - knee, a + dir * dist - knee), scratch);
}

glm::quat rotationOf(const glm::mat4& m)
{
    return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])))));
}

void applyIk(const Skeleton& skeleton, const AnimGraph& g, const AnimGraphState& st, std::vector<glm::mat4>& globals)
{
    if (st.ikNodes.size() != g.ik.size() || st.ikWeight.size() != g.ik.size() || st.resolvedRevision != g.revision)
        return;
    std::vector<char> scratch;
    // Feet: the body drops for the lower foot, then each leg reaches its own ground.
    float footWeight = 0.0f;
    for (std::size_t i = 0; i < g.ik.size(); ++i)
        if (g.ik[i].kind == AnimGraph::IkKind::Foot)
            footWeight = std::max(footWeight, st.ikWeight[i]);
    const bool feet = st.footIk && footWeight > 0.0f && st.footLift.size() == g.ik.size();
    if (feet && st.pelvisDrop != 0.0f)
        for (glm::mat4& m : globals)
            m[3].y += st.pelvisDrop * footWeight;
    for (std::size_t i = 0; i < g.ik.size(); ++i) {
        const AnimGraph::IkChain& c = g.ik[i];
        const std::array<int, 4>& n = st.ikNodes[i];
        const float w = st.ikWeight[i];
        if (w <= 0.0f || n[0] < 0 || n[1] < 0 || n[2] < 0)
            continue;
        const glm::vec3 end = position(globals[static_cast<std::size_t>(n[2])]);
        if (c.kind == AnimGraph::IkKind::Foot) {
            if (!feet)
                continue;
            const float lift = (st.footLift[i] - st.pelvisDrop) * w;
            if (std::abs(lift) > 1e-5f)
                solveTwoBone(skeleton, globals, n, end + glm::vec3(0.0f, lift, 0.0f), scratch);
        } else if (n[3] >= 0) {
            const glm::mat4 want = globals[static_cast<std::size_t>(n[3])] * st.ikGrip[i];
            solveTwoBone(skeleton, globals, n, glm::mix(end, position(want), w), scratch);
            const glm::mat4& now = globals[static_cast<std::size_t>(n[2])];
            const glm::quat delta = rotationOf(want) * glm::inverse(rotationOf(now));
            rotateSubtree(skeleton, globals, n[2], position(now), glm::slerp(glm::quat(1, 0, 0, 0), delta, w), scratch);
        }
    }
}

// Local pose of one state: its samples blended by weight (rest pose for an empty state).
bool statePose(const Skeleton& skeleton, const AnimGraph& g, const AnimGraphState& st, int layer, int state, float phase,
               const std::vector<Local>& rest, std::vector<Local>& out)
{
    const AnimGraph::State& s = g.layers[static_cast<std::size_t>(layer)].states[static_cast<std::size_t>(state)];
    const std::vector<int>& clips = st.clips[static_cast<std::size_t>(st.stateBase[static_cast<std::size_t>(layer)] + state)];
    std::vector<float> w;
    stateWeights(g, st, s, w);
    out = rest;
    bool any = false;
    std::vector<Local> one;
    float total = 0.0f;
    for (std::size_t i = 0; i < w.size(); ++i) {
        if (w[i] <= 0.0001f || clips[i] < 0)
            continue;
        Animator::Track track{clips[i], phase * clipDuration(skeleton, clips[i]), 1.0f, s.loop};
        if (!any) {
            applyClip(skeleton, track, out);
            any = true;
            total = w[i];
            continue;
        }
        one = rest;
        applyClip(skeleton, track, one);
        total += w[i];
        const float k = w[i] / total; // running weighted average
        for (std::size_t n = 0; n < out.size(); ++n) {
            out[n].t = glm::mix(out[n].t, one[n].t, k);
            out[n].r = glm::slerp(out[n].r, one[n].r, k);
            out[n].s = glm::mix(out[n].s, one[n].s, k);
        }
    }
    return any;
}

void blendInto(std::vector<Local>& dst, const std::vector<Local>& src, float w, const std::vector<char>* mask)
{
    for (std::size_t n = 0; n < dst.size(); ++n) {
        if (mask && !(*mask)[n])
            continue;
        dst[n].t = glm::mix(dst[n].t, src[n].t, w);
        dst[n].r = glm::slerp(dst[n].r, src[n].r, w);
        dst[n].s = glm::mix(dst[n].s, src[n].s, w);
    }
}

void evaluateGraph(const Skeleton& skeleton, const AnimGraph& g, const AnimGraphState& st, std::vector<Local>& locals)
{
    if (st.resolvedFor != &skeleton || st.resolvedGraph != &g || st.resolvedRevision != g.revision || st.layers.size() != g.layers.size())
        return; // not updated since the graph changed: rest pose
    const std::vector<Local> rest = locals;
    std::vector<Local> pose, from;
    std::vector<char> mask;
    for (std::size_t li = 0; li < g.layers.size(); ++li) {
        const AnimGraph::Layer& layer = g.layers[li];
        const AnimGraphState::Layer& sl = st.layers[li];
        if (sl.state < 0)
            continue;
        float weight = li == 0 ? 1.0f : layer.weight;
        if (li > 0 && !layer.weightParam.empty())
            weight = std::clamp(paramValue(g, st, layer.weightParam), 0.0f, 1.0f);
        if (weight <= 0.0f)
            continue;
        const int layerIndex = static_cast<int>(li);
        // An empty state stands for "this layer off": the pose below shows through.
        if (!statePose(skeleton, g, st, layerIndex, sl.state, sl.phase, rest, pose))
            pose = locals;
        if (sl.previous >= 0 && sl.fadeDuration > 0.0f) {
            if (!statePose(skeleton, g, st, layerIndex, sl.previous, sl.previousPhase, rest, from))
                from = locals;
            blendInto(from, pose, std::clamp(sl.fade / sl.fadeDuration, 0.0f, 1.0f), nullptr);
            pose.swap(from);
        }
        const int maskNode = st.maskNode[li];
        if (maskNode >= 0) {
            mask.assign(skeleton.nodes.size(), 0);
            for (std::size_t n = 0; n < skeleton.nodes.size(); ++n) {
                const int parent = skeleton.nodes[n].parent;
                mask[n] = static_cast<int>(n) == maskNode || (parent >= 0 && mask[static_cast<std::size_t>(parent)]);
            }
        }
        blendInto(locals, pose, weight, maskNode >= 0 ? &mask : nullptr);
    }
}

} // namespace

void Animator::update(const Skeleton& skeleton, float dt)
{
    if (graph)
        updateGraph(skeleton, *graph, graphState, graphActive() ? dt : 0.0f);
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
    if (graphActive()) {
        if (graphState.layers.empty() || graph->layers.empty() || graphState.layers[0].state < 0)
            return true;
        const AnimGraph::State& s = graph->layers[0].states[static_cast<std::size_t>(graphState.layers[0].state)];
        return !s.loop && graphState.layers[0].phase >= 1.0f;
    }
    if (current.clip < 0)
        return true;
    if (current.loop)
        return false;
    const float duration = skeleton.clips[static_cast<std::size_t>(current.clip)].duration;
    return current.speed >= 0.0f ? current.time >= duration : current.time <= 0.0f;
}

std::string Animator::clipName(const Skeleton& skeleton) const
{
    if (graphActive())
        return stateName(0);
    return current.clip >= 0 ? skeleton.clips[static_cast<std::size_t>(current.clip)].name : std::string();
}

void Animator::setGraph(std::shared_ptr<const AnimGraph> g)
{
    if (g == graph)
        return;
    graph = std::move(g);
    graphState = {};
}

bool Animator::setParam(const std::string& name, float value)
{
    if (!graph)
        return false;
    const int i = graph->findParam(name);
    if (i < 0)
        return false;
    if (graphState.params.size() != graph->params.size()) {
        graphState.params.resize(graph->params.size());
        for (std::size_t p = 0; p < graph->params.size(); ++p)
            graphState.params[p] = graph->params[p].value;
    }
    graphState.params[static_cast<std::size_t>(i)] = value;
    return true;
}

float Animator::param(const std::string& name) const
{
    if (!graph)
        return 0.0f;
    const int i = graph->findParam(name);
    if (i < 0)
        return 0.0f;
    return static_cast<std::size_t>(i) < graphState.params.size() ? graphState.params[static_cast<std::size_t>(i)]
                                                                     : graph->params[static_cast<std::size_t>(i)].value;
}

bool Animator::trigger(const std::string& name)
{
    return setParam(name, 1.0f);
}

std::string Animator::stateName(int layer) const
{
    if (!graph || layer < 0 || layer >= static_cast<int>(graph->layers.size()) || layer >= static_cast<int>(graphState.layers.size()))
        return {};
    const int s = graphState.layers[static_cast<std::size_t>(layer)].state;
    const auto& states = graph->layers[static_cast<std::size_t>(layer)].states;
    return s >= 0 && s < static_cast<int>(states.size()) ? states[static_cast<std::size_t>(s)].name : std::string();
}

void evaluatePose(const Skeleton& skeleton, const Animator* animator, std::vector<glm::mat4>& globals, bool ik)
{
    const std::size_t n = skeleton.nodes.size();
    std::vector<Local> locals(n);
    for (std::size_t i = 0; i < n; ++i)
        locals[i] = {skeleton.nodes[i].translation, skeleton.nodes[i].rotation, skeleton.nodes[i].scale};

    if (animator && animator->graphActive()) {
        evaluateGraph(skeleton, *animator->graph, animator->graphState, locals);
    } else if (animator) {
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
    if (ik && animator && animator->graphActive() && !animator->graph->ik.empty())
        applyIk(skeleton, *animator->graph, animator->graphState, globals);
}

void skinMatrices(const Skeleton& skeleton, int skin, const std::vector<glm::mat4>& globals, std::vector<glm::mat4>& out)
{
    const Skeleton::Skin& s = skeleton.skins[static_cast<std::size_t>(skin)];
    out.resize(s.joints.size());
    for (std::size_t j = 0; j < s.joints.size(); ++j)
        out[j] = globals[static_cast<std::size_t>(s.joints[j])] * s.inverseBind[j];
}

} // namespace engine
