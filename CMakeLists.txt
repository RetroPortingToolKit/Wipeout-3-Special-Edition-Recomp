cmake_minimum_required(VERSION 3.20)
project(Wipeout_3_Special_Edition_Recomp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to psxrecomp")

# --- Standalone product surfaces -------------------------------------------
# Scaffold prompts / flags fill these. Without recomp-ui, PSX_RECOMP_UI is OFF.
# Setup-host CI requires WIZARD_CMAKE_BLOCK / ENABLE_SETUP_WIZARD (FORCE_SETUP_HOST
# alone does not open first-run / Generate & rebuild).
# recomp-ui submodule present (PSX_RECOMP_UI defaults ON).
# Netplay and rewind add per-tick synchronization/snapshot work that this
# standalone single-player product does not use. Keep both opt-in so the
# generated guest can sustain the PAL x2 100 Hz contract.
set(PSX_NETPLAY OFF CACHE BOOL
    "Link recomp-net delay-sync (opt-in; needs recomp-net)" FORCE)
set(PSX_REWIND OFF CACHE BOOL
    "Build and expose local rewind support (opt-in)" FORCE)
set(PSX_SETUP_WIZARD OFF CACHE BOOL
    "Advertise first-run setup wizard + Generate & rebuild in recomp-ui" FORCE)
# WipEout 3 is a retail-BIOS product: never link or stage OpenBIOS.
set(PSXRECOMP_BIOS_STEMS "SCPH1001" CACHE STRING
    "Retail BIOS backend required for WipEout 3" FORCE)
set(PSXRECOMP_BIOS_STEM "SCPH1001" CACHE STRING
    "Retail BIOS backend used by WipEout 3" FORCE)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

file(GLOB WIPEOUT3_MOD_PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/*.c")

psxrecomp_add_game_runtime(psx-runtime
    # PGXP is not optional for this title. WipEout 3 renders at up to 16x
    # internal resolution, where the PS1's affine texture mapping and integer
    # vertex snapping are the dominant visual defect. Without PSX_PGXP=1 the
    # PGXP_LOAD/STORE/ALU/MULDIV/COP2 hooks the emitter writes into every
    # generated translation unit preprocess to ((void)0), and the "pgxp" mod
    # rides on the legacy SWC2-only recorder: 18.7% of drawn vertices got
    # precise positions, against 99.4% with the hooks compiled in.
    PGXP
    ENABLE_NETPLAY_IF_PRESENT
    NETPLAY_LOBBY_URL "ws://netplay.retcomm.net:8765"
    WINDOW_TITLE "Wipeout 3 Special Edition  Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAX_PLAYERS 4
    LAUNCHER_BOXART "${CMAKE_CURRENT_SOURCE_DIR}/launcher_assets/img/boxart.tga"
    APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/psxrecomp.ico"
    GEN_MARKER "generated/SCES_028.45_dispatch.c"
    GEN_FULL_GLOB "generated/SCES_028.45_full_*.c"
    CODEGEN_SETUP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/codegen_setup.c"
        ${WIPEOUT3_MOD_PLUGIN_SOURCES}
)

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
