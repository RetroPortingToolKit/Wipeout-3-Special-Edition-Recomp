cmake_minimum_required(VERSION 3.20)
project(Wipeout_3_Special_Edition_Recomp LANGUAGES C CXX)

include(CTest)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to psxrecomp")

# --- Opt-in product surfaces (OFF by default in the framework) -------------
# Scaffold prompts / flags fill these. Without recomp-ui, PSX_RECOMP_UI is OFF.
# Setup-host CI requires WIZARD_CMAKE_BLOCK / ENABLE_SETUP_WIZARD (FORCE_SETUP_HOST
# alone does not open first-run / Generate & rebuild).
# recomp-ui submodule present (PSX_RECOMP_UI defaults ON).
if(EXISTS "${PSXRECOMP_ROOT}/lib/recomp-net/CMakeLists.txt")
    set(PSX_NETPLAY ON CACHE BOOL
        "Link recomp-net delay-sync (opt-in; needs recomp-net)" FORCE)
endif()
set(PSX_SETUP_WIZARD ON CACHE BOOL
    "Advertise first-run setup wizard + Generate & rebuild in recomp-ui" FORCE)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

file(GLOB WIPEOUT3_MOD_PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/*.c")
# Diagnostic speed probes are retained as development tools, but release
# binaries must not ship their callbacks or activation surface. Final speed
# acceptance is the hash-bound visible HUD timer matrix under artifacts/.
list(FILTER WIPEOUT3_MOD_PLUGIN_SOURCES EXCLUDE REGEX
    "/wipeout3_(progress|race_clock)_probe\\.c$")

psxrecomp_add_game_runtime(psx-runtime
    # Shipping baseline is deliberately PGXP-free. Exact-NCLIP widescreen is
    # title-scoped and does not require the per-instruction PGXP dataflow
    # engine; leaving PGXP compiled into an apparently-disabled build adds the
    # very hot-path work this release is intended to avoid.
    ENABLE_NETPLAY_IF_PRESENT
    NETPLAY_LOBBY_URL "ws://netplay.retcomm.net:8765"
    ENABLE_SETUP_WIZARD
    WINDOW_TITLE "Wipeout 3 Special Edition  Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAIN_RAM_MIB 8
    MAX_PLAYERS 4
    LAUNCHER_BOXART "${CMAKE_CURRENT_SOURCE_DIR}/launcher_assets/img/boxart.tga"
    APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/psxrecomp.ico"
    GEN_MARKER "generated/SCES_028.45_dispatch.c"
    GEN_FULL_GLOB "generated/SCES_028.45_full_*.c"
    CODEGEN_SETUP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/codegen_setup.c"
        ${WIPEOUT3_MOD_PLUGIN_SOURCES}
)

if(BUILD_TESTING)
    add_executable(wipeout3-pal100-semantic-test
        tests/wipeout3_pal100_semantic_test.c)
    target_include_directories(wipeout3-pal100-semantic-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    add_test(NAME wipeout3-pal100-semantic
        COMMAND wipeout3-pal100-semantic-test)

    add_executable(wipeout3-hue-visual-presets-test
        tests/test_wipeout3_hue_visual_presets.c)
    target_include_directories(wipeout3-hue-visual-presets-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    add_test(NAME wipeout3-hue-visual-presets
        COMMAND wipeout3-hue-visual-presets-test)

    add_executable(wipeout3-billboard-type4-cull-test
        tests/test_wipeout3_billboard_type4_cull.c)
    target_include_directories(wipeout3-billboard-type4-cull-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    add_test(NAME wipeout3-billboard-type4-cull
        COMMAND wipeout3-billboard-type4-cull-test)

    add_executable(wipeout3-hue-8mb-title-test
        tests/test_wipeout3_hue_8mb_title.c)
    target_include_directories(wipeout3-hue-8mb-title-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    add_test(NAME wipeout3-hue-8mb-title
        COMMAND wipeout3-hue-8mb-title-test)

    add_executable(wipeout3-draw-distance-test
        tests/test_wipeout3_draw_distance_far.c)
    target_include_directories(wipeout3-draw-distance-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    add_test(NAME wipeout3-draw-distance
        COMMAND wipeout3-draw-distance-test)

    add_executable(wipeout3-intro-skip-test
        tests/test_wipeout3_intro_skip.c)
    target_include_directories(wipeout3-intro-skip-test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include")
    add_test(NAME wipeout3-intro-skip
        COMMAND wipeout3-intro-skip-test)
endif()

# Stage runtime configs next to the exe for direct build/ launches. Only copy
# files that exist — never fail POST_BUILD when game_options.toml is absent.
foreach(_PSX_RUNTIME_CONFIG game.toml game_options.toml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}")
        add_custom_command(TARGET psx-runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}"
                "$<TARGET_FILE_DIR:psx-runtime>/${_PSX_RUNTIME_CONFIG}"
            COMMENT "Staging ${_PSX_RUNTIME_CONFIG}"
            VERBATIM)
    endif()
endforeach()

# Ship WipEout 3's trusted, game-owned mod catalog beside the runtime. Every
# feature remains disabled until selected on the launcher's Mods page.
set(WIPEOUT3_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(EXISTS "${WIPEOUT3_PRELOADED_MODS}/packages")
    add_custom_command(TARGET psx-runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WIPEOUT3_PRELOADED_MODS}"
            "$<TARGET_FILE_DIR:psx-runtime>/mods"
        COMMENT "Preloading the WipEout 3 mod catalog"
        VERBATIM)

# Bezel artwork (margin wallpapers; toolboxio's team marks). The runtime loads
# from <exe dir>/bezels, so it must be staged like the mod catalog -- it was
# hand-copied after every build before this, and forgotten more than once.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/bezels")
    add_custom_command(TARGET psx-runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/bezels"
            "$<TARGET_FILE_DIR:psx-runtime>/bezels"
        COMMENT "Staging bezel artwork"
        VERBATIM)
endif()
endif()
