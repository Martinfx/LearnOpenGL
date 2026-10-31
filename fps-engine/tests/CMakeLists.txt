add_executable(engine_tests engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE engine)
add_test(NAME engine_tests COMMAND engine_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
