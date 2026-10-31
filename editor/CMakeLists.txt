set(CMAKE_AUTOMOC ON)

add_executable(fps_editor
    main.cpp
    AssetPaths.h
    MainWindow.cpp
    HierarchyPanel.cpp
    AssetBrowser.cpp
    EditorHistory.cpp
    MainWindow.h
    MaterialEditor.cpp
    MaterialEditor.h
    ScriptEditor.cpp
    TextureEditor.cpp
    TextureEditor.h
    ScriptEditor.h
    Vec3Editor.cpp
    Vec3Editor.h
    Viewport.cpp
    WeaponPanel.cpp
    WeaponPanel.h
    Viewport.h
)
target_link_libraries(fps_editor PRIVATE engine Qt6::Widgets Qt6::OpenGLWidgets)
# Development builds edit the repository's assets directly (the build's copy is overwritten on every build).
option(ENGINE_EDITOR_SOURCE_ASSETS "Editor reads and writes assets in the source tree" ON)
if(ENGINE_EDITOR_SOURCE_ASSETS)
    target_compile_definitions(fps_editor PRIVATE FPS_SOURCE_ASSETS="${PROJECT_SOURCE_DIR}/assets")
endif()
set_target_properties(fps_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    WIN32_EXECUTABLE ON)
if(TARGET fps_game)
    add_dependencies(fps_editor fps_game) # the editor's Play button launches it
endif()
