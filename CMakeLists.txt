cmake_minimum_required(VERSION 3.21)
project(FpsEngine VERSION 0.1.0 LANGUAGES C CXX)

# Without a build type CMake compiles with no optimisation at all: glm, Jolt and the
# renderer's per-object loops then run 10-50x slower (a big level drops to ~1 FPS).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No CMAKE_BUILD_TYPE given: using Release (pass -DCMAKE_BUILD_TYPE=Debug to debug)")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENGINE_BUILD_GAME   "Build the game runtime (GLFW)" ON)
option(ENGINE_BUILD_EDITOR "Build the level editor (Qt 6)" ON)
option(ENGINE_USE_SYSTEM_DEPS "Prefer system packages (pkg/apt/vcpkg) over downloading" ON)
# A system Jolt package exports its CPU flags (often AVX2/FMA, sometimes AVX-512) to every
# target that links it, so the whole engine would need that CPU. The bundled copy is
# built for the portable SSE4.2 baseline.
option(ENGINE_USE_SYSTEM_JOLT "Use a system Jolt package instead of 3rdparty/jolt" OFF)

include(cmake/Dependencies.cmake)

add_subdirectory(engine)

include(CTest)
if(ENGINE_BUILD_GAME)
    add_subdirectory(game)
endif()
if(ENGINE_BUILD_EDITOR)
    add_subdirectory(editor)
endif()
if(BUILD_TESTING)
    add_subdirectory(tests) # after game: the level smoke tests run fps_game
endif()
