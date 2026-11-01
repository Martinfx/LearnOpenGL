#pragma once
#include "engine/AnimGraph.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// Node hierarchy, skins and animation clips of a glTF character.
struct Skeleton {
    struct Node {
        std::string name;
        int parent = -1;                       // parents always come before their children
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };
    struct Skin {
        std::vector<int> joints;               // node indices
        std::vector<glm::mat4> inverseBind;    // one per joint
    };
    enum class Path { Translation, Rotation, Scale };
    struct Channel {
        int node = 0;
        Path path = Path::Translation;
        bool step = false;                     // STEP interpolation, otherwise linear
        std::vector<float> times;
        std::vector<glm::vec4> values;         // xyz for T/S, xyzw quaternion for R
    };
    struct Clip {
        std::string name;
        float duration = 0.0f;                 // seconds
        std::vector<Channel> channels;
    };

    std::vector<Node> nodes;
    std::vector<Skin> skins;
    std::vector<Clip> clips;

    int findClip(std::string_view name) const; // -1 if missing
};

// Playback state of one animated object: the current clip plus a crossfade
// from the previous one, so switching walk -> run -> attack is smooth.
// With an animation graph the graph drives the pose (states, blend spaces, layers) while no
// clip is played explicitly: play() overrides the graph, stop() hands control back to it.
struct Animator {
    struct Track {
        int clip = -1;
        float time = 0.0f;
        float speed = 1.0f;
        bool loop = true;
    };
    Track current;
    Track previous;
    float fadeTime = 0.0f;     // time since the switch
    float fadeDuration = 0.0f; // 0 = no crossfade in progress

    // Starts `name`. Playing the clip that already runs only updates loop/speed
    // unless `restart` is set, so scripts can call this every update.
    bool play(const Skeleton& skeleton, std::string_view name, bool loop = true, float speed = 1.0f,
              float fade = 0.2f, bool restart = false);
    void stop();
    void update(const Skeleton& skeleton, float dt);
    bool finished(const Skeleton& skeleton) const; // a non-looping clip reached its end
    std::string clipName(const Skeleton& skeleton) const; // clip, or the base layer's state with a graph

    std::shared_ptr<const AnimGraph> graph;
    AnimGraphState graphState;
    void setGraph(std::shared_ptr<const AnimGraph> g);
    bool graphActive() const { return graph && current.clip < 0; }
    bool setParam(const std::string& name, float value); // false if the graph has no such parameter
    float param(const std::string& name) const;
    bool trigger(const std::string& name);               // sets a trigger; a transition using it consumes it
    std::string stateName(int layer = 0) const;
    // Re-reads clip and bone names after the graph was edited (keeps parameters and states when possible).
    void graphChanged() { graphState.resolvedFor = nullptr; }
};

// Model-space matrix of every node for the animator's current pose
// (rest pose when animator is null or not playing).
void evaluatePose(const Skeleton& skeleton, const Animator* animator, std::vector<glm::mat4>& globals);

// Skinning matrix per joint of `skin` for the pose in `globals`.
void skinMatrices(const Skeleton& skeleton, int skin, const std::vector<glm::mat4>& globals, std::vector<glm::mat4>& out);

} // namespace engine
