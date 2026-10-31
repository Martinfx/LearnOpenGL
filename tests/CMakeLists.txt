add_executable(engine_tests engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE engine)
add_test(NAME engine_tests COMMAND engine_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(engine_tests PRIVATE ENGINE_TEST_ASSETS="${PROJECT_SOURCE_DIR}/assets")

# Smoke test of every level (ctest -L smoke; skip with ctest -LE smoke). Needs a
# display or xvfb-run, so it is only added where one of them exists.
find_program(XVFB_RUN xvfb-run)
if(UNIX AND TARGET fps_game AND (XVFB_RUN OR DEFINED ENV{DISPLAY}))
    file(GLOB SMOKE_LEVELS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/levels/*.level)
    foreach(level ${SMOKE_LEVELS})
        get_filename_component(name ${level} NAME_WE)
        add_test(NAME level_smoke_${name}
                 COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/smoke_level.sh $<TARGET_FILE:fps_game> assets/levels/${name}.level 120)
        set_tests_properties(level_smoke_${name} PROPERTIES LABELS smoke TIMEOUT 900)
    endforeach()
endif()
