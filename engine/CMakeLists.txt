add_library(engine STATIC
    src/Animation.cpp
    src/Camera.cpp
    src/FixedStepClock.cpp
    src/Geometry.cpp
    src/GltfLoader.cpp
    src/Material.cpp
    src/CharacterBody.cpp
    src/Physics.cpp
    src/Scripting.cpp
    src/Weapon.cpp
    src/PlayerController.cpp
    src/Renderer.cpp
    src/Scene.cpp
)
target_include_directories(engine PUBLIC include)
target_link_libraries(engine PUBLIC glm::glm PRIVATE glad Jolt::Jolt lua_static sol2::sol2 cgltf_headers stb_headers)
target_compile_definitions(engine PRIVATE SOL_ALL_SAFETIES_ON=1)
target_compile_definitions(engine PUBLIC GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
set_target_properties(engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
