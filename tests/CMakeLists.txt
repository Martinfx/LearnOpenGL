add_executable(engine_tests engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE engine)
add_test(NAME engine_tests COMMAND engine_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(engine_tests PRIVATE ENGINE_TEST_ASSETS="${PROJECT_SOURCE_DIR}/assets")
