﻿#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <memory>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "../3rdparty/stb_image.h"


/////////////////////////////////////////////////////////////////////////////////////

struct VertexPNT
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
};

static float CubeVertices[] =
{
    // positions          // normals           // texture coords
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
    0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  0.0f,
    0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
    0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,

    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
    0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  0.0f,
    0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
    0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,

    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
    -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
    -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

    0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
    0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
    0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
    0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
    0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
    0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,
    0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  1.0f,
    0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
    0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,

    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,
    0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  1.0f,
    0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
    0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f
};

static unsigned int CubeIndices[] = {
    0, 1, 3, // first triangle
    1, 2, 3  // second triangle
};

/////////////////////////////////////////////////////////////////////////////////////
// Experimental with scenegraph implementation
/////////////////////////////////////////////////////////////////////////////////////


struct TransformationState
{
    glm::mat4 modelview;
};

class Node
{
public:

    Node();
    Node(Node *parent)
    {
        m_parent = parent;
    }

    virtual ~Node();

    void addChild(Node *node)
    {
        m_children.push_back(node);
    }

    void removeChild(Node *node)
    {
        for(unsigned int i = 0; i < m_children.size(); ++i)
        {
            if(node == m_children[i])
            {
                m_children.erase(m_children.begin() + i);
            }
        }
    }

    Node* getParent()
    {
        return m_parent;
    }

    bool isGeometry() const
    {
        return m_isGeometry;
    }

    //enum class Type { None, GeometryNode, TransormNode };

protected:

    Node *m_parent = nullptr;

    Node *m_firstChild = nullptr;

    /// Next node in the same group of children.
    Node *m_prev = nullptr;

    /// Previous node in the same group of children.
    Node *m_next = nullptr;

    /// True for geometry nodes, false for non-geometry.
    bool m_isGeometry;

    std::vector<Node*> m_children;
};

class Transformation : public Node
{
public:

    Transformation() { m_isGeometry = false; }

    /// Applies the transformation to the state.
    virtual void apply(TransformationState *state) = 0;

    /// Removes the transformation from the state.
    virtual void revert(TransformationState *state) = 0;
};

class Translation : public Transformation
{
public:

    Translation() : m_translation(0.0f, 0.0f, 0.0f) {}
    virtual ~Translation() {}
    void apply(TransformationState *state);
    void revert(TransformationState *state);
    void set(const glm::vec3 &translation) { m_translation = translation; }

protected:

    glm::vec3 m_translation;
};

class Geometry : public Node
{
public:
    Geometry(Node* parent = nullptr);

    //void setMaterial(Material* m) { m_material = m; }
    //Material* material() const { return m_material; }

    //void setGeometry(Geometry* g) { m_geometry = g; }
    //Geometry* geometry() const { return m_geometry; }

private:
    //Material* m_material;
    //Geometry* m_geometry;

};

class Transform : public Node
{
public:
    Transform(Node* parent = nullptr);

    inline const  glm::mat4x4 matrix() const { return m_matrix; }
    inline void setMatrix(const glm::mat4x4& m) { m_matrix = m; }

private:
    glm::mat4x4 m_matrix;
};


namespace renderer {

const int ATTR_POSITION = 0;
const int ATTR_NORMAL = 1;
const int ATTR_TEXCOORDS = 2;

class Box
{
    Box()
    {
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(CubeVertices), CubeVertices, GL_STATIC_DRAW);

        glBindVertexArray(m_vao);
        glVertexAttribPointer(ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(ATTR_POSITION);

        glVertexAttribPointer(ATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(ATTR_NORMAL);

        glVertexAttribPointer(ATTR_TEXCOORDS, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(ATTR_TEXCOORDS);
    }

    ~Box()
    {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
    }

    void render()
    {
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, sizeof(CubeIndices));
    }

private:

    unsigned int m_vao;
    unsigned int m_vbo;
};

}
//class SceneGraph
//{
//public:
//    SceneGraph() {}
//};

enum CameraDirection {
    FORWARD_DIRECTIOM = 0,
    BACKWARD_DIRECTION = 1,
    LEFT_DIRECTION = 2,
    RIGHT_DIRECTION = 3
};

class CameraSettings
{
public:

