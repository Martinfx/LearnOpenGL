#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace engine {

class FpsCamera {
public:
    glm::vec3 position{0.0f, 1.65f, 0.0f};
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
