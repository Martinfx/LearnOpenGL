#pragma once
#include <array>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <memory>
#include <string>
#include <vector>

namespace engine {

// Animation graph (.animgraph, edited in the Animation editor): parameters set by scripts and
// the game (speed, aiming, a death trigger), layers of states with transitions between them.
// A state plays one clip or blends several by one or two parameters (blend space: idle / walk /
// run by speed; aim up / down by pitch). Upper layers override the bones under a mask bone
// (shooting with the upper body while the legs walk). Clips can carry named events (footstep,
// magazine out) that fire when playback passes them.
struct AnimGraph {
    enum class ParamType { Float, Bool, Trigger };
    struct Param {
        std::string name;
        ParamType type = ParamType::Float;
        float value = 0.0f; // default
    };
    struct Sample {
        std::string clip;
        float x = 0.0f, y = 0.0f; // position in the blend space
        float speed = 1.0f;
    };
    enum class StateKind { Clip, Blend1D, Blend2D, Empty };
    struct State {
        std::string name;
        StateKind kind = StateKind::Clip;
        std::vector<Sample> samples; // Clip: one; blend spaces: several; Empty: none (layer off)
        std::string paramX, paramY;  // blend space parameters
        bool loop = true;
        float speed = 1.0f;
        float editorX = 0.0f, editorY = 0.0f; // node position in the editor's graph
    };
    enum class Op { IsTrue, IsFalse, Greater, Less, GreaterEq, LessEq, Equal, NotEqual };
    struct Condition {
        std::string param;
        Op op = Op::IsTrue;
        float value = 0.0f;
    };
    struct Transition {
        int from = -1;            // state index, -1 = from any state
        int to = 0;
        float duration = 0.2f;    // crossfade seconds
        float exitTime = -1.0f;   // 0..1 of the source state's clip before it may leave, -1 = any time
        std::vector<Condition> conditions; // all must hold (none: leaves at the exit time)
    };
    struct Layer {
        std::string name = "Base";
        std::string maskBone;     // empty = every bone; else this bone and everything below it
        float weight = 1.0f;
        std::string weightParam;  // empty = `weight`; else a float parameter 0..1
        std::vector<State> states;
        std::vector<Transition> transitions;
        int entry = 0;            // state the layer starts in
    };
    struct Event {
        std::string clip;
        float time = 0.0f;        // seconds into the clip
        std::string name;         // "footstep", "mag_out", ...
    };

    // Two-bone IK after the graph's pose. Foot: the leg reaches the ground under the foot (the
    // game supplies the ground heights; the body drops for the lower foot). Hand: the hand keeps
    // the offset to `target` (a bone, e.g. the other hand on the weapon) it has in the reference
    // clip, so a two-handed grip holds while aim blends move the arms differently.
    enum class IkKind { Foot, Hand };
    struct IkChain {
        IkKind kind = IkKind::Foot;
        std::string name;
        std::string root, mid, end; // thigh, calf, foot / upper arm, forearm, hand
        float weight = 1.0f;
        std::string weightParam;    // empty = `weight`; else a float parameter 0..1
        std::string target;         // Hand: bone it follows
        std::string referenceClip;  // Hand: clip with the grip to keep
        float referenceTime = 0.0f;
    };

    // Ragdoll (dead characters, engine/Ragdoll.h): one capsule body per listed bone, from the bone
    // to its first listed child (or `length` along the bone), jointed to the nearest listed ancestor.
    struct RagdollBone {
        std::string bone;
        float radius = 0.08f;       // m
        float length = 0.0f;        // m, 0 = to the next ragdoll bone below (end bones: 2 x radius)
        float mass = 5.0f;          // kg
        float swing = 45.0f;        // degrees: cone the bone may swing in relative to its parent
        float twist = 20.0f;        // degrees either way around the bone
    };

    std::filesystem::path sourcePath;
    unsigned revision = 0;          // bumped by every edit (Animation editor): animators re-resolve
    std::vector<Param> params;
    std::vector<IkChain> ik;
    float footMaxAdjust = 0.45f;    // m: largest step up / down the feet adapt to
    std::vector<RagdollBone> ragdoll; // empty = no ragdoll
    bool ragdollOnDeath = true;     // the game switches dead characters to the ragdoll
    std::vector<Layer> layers;
    std::vector<Event> events;

    int findParam(const std::string& name) const;  // -1 if missing
    int findState(int layer, const std::string& name) const;
};

const char* animOpText(AnimGraph::Op op); // "", "!", ">", "<", ">=", "<=", "==", "!="
// Transition conditions as text: "dead !aim speed > 2" (all must hold). False on a bad comparison value.
bool parseAnimConditions(const std::string& text, std::vector<AnimGraph::Condition>& out);
std::string animConditionsText(const std::vector<AnimGraph::Condition>& conditions);

bool loadAnimGraphFile(AnimGraph& graph, const std::filesystem::path& file, std::string* error = nullptr);
bool saveAnimGraphFile(const AnimGraph& graph, const std::filesystem::path& file, std::string* error = nullptr);
// Cached per path: entities with the same graph share it; the editor edits it in place.
std::shared_ptr<AnimGraph> loadAnimGraph(const std::filesystem::path& file, std::string* error = nullptr);

// Playback state of a graph on one character (part of engine::Animator).
struct AnimGraphState {
    struct Layer {
        int state = -1, previous = -1;
        float phase = 0.0f, previousPhase = 0.0f; // 0..1 through the state's clips (blend spaces in sync)
        float time = 0.0f;                        // normalized time in the state, not wrapped (exit times)
        float fade = 0.0f, fadeDuration = 0.0f;   // crossfade from `previous`, seconds
    };
    std::vector<Layer> layers;
    std::vector<float> params;          // by AnimGraph::params index
    std::vector<std::string> events;    // fired during the last update (the next update clears them)
    const void* resolvedFor = nullptr;  // skeleton the indices below belong to (nullptr = resolve again)
    const void* resolvedGraph = nullptr;
    unsigned resolvedRevision = 0;
    std::vector<int> stateBase;         // per layer: index of its first state in `clips`
    std::vector<std::vector<int>> clips; // per state (stateBase[layer] + state): clip index per sample
    std::vector<int> maskNode;          // per layer: mask bone node, -1 = all bones
    std::vector<int> eventClip;         // per AnimGraph::events entry: clip index
    std::vector<std::array<int, 4>> ikNodes; // per AnimGraph::ik chain: root, mid, end, target node (-1 = missing)
    std::vector<glm::mat4> ikGrip;      // per chain (Hand): end bone relative to the target in the reference clip
    // Foot IK input from the game, in model units: lift per chain (ground under that foot relative
    // to the character's base) and the body drop. Smoothed by the caller.
    std::vector<float> ikWeight;        // per chain: weight eased toward its parameter
    std::vector<float> footLift;
    float pelvisDrop = 0.0f;
    bool footIk = false;                // the game fed ground heights this frame
};

} // namespace engine