    CameraSettings() : position(0.0f, 0.0f, 10.0f), up(0.0f, 5.0f, 0.0f),
        front(0.0f, 0.0f, -1.0f), cameraSpeed(5.0f), fov(45.0f), yaw(-90.0f),
        pitch(0.0f)
    {
    }

    glm::vec3 position;
    glm::vec3 up;
    glm::vec3 front;

    float cameraSpeed;
    float fov;
    float yaw;
    float pitch;
};

class FpsCamera
{
public:

    FpsCamera() : m_position(0.0f, 0.0f, 10.0f), m_up(0.0f, 5.0f,  0.0f),
        m_front(0.0f, 0.0f, -1.0f), m_cameraSpeed(5.0f)
    {
    }

    FpsCamera(CameraSettings &settings) : m_settings(settings)
    {
        m_position = settings.position;
        m_up = settings.up;
        m_front = settings.front;
        m_cameraSpeed = settings.cameraSpeed;
        m_fov = settings.fov;
        m_yaw = settings.yaw;
        m_pitch = settings.pitch;

        computeCamera();
    }

    ~FpsCamera() { }

    glm::vec3 getCameraPosition()
    {
        return m_position;
    }

    glm::vec3 getCameraFront()
    {
        return m_front;
    }

    glm::vec3 getCameraUp()
    {
        return m_up;
    }

    glm::vec3 getCameraRight()
    {
        return m_right;
    }

    float getSpeed()
    {
        return m_cameraSpeed;
    }

    float getFov()
    {
        return m_fov;
    }

    void setFov(float fov)
    {
        m_fov = fov;
    }

    void setFront(glm::vec3 front)
    {
        m_front = front;
    }

    void setRight(glm::vec3 right)
    {
        m_right = right;
    }

    void setUp(glm::vec3 up)
    {
        m_up = up;
    }

    void updateCameraDirection(CameraDirection direction, float deltaTime)
    {
        float cameraSpeed = m_cameraSpeed * deltaTime;
        if(direction == CameraDirection::FORWARD_DIRECTIOM)
        {
            m_position += cameraSpeed * m_front;
        }

        if(direction == CameraDirection::BACKWARD_DIRECTION)
        {
            m_position -= cameraSpeed * m_front;
        }

        if(direction == CameraDirection::LEFT_DIRECTION)
        {
            /*m_position -= glm::normalize(
                          glm::cross(m_front,
                          m_up)) * cameraSpeed;*/
            m_position -= cameraSpeed * m_right;
        }

        if(direction == CameraDirection::RIGHT_DIRECTION)
        {
            /*m_position += glm::normalize(
                          glm::cross(m_front,
                          m_up)) * cameraSpeed;*/

            m_position += cameraSpeed * m_right;
        }
    }

    void updateMouseCameraDirection(double xoffset, double yoffset)
    {
        double sensitivity = 0.1;
        xoffset *= sensitivity;
        yoffset *= sensitivity;

        m_yaw += xoffset;
        m_pitch += yoffset;

        if (m_pitch > 89.0f)
            m_pitch = 89.0f;
        if (m_pitch < -89.0f)
            m_pitch = -89.0f;

        computeCamera();
    }

    void updateCameraZoom(double xoffset, double yoffset)
    {
        if (m_fov >= 1.0f && m_fov <= 45.0f)
            m_fov -= yoffset;
        if (m_fov <= 1.0f)
            m_fov = 1.0f;
        if (m_fov >= 45.0f)
            m_fov = 45.0f;
    }

    glm::mat4 getLookAtCamera()
    {
       return glm::lookAt(m_position, m_position + m_front, m_up);
    }

