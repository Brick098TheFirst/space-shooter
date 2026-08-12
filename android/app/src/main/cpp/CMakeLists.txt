cmake_minimum_required(VERSION 3.22.1)
project(spacegame)

set(GBA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../gba)

add_library(spacegame SHARED
    native-lib.c
    platform_host.c
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
)

target_compile_definitions(spacegame PRIVATE PLATFORM_HOST=1)
target_compile_options(spacegame PRIVATE -O2 -ffast-math)

find_library(log-lib log)
target_link_libraries(spacegame ${log-lib} m)
