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
    ${GBA_DIR}/src/game.c
    ${GBA_DIR}/src/menu.c
    ${GBA_DIR}/src/renderer.c
    ${GBA_DIR}/src/audio.c
    ${GBA_DIR}/src/audio_data.c
    ${GBA_DIR}/src/save.c
    ${GBA_DIR}/src/starfield.c
    ${GBA_DIR}/src/gfx_data.c
)

target_include_directories(spacegame PRIVATE
    ${GBA_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${EOS_INCLUDE_DIR}
)

target_compile_definitions(spacegame PRIVATE PLATFORM_HOST=1 EOS_ENABLED=1)
target_compile_options(spacegame PRIVATE -O2 -ffast-math)

find_library(log-lib log)
target_link_libraries(spacegame EOSSDK ${log-lib} m)