    void computeCamera()
    {
        glm::vec3 front;
        front.x = cosf(glm::radians(m_yaw)) * cosf(glm::radians(m_pitch));
        front.y = sinf(glm::radians(m_pitch));
        front.z = sinf(glm::radians(m_yaw)) * cosf(glm::radians(m_pitch));
        m_front = glm::normalize(front);

        /*std::cerr << "front x : " << front.x
                  << " front y : " << front.y
                  << " front z : " << front.z << std::endl;*/

        //m_right = glm::normalize(glm::cross(m_front, m_up));
        //m_up    = glm::normalize(glm::cross(m_right, m_front));
    }

public:

    CameraSettings m_settings;

private:

    glm::vec3 m_position;
    glm::vec3 m_up;
    glm::vec3 m_front;
    glm::vec3 m_right;

    float m_cameraSpeed;
    float m_fov;
    float m_yaw;
    float m_pitch;
};

/*class Node
{
public:

    Node() { }
    ~Node() { }

    void glSetting()
    {
        glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    }

    void render()
    {
        for(unsigned int i = 0; i < m_childrens.size(); i++)
        {
            m_childrens.at(i)->render();
        }
    }

    void addChild(Node *node)
    {
        m_childrens.push_back(node);
    }

    void removeChild(Node *node)
    {
        for(unsigned int i = 0; i < m_childrens.size(); ++i)
        {
            if(node == m_childrens[i])
            {
                m_childrens.erase(m_childrens.begin() + i);
            }
        }
    }

private:

    std::vector<Node*> m_childrens;

};

class TransFormation : public Node
{
public:

    TransFormation() {}
    void translate(glm::vec3 &translate);
    void scale(glm::vec3 &scale);
    void rotation(glm::vec3 &rotation);
};


class Box : public Node
{
    Box()
    {
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(CubeVertices), CubeVertices, GL_STATIC_DRAW);

        glBindVertexArray(m_vao);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
    }

    ~Box()
    {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
    }

    void render()
    {
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, sizeof(CubeIndices));
    }

private:

    unsigned int m_vao;
    unsigned int m_vbo;
};
*/
/*
class SceneGraph
{
public:

    SceneGraph() {}
    void addNode()

};*/


//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

static float vertices[] = {
      // positions          // normals           // texture coords
      -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
       0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  0.0f,
       0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
       0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
      -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  1.0f,
      -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,

      -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
       0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  0.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
      -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  1.0f,
      -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,

      -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
      -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
      -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
      -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
      -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
      -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

       0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
       0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
       0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
       0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
       0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
       0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

      -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,
       0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  1.0f,
       0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
       0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
      -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  0.0f,
      -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,

      -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,
       0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  1.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
      -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  0.0f,
      -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f
  };

static unsigned int indices[] = {
        0, 1, 3, // first triangle
        1, 2, 3  // second triangle
    };

static float skyboxVertices[] = {
    // positions
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    -1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f
};

/*
const char* shaderVertex =
"#version 330 core\n"
"uniform mat4 model;\n"
"uniform mat4 view;\n"
"uniform mat4 projection;\n"
"attribute vec3 vCol;\n"
"attribute vec3 vPos;\n"
"attribute vec2 aTexCoord;\n"
"out vec3 color;\n"
"out vec2 TexCoord;"
"void main()\n"
"{\n"
"    gl_Position = projection * view * model * vec4(vPos, 1.0);\n"
"    TexCoord = vec2(aTexCoord.x, aTexCoord.y);\n"
"    color = vCol;\n"
"}\n";

const char* shaderFragment =
"#version 330 core\n"
"varying vec3 color;\n"
"varying vec2 TexCoord;\n"
"uniform sampler2D texture1;\n"
"void main()\n"
"{\n"
"    //gl_FragColor = vec4(color, 1.0);\n"
"    gl_FragColor = texture(texture1, TexCoord);\n"
"}\n";
*/

class Light
{
public:

    Light() {}
    Light(glm::vec3 &position) : m_position(position)
    {

    }

    ~Light() {}

    void setPosition(glm::vec3 position)
    {
        m_position = position;
    }

    glm::vec3 getPosition()
    {
        return m_position;
    }

