set(CMAKE_AUTOMOC ON)

add_executable(fps_editor
    main.cpp
    AssetPaths.h
    MainWindow.cpp
    TerrainTools.cpp
    MeshPaintTools.cpp
    HierarchyPanel.cpp
    AssetBrowser.cpp
    EditorHistory.cpp
    EnvironmentPanel.cpp
    PaletteDialog.cpp
    AssetValidationDialog.cpp
    EnvironmentPanel.h
    MainWindow.h
    MaterialEditor.cpp
    MaterialEditor.h
    ScriptEditor.cpp
    ShaderEditor.cpp
    ShaderEditor.h
    TextureEditor.cpp
    TextureEditor.h
    ScriptEditor.h
    Vec3Editor.cpp
    Vec3Editor.h
    Viewport.cpp
    WeaponPanel.cpp
    AnomalyPanel.cpp
    LightPanel.h
    LightPanel.cpp
    AmbienceEditor.h
    AmbienceEditor.cpp
    OutputsPanel.h
    OutputsPanel.cpp
    SoundEditor.cpp
    SurfacesEditor.cpp
    ParticleEditor.cpp
    AnimGraphEditor.cpp
    SoundEditor.h
    WeaponPanel.h
    Viewport.h
    ViewportPanel.cpp
    ViewportPanel.h
    EditorUi.cpp
    EditorUi.h
)
target_link_libraries(fps_editor PRIVATE engine Qt6::Widgets Qt6::OpenGLWidgets)
# Development builds edit the repository's assets directly (the build's copy is overwritten on every build).
option(ENGINE_EDITOR_SOURCE_ASSETS "Editor reads and writes assets in the source tree" ON)
if(ENGINE_EDITOR_SOURCE_ASSETS)
    target_compile_definitions(fps_editor PRIVATE FPS_SOURCE_ASSETS="${PROJECT_SOURCE_DIR}/assets"
                                                  FPS_SOURCE_SHADERS="${PROJECT_SOURCE_DIR}/engine/shaders")
endif()
set_target_properties(fps_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    WIN32_EXECUTABLE ON)
if(TARGET fps_game)
    add_dependencies(fps_editor fps_game) # the editor's Play button launches it
endif()

# Function names in crash report stacks (backtrace_symbols needs exported symbols).
if(NOT WIN32)
    set_target_properties(fps_editor PROPERTIES ENABLE_EXPORTS ON)
endif()
