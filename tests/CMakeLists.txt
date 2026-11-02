add_executable(engine_tests engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE engine)
add_test(NAME engine_tests COMMAND engine_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
# Windows has no rpath: the DLLs it links (OpenAL) go next to the test executable.
if(WIN32)
    add_custom_command(TARGET engine_tests POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:engine_tests> $<TARGET_FILE_DIR:engine_tests>
                       COMMAND_EXPAND_LISTS)
endif()
target_compile_definitions(engine_tests PRIVATE ENGINE_TEST_ASSETS="${PROJECT_SOURCE_DIR}/assets"
                                               ENGINE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

# Asset validation (missing references; `asset_check --models` also checks the scale).
add_executable(asset_check asset_check.cpp)
target_link_libraries(asset_check PRIVATE engine)
add_test(NAME asset_check COMMAND asset_check ${PROJECT_SOURCE_DIR}/assets --quiet)
set_tests_properties(asset_check PROPERTIES LABELS assets)

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

# Golden images of the renderer (ctest -L golden): a few fixed views compared with
# tests/golden/*.ppm within a tolerance. GOLDEN_UPDATE=1 ctest -L golden renews them.
add_executable(image_compare image_compare.cpp)
if(UNIX AND TARGET fps_game AND (XVFB_RUN OR DEFINED ENV{DISPLAY}))
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/golden/shots.txt GOLDEN_SHOTS REGEX "^[a-z0-9_]+\\|")
    foreach(shot ${GOLDEN_SHOTS})
        string(REGEX MATCH "^[a-z0-9_]+" name "${shot}")
        add_test(NAME golden_${name}
                 COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/golden_image.sh $<TARGET_FILE:fps_game> $<TARGET_FILE:image_compare> ${name}
                         ${CMAKE_CURRENT_BINARY_DIR}/golden)
        set_tests_properties(golden_${name} PROPERTIES LABELS golden TIMEOUT 600)
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/golden/shots.txt)
endif()