    glm::vec3 getDiffuseColor()
    {
        return m_diffuse;
    }

    glm::vec3 getSpelecularColor()
    {
        return m_specular;
    }

    glm::vec3 getAmbientColor()
    {
        return m_ambient;
    }

    void setDirection(glm::vec3 &direction)
    {
        m_direction = direction;
    }

    void setDiffuseColor(glm::vec3 diffuse)
    {
        m_diffuse = diffuse;
    }

    void setSpecularColor(glm::vec3 specular)
    {
        m_specular = specular;
    }

    void setAmbientColor(glm::vec3 ambient)
    {
        m_ambient = ambient;
    }

private:

    glm::vec3 m_position;
    glm::vec3 m_direction;
    glm::vec3 m_diffuse;
    glm::vec3 m_specular;
    glm::vec3 m_ambient;

};



///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

CameraSettings settings;
FpsCamera g_camera(settings);

bool firstMouse = true;
double lastX =  800.0 / 2.0;
double lastY =  600.0 / 2.0;

class GLSettings
{
public:

    GLSettings() : majorVersion(3), minorVersion(3),
        windowHeight(640), windowWidth(480), windowName("Hello") { }

    // Major version OpenGL, Minor version OpenGL
    int majorVersion, minorVersion;

    // Window width, height,
    int windowHeight, windowWidth;

    // Main window name
    std::string windowName;
};

class GLWindow
{
public:

    GLWindow(GLSettings &settings) : m_settings(settings)
    {

        if(!glfwInit())
        {
            std::cout << "Failed initilize GLFW library!" << std::endl;
            glfwTerminate();
        }

        glfwSetErrorCallback(errorCallback);

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, m_settings.majorVersion);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, m_settings.minorVersion);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        std::cout << "Major: " << m_settings.majorVersion
                  << " Minor: "<< m_settings.minorVersion;

        createWindow(m_settings.windowHeight,
                     m_settings.windowWidth,
                     m_settings.windowName);
    }

    ~GLWindow()
    {
        glfwTerminate();
    }

    int getWidthWindow()
    {
        return m_settings.windowWidth;
    }

    int getHeightWindow()
    {
        return m_settings.windowHeight;
    }

    int checkCloseWindow()
    {
        return glfwWindowShouldClose(m_window);
    }

    void checkPoolEvents()
    {
        glfwPollEvents();
    }

    void checkSwapBuffer()
    {
        glfwSwapBuffers(m_window);
    }

    void updateFpsCounter()
    {
        static double prevSecond = glfwGetTime();
        static int frameCounter = 0;

        double currentSecond = glfwGetTime();
        double elapsedSecond = currentSecond - prevSecond;

        if(elapsedSecond > 0.25)
        {
            prevSecond = currentSecond;
            double fps = static_cast<double>(frameCounter) / elapsedSecond;

            char buffer[256];
            sprintf(buffer, "fps: %.2f", fps);
            glfwSetWindowTitle(m_window, buffer);
            frameCounter = 0;
        }

        frameCounter++;
    }

    static void errorCallback(int error, const char* description)
    {
        fprintf(stderr, "Error callback: %s\n", description);
    }

    /*static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {

    }*/

    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        g_camera.updateCameraZoom(xoffset, yoffset);
    }

    static void mouseCursorPositionCallback(GLFWwindow* window, double x, double y)
    {
        if (firstMouse)
        {
            lastX = x;
            lastY = y;
            firstMouse = false;
        }

        double xoffset = x - lastX;
        double yoffset = lastY - y;

        lastX = x;
        lastY = y;

        g_camera.updateMouseCameraDirection(xoffset, yoffset);
    }

    static void keyboardCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
               glfwSetWindowShouldClose(window, true);
        }
    }

    void getFramebufferSize(int *width, int *height)
    {
        glfwGetFramebufferSize(m_window, width, height);
    }

    void updateframeBufferSize()
    {
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        glViewport(0, 0, width, height);
    }

    void updateCamera(float deltaTime)
    {
        if(glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS)
        {
            g_camera.updateCameraDirection(CameraDirection::FORWARD_DIRECTIOM,
                                     deltaTime);
        }

        if(glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS)
        {
            g_camera.updateCameraDirection(CameraDirection::BACKWARD_DIRECTION,
                                     deltaTime);
        }

        if(glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS)
        {
            g_camera.updateCameraDirection(CameraDirection::LEFT_DIRECTION,
                                     deltaTime);
        }

        if(glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS)
        {
            g_camera.updateCameraDirection(CameraDirection::RIGHT_DIRECTION,
                                     deltaTime);
        }
    }

