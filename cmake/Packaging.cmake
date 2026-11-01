# Release packages: `cmake --install` lays out a game folder, CPack zips it, tools/package.sh
# (Linux, FreeBSD) and tools/package.ps1 (Windows) do the whole release build with symbols.
#
# Package layout (flat, the executables find OpenAL and the assets next to themselves):
#   fps_game, fps_editor, fps_pack, libopenal / OpenAL32.dll
#   game.pak (ENGINE_PACKAGE_PAK) or assets/ (loose, for the editor and modding)
#   LICENSE, licenses/ (third-party), README.md, mods/ (empty, for mods)

option(ENGINE_LTO "Link-time optimization for the engine, game and editor (release packages)" OFF)
option(ENGINE_PACKAGE_PAK "Package the assets as game.pak instead of a loose assets folder" OFF)

if(ENGINE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_ok OUTPUT lto_error LANGUAGES CXX)
    if(lto_ok)
        foreach(t engine fps_game fps_editor fps_pack)
            if(TARGET ${t})
                set_property(TARGET ${t} PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
            endif()
        endforeach()
        message(STATUS "LTO: on")
    else()
        message(WARNING "LTO not supported here: ${lto_error}")
    endif()
endif()

include(GNUInstallDirs)
set(packaged fps_game fps_pack)
if(NOT ENGINE_PACKAGE_PAK)
    list(APPEND packaged fps_editor) # the editor works on loose assets only
endif()
foreach(t ${packaged})
    if(TARGET ${t})
        # Installed executables find their libraries next to themselves.
        if(UNIX AND NOT APPLE)
            set_property(TARGET ${t} PROPERTY INSTALL_RPATH "$ORIGIN")
        endif()
        install(TARGETS ${t} RUNTIME DESTINATION . COMPONENT game)
        if(MSVC)
            install(FILES $<TARGET_PDB_FILE:${t}> DESTINATION . COMPONENT symbols OPTIONAL)
        endif()
    endif()
endforeach()
if(TARGET OpenAL AND NOT TARGET openal_system)
    install(TARGETS OpenAL RUNTIME DESTINATION . LIBRARY DESTINATION . NAMELINK_SKIP COMPONENT game)
endif()

if(ENGINE_PACKAGE_PAK AND TARGET fps_pack)
    # The assets as one archive, made by the freshly built fps_pack at install time.
    install(CODE "
        set(pak \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/game.pak\")
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
        execute_process(COMMAND \"$<TARGET_FILE:fps_pack>\" \"${PROJECT_SOURCE_DIR}/assets\" \"\${pak}\" RESULT_VARIABLE r)
        if(NOT r EQUAL 0)
            message(FATAL_ERROR \"fps_pack failed\")
        endif()" COMPONENT game)
    # The game needs the folder to exist as the root of the virtual paths; the editor needs loose files.
    install(DIRECTORY DESTINATION assets COMPONENT game)
else()
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/assets/ DESTINATION assets COMPONENT game
            PATTERN "*.blend" EXCLUDE PATTERN "*.blend1" EXCLUDE PATTERN "source" EXCLUDE)
endif()
install(DIRECTORY DESTINATION mods COMPONENT game)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/README.md DESTINATION . COMPONENT game)
install(FILES ${ENGINE_LICENSE_FILES} DESTINATION licenses COMPONENT game) # cmake/Licenses.cmake

set(CPACK_PACKAGE_NAME "FpsEngine")
set(CPACK_PACKAGE_VENDOR "Martinfx")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "FpsEngine-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON) # game and symbols as separate archives
if(MSVC)
    set(CPACK_COMPONENTS_ALL game symbols) # .pdb files; elsewhere tools/package.sh splits the debug info
else()
    set(CPACK_COMPONENTS_ALL game)
endif()
include(CPack)
