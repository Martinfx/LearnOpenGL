#include <iostream>
#include <fstream>
#include <string>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "../3rdparty/stb_image.h"

float vertices[] = {
    // positions          // colors           // texture coords
       0.5f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f, // top right
       0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, // bottom right
      -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f, // bottom left
      -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f  // top left
};
unsigned int indices[] = {
        0, 1, 3, // first triangle
        1, 2, 3  // second triangle
    };

const char* shaderVertex =
"#version 330 core\n"
"uniform mat4 MVP;\n"
"attribute vec3 vCol;\n"
"attribute vec3 vPos;\n"
"attribute vec2 aTexCoord;\n"
"out vec3 color;\n"
"out vec2 TexCoord;"
"void main()\n"
"{\n"
"    gl_Position = MVP * vec4(vPos, 1.0);\n"
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
        // update other events like input handling
        glfwPollEvents();
    }

    void checkSwapBuffer()
    {
        // put the stuff we've been drawing onto the display
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

    static void mouseCallback(GLFWwindow* window, int button, int action, int mods)
    {

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

       glfwSetMouseButtonCallback(m_window, mouseCallback);
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

    Shader() : /*m_vertexShader(0), m_fragmentShader(0),
        m_geometryShader(0),*/ m_isVertexShader(false),
        m_isFragmentShader(false), m_isGeometryShader(false)
    {

    }

    ~Shader() {}

    void loadShader(const std::string &shader, TypeShader type)
    {
        if(type == TypeShader::VERTEX_SHADER)
        {
            //const char * vertex_shader = getShaderReader(shader);
            m_vertexShader = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(m_vertexShader, 1, &shaderVertex, NULL);
            glCompileShader(m_vertexShader);
            m_isVertexShader = true;
        }

        if(type == TypeShader::FRAGMENT_SHADER)
        {
            //const char *fragment_shader = getShaderReader(shader);
            m_fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(m_fragmentShader, 1, &shaderFragment, NULL);
            glCompileShader(m_fragmentShader);
            m_isFragmentShader = true;
        }

        /*if(type == TypeShader::GEOMETRY_SHADER)
        {
            const char *geometry_shader = getShaderReader(shader);
            m_geometryShader = glCreateShader(GL_GEOMETRY_SHADER);
            glShaderSource(m_geometryShader, 1, &geometry_shader, NULL);
            glCompileShader(m_geometryShader);
            m_isGeometryShader = true;
        }*/
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
        /*if(m_isGeometryShader)
        {
            glAttachShader(m_id, m_geometryShader);
        }*/

        glLinkProgram(m_id);
    }

    const char * getShaderReader(const std::string &shader)
    {
        std::string line;
        std::string source;
        std::ifstream file(shader);
        if(file.is_open())
        {
            while(std::getline(file, line))
            {
                std::cerr << line << std::endl;
                source = source + line; //+ "\n";
            }

            file.close();
        }
        else
        {
            std::cerr << "Cannot open file! " << shader << std::endl;
        }

        std::cerr << source << std::endl;

        return source.c_str();
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

int main(void)
{
    GLSettings settings;
    settings.windowName = "Hello OpenGL";
    //settings.windowHeight = 800;
    //settings.windowWidth = 600;

    GLWindow window(settings);

    // start GLEW extension handler
    glewExperimental = GL_TRUE;
    glewInit();

    Shader shader;
    shader.loadShader("shader.vert", TypeShader::VERTEX_SHADER);
    shader.loadShader("shader.frag", TypeShader::FRAGMENT_SHADER);
    shader.createShaderProgram();

    /*GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &shaderVertex, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &shaderFragment, NULL);
    glCompileShader(fs);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, fs);
    glAttachShader(shaderProgram, vs);
    glLinkProgram(shaderProgram);*/

    GLint mvp_location, vpos_location, vcol_location, tex_location;
    mvp_location  = glGetUniformLocation(shader.getShaderProgram(), "MVP");
    vpos_location = glGetAttribLocation(shader.getShaderProgram(), "vPos");
    vcol_location = glGetAttribLocation(shader.getShaderProgram(), "vCol");
    tex_location  = glGetAttribLocation(shader.getShaderProgram(), "aTexCoord");

    unsigned int vbo, vao, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(vpos_location, 3, GL_FLOAT, GL_FALSE,  8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(vpos_location);

    glVertexAttribPointer(vcol_location, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(vcol_location);

    // texture coord attribute
    glVertexAttribPointer(tex_location, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(tex_location);

    unsigned int texture;
    glGenTextures(1, &texture);

    // all upcoming GL_TEXTURE_2D operations now have effect on this texture object
    glBindTexture(GL_TEXTURE_2D, texture);

    // set the texture wrapping parameters
    // set texture wrapping to GL_REPEAT (default wrapping method)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // set texture filtering parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // load image, create texture and generate mipmaps
    int width, height, nrChannels;
    unsigned char *data = stbi_load("box.png", &width, &height, &nrChannels, 0);
    if (data)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        std::cout << "Failed to load texture" << std::endl;
    }

    stbi_image_free(data);

    //glUseProgram(shader.getShaderProgram());
    shader.useShaderProgram();
    glUniform1i(glGetUniformLocation(shader.getShaderProgram(), "texture1"), 0);

    // Loop until the user closes the window
    while (!window.checkCloseWindow())
    {
        window.updateFpsCounter();

        float ratio;
        int width, height;
        //glm::mat4x4 m, p, mvp;

        window.getFramebufferSize(&width, &height);
        ratio = static_cast<float>(width) / static_cast<float>(height);

        glViewport(0, 0, width, height);

        // wipe the drawing surface clear
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.2f, 0.5f, 0.7f, 0.6f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glm::mat4 transform;
        transform = glm::translate(transform, glm::vec3(0.0f, 0.0f, 0.0f));
        transform = glm::rotate(transform, (float)glfwGetTime(), glm::vec3(0.0f, 0.0f, 1.0f));

        //glUseProgram(shaderProgram);
        shader.useShaderProgram();
        glUniformMatrix4fv(mvp_location, 1, GL_FALSE, glm::value_ptr(transform));

        // draw points 0-3 from the currently bound vao with current in-use shader
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        window.checkPoolEvents();
        window.checkSwapBuffer();
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);

    return 0;
}
