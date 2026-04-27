cmake_minimum_required(VERSION 3.16)
project(AlienBreed C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find SDL2
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer QUIET)

if(NOT SDL2_mixer_FOUND)
    # Try pkg-config fallback
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2_mixer REQUIRED SDL2_mixer)
endif()

# Asset directories (read-only at runtime)
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
set(GAME_DIR  "${CMAKE_SOURCE_DIR}/game")

# -------------------------------------------------------
# Tools (asset converters - run once at build prep time)
# -------------------------------------------------------

add_executable(convert_bitplanes tools/convert_bitplanes.c)

add_executable(convert_tileset tools/convert_tileset.c)

add_executable(convert_sprites tools/convert_sprites.c)

add_executable(convert_audio tools/convert_audio.c)

add_executable(parse_levels tools/parse_levels.c)

# -------------------------------------------------------
# Main game
# -------------------------------------------------------

set(GAME_SOURCES
    src/c/game/main.c
    src/c/game/player.c
    src/c/game/alien.c
    src/c/game/level.c
    src/c/game/hud.c
    src/c/game/debug.c
    src/c/game/story.c
    src/c/game/menu.c
    src/c/game/briefing.c
    src/c/game/intex.c
    src/c/game/gameover.c
    src/c/game/end.c
    src/c/engine/palette.c
    src/c/engine/sprite.c
    src/c/engine/alien_gfx.c
    src/c/engine/anim_gfx.c
    src/c/engine/typewriter.c
    src/c/engine/tilemap.c
    src/c/hal/video.c
    src/c/hal/input.c
    src/c/hal/audio.c
    src/c/hal/soundmon.c
    src/c/hal/timer.c
    src/c/hal/blitter.c
    src/c/hal/hal.c
)

add_executable(alienbreed ${GAME_SOURCES})

target_include_directories(alienbreed PRIVATE
    src/c
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_mixer_INCLUDE_DIRS}
    /opt/homebrew/include
    /usr/local/include
)

target_link_libraries(alienbreed PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_mixer_LIBRARIES}
    SDL2_mixer::SDL2_mixer
)

# Copy assets directory next to the binary (only if assets/ exists)
if(EXISTS "${ASSETS_DIR}")
    add_custom_command(TARGET alienbreed POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ASSETS_DIR}"
            "$<TARGET_FILE_DIR:alienbreed>/assets"
        COMMENT "Copying converted assets..."
    )
else()
    message(STATUS "Note: No 'assets/' directory found. Run the conversion tools first.")
endif()

# Copy Soundmon music files from game/ to <binary>/assets/music/
# The files are used directly (no conversion needed).
add_custom_command(TARGET alienbreed POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:alienbreed>/assets/music"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GAME_DIR}/title.soundmon"
        "$<TARGET_FILE_DIR:alienbreed>/assets/music/title.soundmon"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GAME_DIR}/level.soundmon"
        "$<TARGET_FILE_DIR:alienbreed>/assets/music/level.soundmon"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GAME_DIR}/boss.soundmon"
        "$<TARGET_FILE_DIR:alienbreed>/assets/music/boss.soundmon"
    COMMENT "Copying Soundmon music files..."
)

# Compiler warnings
if(MSVC)
    target_compile_options(alienbreed PRIVATE /W3)
else()
    target_compile_options(alienbreed PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
