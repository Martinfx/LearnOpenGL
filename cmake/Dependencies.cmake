# Each dependency is taken from the system first (FreeBSD pkg, Linux distro,
# vcpkg on Windows) and otherwise built from the copy in 3rdparty/.
# Files in 3rdparty/ are kept exactly as released upstream (see 3rdparty/README.md):
# everything project-specific is done here, through options and wrapper targets.
set(ENGINE_3RDPARTY ${CMAKE_CURRENT_LIST_DIR}/../3rdparty)

# --- GLM (header-only math) -------------------------------------------------
if(ENGINE_USE_SYSTEM_DEPS)
    find_package(glm CONFIG QUIET)
endif()
if(NOT TARGET glm::glm)
    message(STATUS "GLM: 3rdparty/glm")
    add_library(glm_headers INTERFACE)
    target_include_directories(glm_headers SYSTEM INTERFACE ${ENGINE_3RDPARTY}/glm)
    add_library(glm::glm ALIAS glm_headers)
else()
    message(STATUS "GLM: system package")
endif()

# --- GLFW (window + input for the game) -------------------------------------
if(ENGINE_BUILD_GAME)
    if(ENGINE_USE_SYSTEM_DEPS)
        find_package(glfw3 3.3 CONFIG QUIET)
    endif()
    if(NOT TARGET glfw)
        message(STATUS "GLFW: 3rdparty/glfw")
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        # Wayland backend needs wayland-scanner + dev headers; use it only when present.
        if(UNIX AND NOT APPLE)
            find_program(WAYLAND_SCANNER wayland-scanner)
            if(NOT WAYLAND_SCANNER)
                set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
            endif()
        endif()
        add_subdirectory(${ENGINE_3RDPARTY}/glfw ${CMAKE_BINARY_DIR}/3rdparty/glfw EXCLUDE_FROM_ALL)
    else()
        message(STATUS "GLFW: system package")
    endif()
endif()

# --- Jolt Physics (collision + character controller) -------------------------
if(ENGINE_USE_SYSTEM_DEPS AND ENGINE_USE_SYSTEM_JOLT)
    find_package(Jolt CONFIG QUIET)
endif()
if(TARGET Jolt::Jolt)
    message(STATUS "Jolt: system package")
else()
    message(STATUS "Jolt: 3rdparty/jolt")
    # Portable build: SSE4.2 baseline (no AVX), dynamic MSVC runtime to match Qt,
    # RTTI/exceptions on because engine classes derive from Jolt interfaces.
    set(USE_AVX OFF CACHE BOOL "" FORCE)
    set(USE_AVX2 OFF CACHE BOOL "" FORCE)
    set(USE_LZCNT OFF CACHE BOOL "" FORCE)
    set(USE_TZCNT OFF CACHE BOOL "" FORCE)
    set(USE_F16C OFF CACHE BOOL "" FORCE)
    set(USE_FMADD OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
    set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
    set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
    set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
    # No GPU compute backends (they need shader compilers at build time).
    set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
    set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
    set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${ENGINE_3RDPARTY}/jolt/Build ${CMAKE_BINARY_DIR}/3rdparty/jolt EXCLUDE_FROM_ALL)
    if(NOT TARGET Jolt::Jolt)
        add_library(Jolt::Jolt ALIAS Jolt)
    endif()
endif()

# --- Lua 5.4 + sol2 (gameplay scripting) -------------------------------------
# Lua is built from source so every platform runs the exact same version.
set(lua_SOURCE_DIR ${ENGINE_3RDPARTY}/lua)
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac|onelua|ltests)\\.c$")
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua_static PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE ON)
if(UNIX)
    target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
    target_link_libraries(lua_static PRIVATE m)
endif()

# sol2 is header-only; its own CMake project is not needed.
add_library(sol2_headers INTERFACE)
target_include_directories(sol2_headers SYSTEM INTERFACE ${ENGINE_3RDPARTY}/sol2/include)
target_link_libraries(sol2_headers INTERFACE lua_static)
add_library(sol2::sol2 ALIAS sol2_headers)

# --- Vendored single-file libraries ------------------------------------------
add_library(glad STATIC ${ENGINE_3RDPARTY}/glad/src/gl.c)
target_include_directories(glad PUBLIC ${ENGINE_3RDPARTY}/glad/include)
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(cgltf_headers INTERFACE)
target_include_directories(cgltf_headers SYSTEM INTERFACE ${ENGINE_3RDPARTY}/cgltf)
add_library(stb_headers INTERFACE)
target_include_directories(stb_headers SYSTEM INTERFACE ${ENGINE_3RDPARTY}/stb)

