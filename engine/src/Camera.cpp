#include "engine/Camera.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace engine {

void FpsCamera::look(glm::vec2 mouseDelta)
{
    yawDegrees += mouseDelta.x * sensitivity;
    pitchDegrees = std::clamp(pitchDegrees - mouseDelta.y * sensitivity, -89.0f, 89.0f);
    yawDegrees = std::fmod(yawDegrees, 360.0f);
}

glm::vec3 FpsCamera::forward() const
{
    const float yaw = glm::radians(yawDegrees);
    const float pitch = glm::radians(pitchDegrees);
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
}

glm::vec3 FpsCamera::flatForward() const
{
    const float yaw = glm::radians(yawDegrees);
    return glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
}

glm::vec3 FpsCamera::right() const
{
    return glm::normalize(glm::cross(flatForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::mat4 FpsCamera::view() const
{
    return glm::lookAt(position, position + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 FpsCamera::projection(float aspect) const
{
    return glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
}

} // namespace engine
