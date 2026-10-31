add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(engine STATIC
    src/Camera.cpp
    src/FixedStepClock.cpp
    src/Geometry.cpp
    src/GltfLoader.cpp
    src/Physics.cpp
    src/PlayerController.cpp
    src/Renderer.cpp
    src/Scene.cpp
)
target_include_directories(engine PUBLIC include)
target_link_libraries(engine PUBLIC glm::glm PRIVATE glad Jolt::Jolt)
target_include_directories(engine PRIVATE third_party/cgltf)
target_compile_definitions(engine PUBLIC GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
set_target_properties(engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
