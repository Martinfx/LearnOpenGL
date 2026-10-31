cmake_minimum_required(VERSION 3.21)
project(FpsEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENGINE_BUILD_GAME   "Build the game runtime (GLFW)" ON)
option(ENGINE_BUILD_EDITOR "Build the level editor (Qt 6)" ON)
option(ENGINE_USE_SYSTEM_DEPS "Prefer system packages (pkg/apt/vcpkg) over downloading" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(engine)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
if(ENGINE_BUILD_GAME)
    add_subdirectory(game)
endif()
if(ENGINE_BUILD_EDITOR)
    add_subdirectory(editor)
endif()
