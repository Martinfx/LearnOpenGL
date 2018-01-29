TEMPLATE = app
CONFIG -= console c++11

SOURCES += main.cpp

#linux
#{
#    LIBS += -lglfw3 -lGL -lGLEW -lm -lXrandr -lXi -lX11 -#lXxf86vm -lpthread
#}

LIBS += -lglfw -lGL -lGLEW -lm -lXrandr -lXi -lX11 -lXxf86vm -lpthread

DISTFILES += \
    shader.vert \
    shader.frag \
    light.frag \
    light.vert \
    skybox.frag \
    skybox.vert
