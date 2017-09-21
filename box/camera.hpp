#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>

class Camera
{
public:

    Camera() : m_position(0.0f, 0.0f, 0.0f), m_direction(0.0f, 0.0f, 0.0f),
        m_front(0.0f, 0.0f, 0.0f), m_up(0.0f, 0.0f, 0.0f), m_cameraSpeed(2.0f)
    {

    }

    glm::vec3 getCameraPosition()
    {
        return m_position;
    }

    glm::vec3 getCameraDirection()
    {
        return m_direction;
    }

    glm::vec3 getCameraFront()
    {
        return m_front;
    }

    glm::vec3 getCameraUp()
    {
        return m_up;
    }

    float getSpeed()
    {
        return m_cameraSpeed;
    }

    void updateCameraPosition(glm::vec3 &position)
    {
        m_position = position;
    }

    void updateSpeedCamera(float deltaTime)
    {
        m_cameraSpeed = m_cameraSpeed * deltaTime;
    }

private:

    glm::vec3 m_position;
    glm::vec3 m_direction;
    glm::vec3 m_front;
    glm::vec3 m_up;
    float m_cameraSpeed;
};

#endif // CAMERA_HPP
