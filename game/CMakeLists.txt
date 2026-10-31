add_executable(fps_game main.cpp MapHud.cpp MapHud.h GameAudio.cpp GameAudio.h)
target_link_libraries(fps_game PRIVATE engine glfw)
set_target_properties(fps_game PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Ship the assets next to the executable. They are large (PBR textures, props),
# so newer CMake copies only files that changed.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
    set(ENGINE_COPY_DIR copy_directory_if_different)
else()
    set(ENGINE_COPY_DIR copy_directory)
endif()
add_custom_command(TARGET fps_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E ${ENGINE_COPY_DIR} ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:fps_game>/assets)