protected:

    void createWindow(int width, int height, std::string name)
    {
        m_window = glfwCreateWindow(width, height, name.c_str(), NULL, NULL);
        if(!m_window)
        {
            std::cout << "Failed create GLFW window!" << std::endl;
            glfwTerminate();
        }

       // Make the window's context current
       glfwMakeContextCurrent(m_window);

       //glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
       glfwSetKeyCallback(m_window, keyboardCallback);
       glfwSetCursorPosCallback(m_window, mouseCursorPositionCallback);
       glfwSetScrollCallback(m_window, scrollCallback);

       glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

       // start GLEW extension handler
       glewExperimental = GL_TRUE;
       glewInit();
    }

private:

    GLSettings m_settings;
    GLFWwindow *m_window;
};

enum TypeShader
{
    VERTEX_SHADER = GL_VERTEX_SHADER,
    FRAGMENT_SHADER = GL_FRAGMENT_SHADER,
    GEOMETRY_SHADER = GL_GEOMETRY_SHADER
};

class Shader
{
public:

    Shader() : m_vertexShader(0), m_fragmentShader(0),
        m_geometryShader(0), m_isVertexShader(false),
        m_isFragmentShader(false), m_isGeometryShader(false)
    { }

    ~Shader() {}

    void loadShader(const std::string &shader, TypeShader type)
    {
        if(type == TypeShader::VERTEX_SHADER)
        {
            std::string vertexshader = getShaderReader(shader);
            const char *vertex_shader = vertexshader.c_str();

            m_vertexShader = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(m_vertexShader, 1, &vertex_shader, NULL);
            glCompileShader(m_vertexShader);
            shaderCompileStatus(m_vertexShader);
            m_isVertexShader = true;
        }

        if(type == TypeShader::FRAGMENT_SHADER)
        {
            std::string fragmentshader = getShaderReader(shader);
            const char *fragment_shader = fragmentshader.c_str();
            m_fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(m_fragmentShader, 1, &fragment_shader, NULL);
            glCompileShader(m_fragmentShader);
            shaderCompileStatus(m_fragmentShader);
            m_isFragmentShader = true;
        }

        if(type == TypeShader::GEOMETRY_SHADER)
        {
            std::string geometryshader = getShaderReader(shader);
            const char *geometry_shader = geometryshader.c_str();
            m_geometryShader = glCreateShader(GL_GEOMETRY_SHADER);
            glShaderSource(m_geometryShader, 1, &geometry_shader, NULL);
            glCompileShader(m_geometryShader);
            m_isGeometryShader = true;
        }
    }

    void useShaderProgram()
    {
        glUseProgram(m_id);
    }

    GLuint getShaderProgram()
    {
        return m_id;
    }

    void createShaderProgram()
    {
        m_id = glCreateProgram();

        if(m_isVertexShader)
        {
            glAttachShader(m_id, m_vertexShader);
        }
        if(m_isFragmentShader)
        {
            glAttachShader(m_id, m_fragmentShader);
        }
        if(m_isGeometryShader)
        {
            glAttachShader(m_id, m_geometryShader);
        }

        glLinkProgram(m_id);
        programCompileStatus(m_id);
    }

