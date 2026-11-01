# Third-party licence texts shipped with the game: licenses/<library>.txt next to
# the executable (build: bin/licenses, install: licenses/) plus THIRD_PARTY.md,
# the table from 3rdparty/README.md that the credits screen shows.
# Every folder in 3rdparty/ must have a row in the table and an entry here
# (engine_tests checks both).
#
# engine_license(<name> <file> [FROM <text>] [TO <text>]) copies <file>, or only the
# part from FROM up to and including TO when the licence is a comment in a header.

set(ENGINE_LICENSE_NAMES "")

function(engine_license name file)
    cmake_parse_arguments(L "" "FROM;TO" "" ${ARGN})
    file(READ "${file}" text)
    if(L_FROM)
        string(FIND "${text}" "${L_FROM}" at)
        if(at LESS 0)
            message(FATAL_ERROR "engine_license(${name}): '${L_FROM}' not found in ${file}")
        endif()
        string(SUBSTRING "${text}" ${at} -1 text)
    endif()
    if(L_TO)
        string(FIND "${text}" "${L_TO}" at)
        if(at LESS 0)
            message(FATAL_ERROR "engine_license(${name}): '${L_TO}' not found in ${file}")
        endif()
        string(LENGTH "${L_TO}" toLength)
        math(EXPR at "${at} + ${toLength}")
        string(SUBSTRING "${text}" 0 ${at} text)
    endif()
    file(RELATIVE_PATH source "${PROJECT_SOURCE_DIR}" "${file}")
    file(WRITE "${CMAKE_BINARY_DIR}/bin/licenses/${name}.txt" "${name}: from ${source}\n\n${text}\n")
    set(ENGINE_LICENSE_NAMES ${ENGINE_LICENSE_NAMES} ${name} PARENT_SCOPE)
endfunction()

set(T ${PROJECT_SOURCE_DIR}/3rdparty)
engine_license(cgltf ${T}/cgltf/LICENSE)
engine_license(glad ${T}/glad/include/KHR/khrplatform.h FROM "Copyright (c) 2008-2018" TO "*/")
engine_license(glfw ${T}/glfw/LICENSE.md)
engine_license(glm ${T}/glm/copying.txt)
engine_license(jolt ${T}/jolt/LICENSE)
engine_license(lua ${T}/lua/lua.h FROM "* Copyright (C) 1994-2024 Lua.org, PUC-Rio." TO "DEALINGS IN THE SOFTWARE.")
engine_license(miniaudio ${T}/miniaudio/LICENSE)
engine_license(openal-soft ${T}/openal-soft/COPYING)
engine_license(openal-soft-bsd ${T}/openal-soft/BSD-3Clause)
engine_license(openal-soft-pffft ${T}/openal-soft/LICENSE-pffft)
engine_license(recastnavigation ${T}/recastnavigation/License.txt)
engine_license(sol2 ${T}/sol2/LICENSE.txt)
engine_license(stb ${T}/stb/stb_image.h FROM "ALTERNATIVE A - MIT License")
engine_license(tracy ${T}/tracy/LICENSE)
unset(T)

configure_file(${PROJECT_SOURCE_DIR}/3rdparty/README.md ${CMAKE_BINARY_DIR}/bin/licenses/THIRD_PARTY.md COPYONLY)
set(ENGINE_LICENSE_FILES ${CMAKE_BINARY_DIR}/bin/licenses/THIRD_PARTY.md)
foreach(name IN LISTS ENGINE_LICENSE_NAMES)
    list(APPEND ENGINE_LICENSE_FILES ${CMAKE_BINARY_DIR}/bin/licenses/${name}.txt)
endforeach()
