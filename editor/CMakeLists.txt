set(CMAKE_AUTOMOC ON)

add_executable(fps_editor
    main.cpp
    MainWindow.cpp
    MainWindow.h
    Vec3Editor.cpp
    Vec3Editor.h
    Viewport.cpp
    Viewport.h
)
target_link_libraries(fps_editor PRIVATE engine Qt6::Widgets Qt6::OpenGLWidgets)
set_target_properties(fps_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    WIN32_EXECUTABLE ON)
if(TARGET fps_game)
    add_dependencies(fps_editor fps_game) # the editor's Play button launches it
endif()