    std::string getShaderReader(const std::string &shader)
    {
        std::ifstream file(shader);
        std::stringstream buffer;

        if(file.is_open())
        {
            buffer << file.rdbuf();
        }
        else
        {
            std::cerr << "Cannot open file! " << ""<< shader << ""<< std::endl;
        }

        std::string contents(buffer.str());
        //std::cerr << contents << std::endl;
        return contents;
    }

    void setUniformMatrix4x4(const std::string &type, const glm::mat4 &matrix)
    {
        glUniformMatrix4fv(glGetUniformLocation(m_id, type.c_str()), 1, GL_FALSE, &matrix[0][0]);
    }

    void setUniformInt(const std::string &type, const GLint value)
    {
        glUniform1i(glGetUniformLocation(m_id, type.c_str()), value);
    }

    void setUniformFloat(const std::string &type, const GLfloat value)
    {
        glUniform1f(glGetUniformLocation(m_id, type.c_str()), value);
    }

    void setUnifromVec2(const std::string &type, const glm::vec3 &value)
    {
        glUniform2f(glGetUniformLocation(m_id, type.c_str()), value.x, value.y);
    }

    void setUnifromVec2(const std::string &type, const float &x, const float &y)
    {
        glUniform2f(glGetUniformLocation(m_id, type.c_str()), x, y);
    }

    void setUnifromVec3(const std::string &type, const glm::vec3 value)
    {
        glUniform3fv(glGetUniformLocation(m_id, type.c_str()), 1, &value[0]);
    }

    void setUnifromVec3(const std::string &type, const float &x, const float &y, const float &z)
    {
        glUniform3f(glGetUniformLocation(m_id, type.c_str()), x, y, z);
    }

protected:

    void shaderCompileStatus(GLuint shader)
    {
        GLint isCompiled;

        glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
        if(!isCompiled)
        {
            int logLenght;
            GLchar log[1024];
            glGetShaderInfoLog(shader, 1024, &logLenght, log);
            std::cerr << "[WARN] Shader compilation error : "  << log <<
                         " - Log lenght: " << logLenght <<
                         "\n";
        }
        else
        {
            int logLenght;
            GLchar log[1024];
            glGetShaderInfoLog(shader, 1024, &logLenght, log);
            std::cerr << "[INFO] Shader compilation success ! " << log <<
                         " - Log lenght: " << logLenght <<
                         "\n";
        }
    }

    void programCompileStatus(GLuint program)
    {
        GLint isCompiled;

        glGetProgramiv(program, GL_LINK_STATUS, &isCompiled);
        if(!isCompiled)
        {
            int logLenght;
            GLchar log[1024];
            glGetProgramInfoLog(program, 1024, &logLenght, log);
            std::cerr << "[WARN] Program linker error : "  << log <<
                         " - Log lenght: " << logLenght <<
                         "\n";
        }
        else
        {
            int logLenght;
            GLchar log[1024];
            glGetProgramInfoLog(program, 1024, &logLenght, log);
            std::cerr << "[INFO] Pogram linker success ! " << log <<
                         " - Log lenght: " << logLenght <<
                         "\n";
        }
    }

private:

    // Shader program id
    GLuint m_id;
    GLuint m_vertexShader;
    GLuint m_fragmentShader;
    GLuint m_geometryShader;

    bool m_isVertexShader;
    bool m_isFragmentShader;
    bool m_isGeometryShader;
};

class Texture
{
public:

    Texture() {}
    ~Texture() {}

    unsigned int loadTexture(const std::string &name)
    {
        glGenTextures(1, &m_id);
        int width, height, nrChannels;
        unsigned char *data = stbi_load(name.c_str(), &width, &height, &nrChannels, 0);
        if(data)
        {
            GLenum format = 0;
            if (nrChannels == 1)
            {
                format = GL_RED;
            }
            else if (nrChannels == 3)
            {
                format = GL_RGB;
            }
            else if (nrChannels == 4)
            {
                format = GL_RGBA;
            }

            glBindTexture(GL_TEXTURE_2D, m_id);
            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            std::cerr << "Load texture: " << name << std::endl;
            stbi_image_free(data);
        }
        else
        {
            std::cerr << "Failed to load texture: " << name << std::endl;
            stbi_image_free(data);
        }

        return m_id;
    }

