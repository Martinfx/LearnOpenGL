TEMPLATE = app
CONFIG -= console c++11
#CONFIG -= app_bundle
#CONFIG -= qt

SOURCES += main.cpp

#linux
#{
#    LIBS += -lglfw3 -lGL -lGLEW -lm -lXrandr -lXi -lX11 -lXxf86vm -lpthread
#}

freebsd
{
    LIBS += -lglfw -lGL -lGLEW -lm -lXrandr -lXi -lX11 -lXxf86vm -lpthread
}

DISTFILES += \
    shader.frag \
    shader.vert
