#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace engine {

// Players set the horizontal field of view as on a 16:9 screen (the usual "FOV 90"); the
// camera keeps the matching vertical one, so wider screens see more at the sides (Hor+).
float verticalFov(float horizontalDegrees16x9);

class FpsCamera {
public:
    glm::vec3 position{0.0f, 1.7f, 0.0f};
    float yawDegrees = -90.0f;   // -90 looks down -Z
    float pitchDegrees = 0.0f;   // clamped to +-89
    float fovDegrees = 90.0f;    // vertical FOV
    float sensitivity = 0.1f;    // degrees per mouse pixel
    float nearPlane = 0.05f;
    float farPlane = 500.0f;

    void look(glm::vec2 mouseDelta);

    glm::vec3 forward() const;      // full 3D view direction
    glm::vec3 flatForward() const;  // forward projected on the ground plane
    glm::vec3 right() const;

    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;
};

} // namespace engine
