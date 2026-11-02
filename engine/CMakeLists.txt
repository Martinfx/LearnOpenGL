include(cmake_embed_shaders.cmake)
add_library(engine STATIC
    src/Animation.cpp
    src/Atmosphere.cpp
    src/Anomaly.cpp
    src/Audio.cpp
    src/Camera.cpp
    src/CpuFeatures.cpp
    src/FixedStepClock.cpp
    src/Geometry.cpp
    src/Gizmo.cpp
    src/GltfLoader.cpp
    src/Material.cpp
    src/AnimGraph.cpp
    src/Ragdoll.cpp
    src/VertexPaint.cpp
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
    src/Probes.cpp
    src/ProbeBaker.cpp
    src/Ambience.cpp
    src/TimeOfDay.cpp
    src/Water.cpp
    src/WorldStreaming.cpp
    src/FrameArena.cpp
    src/AssetCredits.cpp
    src/AssetRegistry.cpp
    src/FileWatcher.cpp
    src/HotReload.cpp
    src/AssetPalette.cpp
    src/Prefab.cpp
    src/CityGen.cpp
    src/CityRoads.cpp
    src/CityGrammar.cpp
    src/Scene.cpp
    src/JobSystem.cpp
    src/SceneGraph.cpp
    src/Surfaces.cpp
    src/Factions.cpp
    src/ALife.cpp
    src/Fauna.cpp
    src/AnomalyStorm.cpp
    src/Quests.cpp
    src/Dialog.cpp
    src/ScriptDebug.cpp
    src/Demo.cpp
    src/MeshCache.cpp
    src/Vfs.cpp
    src/Localization.cpp
    src/Items.cpp
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
target_link_libraries(engine PUBLIC glm::glm Threads::Threads tracy_client PRIVATE glad Jolt::Jolt lua_static sol2::sol2 cgltf_headers stb_headers miniaudio recastnav OpenAL::OpenAL)
target_compile_definitions(engine PRIVATE SOL_ALL_SAFETIES_ON=1)
# Build version for logs, the menu, crash reports and packages: `git describe` at every build
# (cmake/Version.cmake rewrites the header only when the commit or the dirty flag changed).
add_custom_target(engine_version
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/generated/EngineVersion.h
            -P ${PROJECT_SOURCE_DIR}/cmake/Version.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/generated/EngineVersion.h
    COMMENT "Build version from git")
add_dependencies(engine engine_version)
target_include_directories(engine PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
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
