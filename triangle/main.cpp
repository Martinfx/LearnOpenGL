#include <iostream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

float vertices[] = {
    0.0f,  0.5f,  0.0f,
    0.5f, -0.5f,  0.0f,
   -0.5f, -0.5f,  0.0f
};

const char* shaderVertex =
"#version 330 core\n"
"layout (location = 0) in vec3 pos;\n"
"void main()\n"
"{\n"
"  gl_Position = vec4(pos.x, pos.y, pos.z, 1.0);\n"
"}\0";

const char* shaderFragment =
"#version 330 core\n"
"out vec4 FragColor;\n"
"void main()\n"
"{\n"
"   FragColor = vec4(1.0f, 0.9f, 0.2f, 1.0f);\n"
"}\n\0";

class GLSettings
{
public:

    GLSettings() : major_version(3), minor_version(3),
        window_height(640), window_width(480), name_window("Hello") { }

    // Major version OpenGL
    int major_version;

    // Minor version OpenGL
    int minor_version;

    // Window width and height, name window
    int window_height;
    int window_width;

    // Main window name
    std::string name_window;
};

class GLWindow
{
public:

    GLWindow() { }

    GLWindow(GLSettings &settings) : m_settings(settings), m_window(NULL)
    {
        if(!glfwInit())
        {
            std::cout << "Failed initilize GLFW library!" << std::endl;
            glfwTerminate();
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, m_settings.major_version);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, m_settings.minor_version);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        createWindow(m_settings.window_height, m_settings.window_width, m_settings.name_window);
    }

    ~GLWindow() {}

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
    }

private:

    GLSettings m_settings;
    GLFWwindow *m_window;
};


int main(void)
{

    GLSettings settings;
    settings.name_window = "Hello OpenGL";
    settings.major_version = 3;
    settings.minor_version = 3;
    settings.window_height = 640;
    settings.window_width = 480;

    GLWindow w(settings);

    // start GLEW extension handler
    glewExperimental = GL_TRUE;
    glewInit();

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &shaderVertex, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &shaderFragment, NULL);
    glCompileShader(fs);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, fs);
    glAttachShader(shaderProgram, vs);
    glLinkProgram(shaderProgram);

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

        glUseProgram(shaderProgram);

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
