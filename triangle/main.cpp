#include <iostream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

float vertices[] = {
    0.0f,  0.5f,  0.0f,
    0.5f, -0.5f,  0.0f,
   -0.5f, -0.5f,  0.0f
};

const char* shader_vert =
"#version 330 core\n"
"layout (location = 0) in vec3 pos;\n"
"void main()\n"
"{\n"
"  gl_Position = vec4(pos.x, pos.y, pos.z, 1.0);\n"
"}\0";

const char* shader_frag =
"#version 330 core\n"
"out vec4 FragColor;\n"
"void main()\n"
"{\n"
"   FragColor = vec4(1.0f, 0.9f, 0.2f, 1.0f);\n"
"}\n\0";

class GLWindow
{
public:

    GLWindow() : m_major(3), m_minor(3), m_initialize(true), m_window(NULL)
    {
        if(!glfwInit())
        {
            std::cout << "Failed initilize GLFW library!" << std::endl;
            m_initialize = false;
        }
    }

    ~GLWindow() {}

    void setOpenGLversion(int major, int minor)
    {
        m_major = major;
        m_minor = minor;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, m_major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, m_minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    void createWindow(int x , int y, std::string name)
    {
        m_window = glfwCreateWindow(x, y, name.c_str(), NULL, NULL);
        if(!m_window)
        {
            std::cout << "Failed create GLFW window!" << std::endl;
            glfwTerminate();
        }

        /* Make the window's context current */
        glfwMakeContextCurrent(m_window);
    }

    int checkCloseWindow()
    {
        return glfwWindowShouldClose(m_window);
    }

    void checkPoolEvents()
    {
        // update other events like input handling
        glfwPollEvents();
    }

    void checkSwqpBuffers()
    {
        // put the stuff we've been drawing onto the display
        glfwSwapBuffers(m_window);
    }

    void destroyWindow()
    {
         glfwTerminate();
    }

private:

    int m_major;
    int m_minor;

    bool m_initialize;
    GLFWwindow *m_window;
};


int main(void)
{

    GLWindow w;
    w.setOpenGLversion(3, 3);
    w.createWindow(640, 480, "Hello OpenGL");

    // start GLEW extension handler
    glewExperimental = GL_TRUE;
    glewInit();

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &shader_vert, NULL);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &shader_frag, NULL);
    glCompileShader(fs);

    GLuint shader_programme = glCreateProgram();
    glAttachShader(shader_programme, fs);
    glAttachShader(shader_programme, vs);
    glLinkProgram(shader_programme);

    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 9 * sizeof(float), vertices, GL_STATIC_DRAW);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    /* Loop until the user closes the window */
    while (!w.checkCloseWindow())
    {
        // wipe the drawing surface clear
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shader_programme);

        glBindVertexArray(vao);
        // draw points 0-3 from the currently bound VAO with current in-use shader
        glDrawArrays(GL_TRIANGLES, 0, 3);

        w.checkPoolEvents();
        w.checkSwqpBuffers();
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);

    w.destroyWindow();

    return 0;
}