# --- OpenAL Soft (audio output, 3D, HRTF, EFX reverb) -------------------------
# LGPL: always a shared library (the system package or 3rdparty/openal-soft).
if(ENGINE_USE_SYSTEM_DEPS)
    find_package(OpenAL CONFIG QUIET)
    if(NOT TARGET OpenAL::OpenAL)
        find_package(OpenAL MODULE QUIET)
        if(OPENAL_FOUND)
            add_library(openal_system INTERFACE)
            get_filename_component(OPENAL_PARENT_INCLUDE "${OPENAL_INCLUDE_DIR}" DIRECTORY)
            target_include_directories(openal_system INTERFACE "${OPENAL_INCLUDE_DIR}" "${OPENAL_PARENT_INCLUDE}")
            target_link_libraries(openal_system INTERFACE ${OPENAL_LIBRARY})
            add_library(OpenAL::OpenAL ALIAS openal_system)
        endif()
    endif()
endif()
if(TARGET OpenAL::OpenAL)
    message(STATUS "OpenAL: system package")
else()
    message(STATUS "OpenAL: 3rdparty/openal-soft (shared)")
    set(LIBTYPE SHARED CACHE STRING "" FORCE)
    foreach(opt ALSOFT_UTILS ALSOFT_EXAMPLES ALSOFT_TESTS ALSOFT_INSTALL ALSOFT_INSTALL_CONFIG ALSOFT_INSTALL_HRTF_DATA
                ALSOFT_INSTALL_AMBDEC_PRESETS ALSOFT_INSTALL_EXAMPLES ALSOFT_INSTALL_UTILS ALSOFT_UPDATE_BUILD_VERSION)
        set(${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory(${ENGINE_3RDPARTY}/openal-soft ${CMAKE_BINARY_DIR}/3rdparty/openal-soft EXCLUDE_FROM_ALL)
    # Next to the executables, which find it through their $ORIGIN rpath.
    set_target_properties(OpenAL PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
                                            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# Recast/Detour: navmesh building and path queries (static library, no warnings).
file(GLOB RECAST_SOURCES ${ENGINE_3RDPARTY}/recastnavigation/Recast/Source/*.cpp
                         ${ENGINE_3RDPARTY}/recastnavigation/Detour/Source/*.cpp)
add_library(recastnav STATIC ${RECAST_SOURCES})
target_include_directories(recastnav SYSTEM PUBLIC ${ENGINE_3RDPARTY}/recastnavigation/Recast/Include
                                                  ${ENGINE_3RDPARTY}/recastnavigation/Detour/Include)
set_target_properties(recastnav PROPERTIES POSITION_INDEPENDENT_CODE ON)

# miniaudio (+ stb_vorbis for .ogg): one implementation unit, built without the
# project's warning flags. Backends: WASAPI (Windows), PulseAudio/ALSA/JACK
# (Linux, loaded at runtime), OSS/sndio (FreeBSD).
add_library(miniaudio STATIC ${CMAKE_CURRENT_LIST_DIR}/miniaudio_impl.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${ENGINE_3RDPARTY}/miniaudio ${ENGINE_3RDPARTY}/stb)
set_target_properties(miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
    target_link_libraries(miniaudio PUBLIC m)
endif()

# --- Tracy profiler (client only; the Tracy UI is a separate download) ---------
# Off: only the headers, every zone macro compiles to nothing. On: the client
# listens for the Tracy UI (ENGINE_TRACY=ON, then run the game or editor).
if(ENGINE_TRACY)
    add_library(tracy_client STATIC ${ENGINE_3RDPARTY}/tracy/TracyClient.cpp)
    target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE TRACY_ON_DEMAND)
    target_link_libraries(tracy_client PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    if(WIN32)
        target_link_libraries(tracy_client PUBLIC ws2_32 dbghelp)
    elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
        target_link_libraries(tracy_client PUBLIC execinfo)
    endif()
    set_target_properties(tracy_client PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(tracy_client SYSTEM PUBLIC ${ENGINE_3RDPARTY}/tracy)
else()
    add_library(tracy_client INTERFACE)
    target_include_directories(tracy_client SYSTEM INTERFACE ${ENGINE_3RDPARTY}/tracy)
endif()

# --- Qt 6 (editor) -- always a system/installer package ----------------------
if(ENGINE_BUILD_EDITOR)
    find_package(Qt6 6.2 COMPONENTS Widgets OpenGLWidgets)
    if(NOT Qt6_FOUND)
        message(WARNING "Qt 6 not found: the editor is skipped. "
                        "Install it (pkg install qt6-base / apt install qt6-base-dev / Qt online installer) "
                        "or set CMAKE_PREFIX_PATH.")
        set(ENGINE_BUILD_EDITOR OFF CACHE BOOL "" FORCE)
    endif()
endif()
