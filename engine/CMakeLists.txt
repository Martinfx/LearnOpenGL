include(cmake_embed_shaders.cmake)
add_library(engine STATIC
    src/Animation.cpp
    src/Anomaly.cpp
    src/Audio.cpp
    src/Camera.cpp
    src/CpuFeatures.cpp
    src/FixedStepClock.cpp
    src/Geometry.cpp
    src/Gizmo.cpp
    src/GltfLoader.cpp
    src/Material.cpp
    src/TextureCook.cpp
    src/Memory.cpp
    src/NavMesh.cpp
    src/Terrain.cpp
    src/CharacterBody.cpp
    src/Physics.cpp
    src/Scripting.cpp
    src/Weapon.cpp
    src/Decals.cpp
    src/Particles.cpp
    src/PlayerController.cpp
    src/Renderer.cpp
    src/Lights.cpp
    src/EntityIO.cpp
    src/Ambience.cpp
    src/TimeOfDay.cpp
    src/Water.cpp
    src/WorldStreaming.cpp
    src/FrameArena.cpp
    src/AssetPalette.cpp
    src/Scene.cpp
    src/JobSystem.cpp
    src/SceneGraph.cpp
    src/Surfaces.cpp
    src/Log.cpp
    src/Paths.cpp
    src/Crash.cpp
    src/CVars.cpp
    src/DebugDraw.cpp
    src/Ui2D.cpp
    ${ENGINE_EMBEDDED_SHADERS}
)
target_include_directories(engine PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(engine PUBLIC glm::glm Threads::Threads PRIVATE glad Jolt::Jolt lua_static sol2::sol2 cgltf_headers stb_headers miniaudio recastnav OpenAL::OpenAL)
target_compile_definitions(engine PRIVATE SOL_ALL_SAFETIES_ON=1)
# Build version for logs and crash reports.
find_package(Git QUIET)
set(ENGINE_VERSION "dev")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE ENGINE_GIT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(ENGINE_GIT_VERSION)
        set(ENGINE_VERSION "${ENGINE_GIT_VERSION}")
    endif()
endif()
set_source_files_properties(src/Crash.cpp PROPERTIES COMPILE_DEFINITIONS "ENGINE_VERSION=\"${ENGINE_VERSION}\"")
if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|NetBSD")
    target_link_libraries(engine PRIVATE execinfo) # backtrace() for crash reports
endif()
if(WIN32)
    target_link_libraries(engine PRIVATE dbghelp) # stack symbols and minidumps in crash reports
    target_link_libraries(engine PRIVATE psapi) # process memory for the editor memory panel
endif()
target_compile_definitions(engine PUBLIC GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
set_target_properties(engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
