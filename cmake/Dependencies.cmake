# Each dependency is taken from the system first (FreeBSD pkg, Linux distro,
# vcpkg on Windows) and downloaded with FetchContent only when it is missing.
include(FetchContent)

# --- GLM (header-only math) -------------------------------------------------
if(ENGINE_USE_SYSTEM_DEPS)
    find_package(glm CONFIG QUIET)
endif()
if(NOT TARGET glm::glm)
    message(STATUS "GLM: downloading")
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glm)
else()
    message(STATUS "GLM: system package")
endif()

# --- GLFW (window + input for the game) -------------------------------------
if(ENGINE_BUILD_GAME)
    if(ENGINE_USE_SYSTEM_DEPS)
        find_package(glfw3 3.3 CONFIG QUIET)
    endif()
    if(NOT TARGET glfw)
        message(STATUS "GLFW: downloading")
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
        FetchContent_Declare(glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(glfw)
    else()
        message(STATUS "GLFW: system package")
    endif()
endif()

# --- Jolt Physics (collision + character controller) -------------------------
if(ENGINE_USE_SYSTEM_DEPS)
    find_package(Jolt CONFIG QUIET)
endif()
if(TARGET Jolt::Jolt)
    message(STATUS "Jolt: system package")
else()
    message(STATUS "Jolt: downloading")
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
    FetchContent_Declare(JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG v5.6.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR Build)
    FetchContent_MakeAvailable(JoltPhysics)
    if(NOT TARGET Jolt::Jolt)
        add_library(Jolt::Jolt ALIAS Jolt)
    endif()
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
