add_executable(fps_game main.cpp)
target_link_libraries(fps_game PRIVATE engine glfw)
set_target_properties(fps_game PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Ship the sample level next to the executable.
add_custom_command(TARGET fps_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:fps_game>/assets)
