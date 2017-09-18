#include <iostream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

float vertices[] = {
    // positions         // colors
     0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,   // bottom right
    -0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,   // bottom left
     0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f    // top
};

const char* shaderVertex =
"#version 330 core\n"
"uniform mat4 MVP;\n"
"attribute vec3 vCol;\n"
"attribute vec2 vPos;\n"
"varying vec3 color;\n"
"void main()\n"
"{\n"
"    gl_Position = MVP * vec4(vPos, 0.0, 1.0);\n"
"    color = vCol;\n"
"}\n";

const char* shaderFragment =
"#version 330 core\n"
"varying vec3 color;\n"
"void main()\n"
"{\n"
"    gl_FragColor = vec4(color, 1.0);\n"
"}\n";

class GLSettings
{
public:

    GLSettings() : majorVersion(3), minorVersion(3),
        windowHeight(640), windowWidth(480), windowName("Hello") { }

    // Major version OpenGL
    int majorVersion;

    // Minor version OpenGL
    int minorVersion;

    // Window width and height, name window
    int windowHeight;
    int windowWidth;

    // Main window name
    std::string windowName;
};

class GLWindow
{
public:

    GLWindow() { }

    GLWindow(GLSettings &settings) : m_settings(settings), m_window(NULL)
    {
        glfwSetErrorCallback(errorCallback);

        if(!glfwInit())
        {
            std::cout << "Failed initilize GLFW library!" << std::endl;
            glfwTerminate();
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, m_settings.majorVersion);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, m_settings.minorVersion);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        createWindow(m_settings.windowHeight, m_settings.windowWidth, m_settings.windowName);
    }

    ~GLWindow() {}

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
        // update other events like input handling
        glfwPollEvents();
    }

    void checkSwapBuffer()
    {
        // put the stuff we've been drawing onto the display
        glfwSwapBuffers(m_window);
    }

    void destroyWindow()
    {
        glfwTerminate();
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

    void getFramebufferSize(int *width, int *height)
    {
        glfwGetFramebufferSize(m_window, width, height);
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
    settings.windowName = "Hello OpenGL";
    settings.windowHeight = 800;
    settings.windowWidth = 600;

    GLWindow window(settings);

    // start GLEW extension handler
    glewExperimental = GL_TRUE;
    glewInit();

    GLint mvp_location, vpos_location, vcol_location;

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

    mvp_location = glGetUniformLocation(shaderProgram, "MVP");
    vpos_location = glGetAttribLocation(shaderProgram, "vPos");
    vcol_location = glGetAttribLocation(shaderProgram, "vCol");

    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 9 * sizeof(float), vertices, GL_STATIC_DRAW);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glEnableVertexAttribArray(vpos_location);
    glVertexAttribPointer(vpos_location,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          6 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(vcol_location);

    glVertexAttribPointer(vcol_location,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          6 * sizeof(float), (void*)(3* sizeof(float)));

    // Loop until the user closes the window
    while (!window.checkCloseWindow())
    {
        window.updateFpsCounter();

        float ratio;
        int width, height;
        glm::mat4x4 m, p, mvp;

        window.getFramebufferSize(&width, &height);
        ratio = static_cast<float>(width) / static_cast<float>(height);

        glViewport(0, 0, width, height);

        // wipe the drawing surface clear
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 transform;
        transform = glm::translate(transform, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::rotate(transform, glm::radians(0.0f), glm::vec3(10.0f, 0.0f, 0.0f));

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(mvp_location, 1, GL_FALSE, glm::value_ptr(transform));
        glBindVertexArray(vao);

        // draw points 0-3 from the currently bound VAO with current in-use shader
        glDrawArrays(GL_TRIANGLES, 0, 3);

        window.checkPoolEvents();
        window.checkSwapBuffer();
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);

    window.destroyWindow();

    return 0;
}