    unsigned int loadCubeTexture(std::vector<std::string> &faces)
    {
        unsigned int textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

        int width, height, nrChannels;
        for (unsigned int i = 0; i < faces.size(); i++)
        {
            unsigned char *data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
            if(data)
            {
                GLenum format = 0;
                if (nrChannels == 1)
                {
                    format = GL_RED;
                }
                else if (nrChannels == 3)
                {
                    format = GL_RGB;
                }
                else if (nrChannels == 4)
                {
                    format = GL_RGBA;
                }

                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            }
            else
            {
                std::cout << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
                stbi_image_free(data);
            }
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        return textureID;
    }


private:

    unsigned int m_id;
};


int main(void)
{

    //SceneGraph sceneGraph;
    //sceneGraph.glSetting();

    GLSettings settings;
    settings.windowName = "Hello OpenGL";
    settings.windowHeight = 800;
    settings.windowWidth = 600;

    GLWindow window(settings);

    Shader mainShader;
    mainShader.loadShader("shader.vert", TypeShader::VERTEX_SHADER);
    mainShader.loadShader("shader.frag", TypeShader::FRAGMENT_SHADER);
    mainShader.createShaderProgram();

    Shader skyShader;
    skyShader.loadShader("skybox.vert", TypeShader::VERTEX_SHADER);
    skyShader.loadShader("skybox.frag", TypeShader::FRAGMENT_SHADER);
    skyShader.createShaderProgram();

    Shader lightShader;
    lightShader.loadShader("light.vert", TypeShader::VERTEX_SHADER);
    lightShader.loadShader("light.frag", TypeShader::FRAGMENT_SHADER);
    lightShader.createShaderProgram();

    GLuint /*mvp_location,*/ vpos_location, vcol_location, tex_location;
    //mvp_location  = glGetUniformLocation(shader.getShaderProgram(), "MVP");
    vpos_location = glGetAttribLocation(mainShader.getShaderProgram(), "vPos");
    vcol_location = glGetAttribLocation(mainShader.getShaderProgram(), "vCol");
    tex_location  = glGetAttribLocation(mainShader.getShaderProgram(), "aTexCoord");


    /////////////////////////////////////////////////////////////
    /// Box vbo, vao
    ///
    unsigned int vbo, vaoCube;
    glGenVertexArrays(1, &vaoCube);
    glGenBuffers(1, &vbo);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindVertexArray(vaoCube);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);


    /////////////////////////////////////////////////////////////
    /// Light vao
    ///
    unsigned int lightVAO;
    glGenVertexArrays(1, &lightVAO);
    glBindVertexArray(lightVAO);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    /////////////////////////////////////////////////////////////
    /// Skybox vao, vbo
    ///
    unsigned int skyboxVAO, skyboxVBO;
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    std::vector<std::string> skyTextures
    {
        "skybox/posx.png",
        "skybox/negx.png",
        "skybox/posy.png",
        "skybox/negy.png",
        "skybox/posz.png",
        "skybox/negz.png"
    };

    Texture skyboxTexture;
    unsigned int skyBoxTexture = skyboxTexture.loadCubeTexture(skyTextures);

    skyShader.useShaderProgram();
    skyShader.setUniformInt("skybox", 0);

    std::vector<glm::vec3> randomPosition;
    const int countObjects = 40;
    for(int i = 0; i < countObjects; i++)
    {
        randomPosition.push_back(glm::vec3(static_cast <float> (rand() % 30 - 1) ,
                         static_cast <float> (rand() % 20 - 1) ,
                         static_cast <float> (rand() % 10) - 1));
    }

    float deltaTime = 0.0f;	// time between current frame and last frame
    float lastFrame = 0.0f;

    //glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;

    Light light;
    light.setPosition(glm::vec3(1.2f, 1.0f, 2.0f));
    light.setAmbientColor(glm::vec3(0.2f, 0.2f, 0.2f));
    light.setDiffuseColor(glm::vec3(0.5f, 0.5f, 0.5f));
    light.setSpecularColor(glm::vec3(1.0f, 1.0f, 1.0f));

