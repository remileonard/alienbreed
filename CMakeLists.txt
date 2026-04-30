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
# Option: embed all assets directly into the binary
# -------------------------------------------------------
#
# Usage (after running the conversion tools to produce assets/):
#   cmake -DEMBED_ASSETS=ON ..
#   cmake --build .
#
# When ON, every file from assets/ and game/ is converted to a C byte array
# by the bin2c tool and linked into the executable.  The resulting binary
# has no external file dependencies and can be distributed as a single file.
# When OFF (default), assets are copied next to the binary as usual.
# -------------------------------------------------------
option(EMBED_ASSETS "Embed all game assets into the binary at link time" OFF)

# -------------------------------------------------------
# Tools (asset converters - run once at build prep time)
# -------------------------------------------------------

add_executable(convert_bitplanes tools/convert_bitplanes.c)

add_executable(convert_tileset tools/convert_tileset.c)

add_executable(convert_sprites tools/convert_sprites.c)

add_executable(convert_audio tools/convert_audio.c)

add_executable(parse_levels tools/parse_levels.c)

# bin2c — used at build time to convert binary assets to C source arrays.
# Built for the host so it can always be executed during the build step.
add_executable(bin2c tools/bin2c.c)

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
    src/c/hal/vfs.c
)

# -------------------------------------------------------
# Asset embedding (EMBED_ASSETS=ON)
# -------------------------------------------------------
if(EMBED_ASSETS)
    # Require assets/ to exist — they are produced by the conversion tools.
    if(NOT EXISTS "${ASSETS_DIR}")
        message(FATAL_ERROR
            "EMBED_ASSETS=ON but the assets/ directory does not exist.\n"
            "Run the asset conversion tools first:\n"
            "  cmake --build . --target convert_bitplanes convert_tileset "
            "convert_sprites convert_audio parse_levels\n"
            "  ./convert_assets.sh\n"
            "Then re-run cmake with -DEMBED_ASSETS=ON.")
    endif()

    # Output directory for generated C sources.
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/asset_bundle")
    file(MAKE_DIRECTORY "${BUNDLE_DIR}")

    # Collect sources: everything under assets/ and game/ (level data + music).
    file(GLOB_RECURSE _raw_assets RELATIVE "${CMAKE_SOURCE_DIR}" "${ASSETS_DIR}/*")
    file(GLOB         _game_files RELATIVE "${CMAKE_SOURCE_DIR}" "${GAME_DIR}/*")

    # Filter directories out of the GLOB results (GLOB_RECURSE already skips them,
    # but the plain GLOB may include them on some platforms).
    set(_all_assets "")
    foreach(_f ${_raw_assets})
        if(NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/${_f}")
            list(APPEND _all_assets "${_f}")
        endif()
    endforeach()
    foreach(_f ${_game_files})
        if(NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/${_f}")
            list(APPEND _all_assets "${_f}")
        endif()
    endforeach()

    # For each file, schedule a build-time bin2c invocation and collect the
    # generated .c paths.  Also accumulate the information needed for the
    # bundle index (generated at configure time, sizes via file(SIZE ...)).
    set(_bundle_sources "")      # list of generated .c paths
    set(_index_externs "")       # extern declarations for the index
    set(_index_entries "")       # AssetEntry initializers for the index
    set(_index_count 0)

    foreach(_rel_path ${_all_assets})
        # Derive a C-safe symbol name from the relative path.
        string(REGEX REPLACE "[^a-zA-Z0-9]" "_" _symbol "${_rel_path}")
        set(_symbol "asset_${_symbol}")

        set(_full_path "${CMAKE_SOURCE_DIR}/${_rel_path}")
        set(_out_c    "${BUNDLE_DIR}/${_symbol}.c")

        # Map game/*.soundmon → "assets/music/<name>" (the path the engine uses).
        string(REGEX MATCH "^game/[^/]+\\.soundmon$" _is_soundmon "${_rel_path}")
        if(_is_soundmon)
            get_filename_component(_fname "${_rel_path}" NAME)
            set(_bundle_key "assets/music/${_fname}")
        else()
            set(_bundle_key "${_rel_path}")
        endif()

        # Build-time command: bin2c converts the binary file to a C array.
        add_custom_command(
            OUTPUT  "${_out_c}"
            COMMAND $<TARGET_FILE:bin2c>
                    "${_full_path}" "${_out_c}" "${_symbol}"
            DEPENDS $<TARGET_FILE:bin2c> "${_full_path}"
            COMMENT "Embedding ${_rel_path}"
            VERBATIM
        )
        list(APPEND _bundle_sources "${_out_c}")

        # Get the file size at configure time for the index.
        file(SIZE "${_full_path}" _fsize)

        # Accumulate index data.
        string(APPEND _index_externs
            "extern const unsigned char ${_symbol}[];\n"
            "extern const size_t        ${_symbol}_size;\n")
        string(APPEND _index_entries
            "    { \"${_bundle_key}\", ${_symbol}, ${_fsize} },\n")
        math(EXPR _index_count "${_index_count} + 1")
    endforeach()

    # Generate the bundle index at configure time.
    # (Symbol names and sizes are known; actual data is generated at build time.)
    set(_index_file "${BUNDLE_DIR}/asset_bundle_index.c")
    file(WRITE "${_index_file}"
        "/* Auto-generated by CMake — do not edit */\n"
        "#include <stddef.h>\n"
        "#include \"hal/asset_bundle.h\"\n\n"
        "${_index_externs}\n"
        "const AssetEntry g_asset_bundle[] = {\n"
        "${_index_entries}"
        "};\n"
        "const int g_asset_bundle_count = ${_index_count};\n"
    )

    # Add all generated sources to the game target.
    list(APPEND GAME_SOURCES ${_bundle_sources} "${_index_file}")

    message(STATUS "EMBED_ASSETS: will embed ${_index_count} asset files into the binary.")

else()
    # EMBED_ASSETS=OFF — use the empty stub so the VFS always falls back to disk.
    list(APPEND GAME_SOURCES src/c/hal/asset_bundle_empty.c)
endif()

add_executable(alienbreed ${GAME_SOURCES})

target_include_directories(alienbreed PRIVATE
    src/c
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_mixer_INCLUDE_DIRS}
    /opt/homebrew/include
    /usr/local/include
)

if(EMBED_ASSETS)
    target_include_directories(alienbreed PRIVATE "${BUNDLE_DIR}")
    target_compile_definitions(alienbreed PRIVATE EMBED_ASSETS=1)
endif()

target_link_libraries(alienbreed PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_mixer_LIBRARIES}
    SDL2_mixer::SDL2_mixer
)

# -------------------------------------------------------
# Post-build: copy assets next to the binary (EMBED_ASSETS=OFF only)
# -------------------------------------------------------
if(NOT EMBED_ASSETS)
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
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(alienbreed PRIVATE /W3)
else()
    target_compile_options(alienbreed PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
