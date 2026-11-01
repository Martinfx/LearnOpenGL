# Writes ${OUT} (a header with ENGINE_VERSION) from `git describe`, only when it changed, so
# every build carries the current commit without recompiling when nothing moved.
# Run as: cmake -DSOURCE_DIR=... -DOUT=... -P Version.cmake
set(version "dev")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags WORKING_DIRECTORY ${SOURCE_DIR}
                    OUTPUT_VARIABLE described OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(described)
        set(version "${described}")
    endif()
endif()
set(text "#pragma once\n#define ENGINE_VERSION \"${version}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL text)
    file(WRITE "${OUT}" "${text}")
endif()
