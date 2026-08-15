cmake_minimum_required(VERSION 3.22.1)
project(spacegame)

set(GBA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../gba)

if(NOT DEFINED EOS_INCLUDE_DIR OR NOT DEFINED EOS_LIBRARY_DIR)
    message(FATAL_ERROR "EOS_INCLUDE_DIR and EOS_LIBRARY_DIR must be supplied by Gradle")
endif()

# Epic Online Services Android SDK. The static-stdc++ AAR carries one shared
# EOS binary per supported ABI; Gradle expands it before CMake is configured.
add_library(EOSSDK SHARED IMPORTED)
set_target_properties(EOSSDK PROPERTIES
    IMPORTED_LOCATION "${EOS_LIBRARY_DIR}/${ANDROID_ABI}/libEOSSDK.so"
    INTERFACE_INCLUDE_DIRECTORIES "${EOS_INCLUDE_DIR}"
)

add_library(spacegame SHARED
    native-lib.c
    platform_host.c
    eos_online.c
    coop.c
    game.c
    story.c
    story_data.c
    ${GBA_DIR}/src/menu.c
    ${GBA_DIR}/src/renderer.c
    ${GBA_DIR}/src/audio.c
    ${GBA_DIR}/src/audio_data.c
    ${GBA_DIR}/src/save.c
    ${GBA_DIR}/src/starfield.c
    ${GBA_DIR}/src/gfx_data.c
)

# The Android multiplayer edition compiles its own FORKED copy of game.c/game.h
# (the one in this folder), which adds the host-authoritative co-op player.
# Keep this source dir FIRST so every translation unit picks up the forked
# game.h instead of the unmodified gba/include/game.h.
target_include_directories(spacegame PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GBA_DIR}/include
    ${EOS_INCLUDE_DIR}
)

target_compile_definitions(spacegame PRIVATE PLATFORM_HOST=1 EOS_ENABLED=1)
target_compile_options(spacegame PRIVATE -O2 -ffast-math)

find_library(log-lib log)
target_link_libraries(spacegame EOSSDK ${log-lib} m)