    Texture diffuseTexture;
    Texture specularTexture;
    unsigned int diffuseTex  = diffuseTexture.loadTexture("box/box-diffuse.png");
    unsigned int specularTex = specularTexture.loadTexture("box/box-specular.png");

    lightShader.useShaderProgram();
    lightShader.setUniformInt("material.diffuse", 0);
    lightShader.setUniformInt("material.specular", 1);

    // Loop until the user closes the window
    while(!window.checkCloseWindow())
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        window.updateFpsCounter();
        window.updateframeBufferSize();
        window.updateCamera(deltaTime);

        /////////////////////////////////////////////////////////////////
        // wipe the drawing surface clear
        glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

        /////////////////////////////////////////////////////////////////
        /// Use light shader
        lightShader.useShaderProgram();
        lightShader.setUnifromVec3("light.position", light.getPosition());
        lightShader.setUnifromVec3("viewPos", g_camera.getCameraPosition());

        /////////////////////////////////////////////////////////////////
        // light properties
        lightShader.setUnifromVec3("light.ambient", light.getAmbientColor());
        lightShader.setUnifromVec3("light.diffuse", light.getDiffuseColor());
        lightShader.setUnifromVec3("light.specular", light.getSpelecularColor());
        lightShader.setUniformFloat("material.shininess", 64.0f);

        //model = glm::rotate(model, glm::radians(50.0f), glm::vec3(0.5f, 1.0f, 0.0f));
        // view  = glm::translate(view, glm::vec3(0.0f, 0.0f, -5.0f));
        //float radius = 6.0f;
        //float camX = sin(glfwGetTime()) * radius;
        //float camZ = cos(glfwGetTime()) * radius;
        //view = glm::lookAt(glm::vec3(camX, 0.0, camZ), glm::vec3(0.0, 0.0, 0.0), glm::vec3(0.0, 1.0, 0.0));

        projection = glm::perspective(glm::radians(g_camera.getFov()),
                                      static_cast<float>(window.getWidthWindow()) /
                                      static_cast<float>(window.getHeightWindow()),
                                      0.1f,
                                      100.0f);

        view = g_camera.getLookAtCamera();
        lightShader.setUniformMatrix4x4("projection", projection);
        lightShader.setUniformMatrix4x4("view", view);


        for(unsigned int i = 0; i < countObjects; i++)
        {
            glm::mat4 model;
            model = glm::translate(model, randomPosition[i]);
            //float angle = 20.0f * i;
            //model = glm::rotate(model, glm::radians(0.0f), glm::vec3(1.0f, 0.3f, 0.5f));
            //glm::mat4 model;
            lightShader.setUniformMatrix4x4("model", model);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, diffuseTex);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, specularTex);

            glBindVertexArray(vaoCube);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        mainShader.useShaderProgram();
        mainShader.setUniformMatrix4x4("projection", projection);
        mainShader.setUniformMatrix4x4("view", view);

        glm::mat4 models;
        models = glm::translate(models, light.getPosition());
        models = glm::scale(models, glm::vec3(0.2f));
        mainShader.setUniformMatrix4x4("model", models);

        glBindVertexArray(lightVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // skybox
        glDepthFunc(GL_LEQUAL);
        skyShader.useShaderProgram();
        view = glm::mat4(glm::mat3(g_camera.getLookAtCamera()));
        skyShader.setUniformMatrix4x4("view", view);
        skyShader.setUniformMatrix4x4("projection", projection);

        // skybox cube
        glBindVertexArray(skyboxVAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyBoxTexture);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthFunc(GL_LESS); // set depth function back to default

        window.checkPoolEvents();
        window.checkSwapBuffer();
    }

    glDeleteVertexArrays(1, &vaoCube);
    glDeleteVertexArrays(1, &lightVAO);
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &skyboxVBO);

    return 0;
}
