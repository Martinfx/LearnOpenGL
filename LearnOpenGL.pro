TEMPLATE = app
CONFIG -= console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.cpp
LIBS += -lglfw3 -lGL -lm -lXrandr -lXi -lX11 -lXxf86vm -lpthread
