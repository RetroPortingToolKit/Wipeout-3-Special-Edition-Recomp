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
option(PSX_ENABLE_LTO
    "Enable LTO for the full-detail recompiled runtime" ON)
set(PSX_SETUP_WIZARD OFF CACHE BOOL
    "Advertise first-run setup wizard + Generate & rebuild in recomp-ui")
# WipEout 3 is a retail-BIOS product: never link or stage OpenBIOS.
set(PSXRECOMP_BIOS_STEMS "SCPH1001" CACHE STRING
    "Retail BIOS backend required for WipEout 3" FORCE)
set(PSXRECOMP_BIOS_STEM "SCPH1001" CACHE STRING
    "Retail BIOS backend used by WipEout 3" FORCE)
# This title ships a deliberately closed remaster catalog. Keep framework
# packages that are not part of that catalog (for example CD-speed, NTSC, or
# duplicate experimental variants) out of the one-folder release entirely.
set(PSX_BUILTIN_MOD_ALLOWLIST
    "psx.enhancement.fast-loading;psx.enhancement.pgxp")
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# Compile only plug-ins referenced by the shipped title manifests. Research
# generators may leave retired variants under src/mods; a glob would silently
# turn those files into product behavior.
set(WIPEOUT3_MOD_PLUGIN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/wipeout3_framerate_100_plugin.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/wipeout3_skip_fmv_plugin.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/wipeout3_widescreen_plugin.c")

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
    # The authenticated PAL x2/high-detail title relocates executable blocks
    # into live RAM after the boot EXE is loaded.  This corpus is generated
    # from those exact captured byte variants and supplies native dispatch for
    # them; without it the runtime correctly rejects the original AOT image
    # but falls through to the interpreter for every relocated call.
    GAME_OVERLAY_STATIC_C "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c"
    CODEGEN_SETUP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/codegen_setup.c"
        ${WIPEOUT3_MOD_PLUGIN_SOURCES}
)

# WipEout 3 is an authenticated expanded-RAM title.  The generic upstream
# runtime now exposes the retail/expanded geometry contract; selecting 8 MiB
# here is title policy and keeps the framework default faithful at 2 MiB.
foreach(_WIPEOUT3_RUNTIME_TARGET IN ITEMS psx-runtime psx-runtime-pgxp)
    if(TARGET ${_WIPEOUT3_RUNTIME_TARGET})
        target_compile_definitions(${_WIPEOUT3_RUNTIME_TARGET} PRIVATE
            PSX_MAIN_RAM_BYTES=0x00800000u)
    endif()
endforeach()

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

# POST_BUILD staging only runs when the executable links. Make profile edits a
# link dependency so changing a runtime default or the seeded mod selection
# cannot leave an apparently fresh executable beside stale configuration.
set_property(TARGET psx-runtime APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/game.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/game_options.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/state.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp/mods/builtin/packages/psx.enhancement.pgxp/1.0.0/manifest.toml")

# Keep the presentation asset in the one-folder release. The renderer treats
# the directory as user-replaceable artwork, while `bezel = "random"` selects
# one of the shipped PNGs automatically.
set(WIPEOUT3_BEZELS "${CMAKE_CURRENT_SOURCE_DIR}/bezels")
if(EXISTS "${WIPEOUT3_BEZELS}")
    add_custom_command(TARGET psx-runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WIPEOUT3_BEZELS}"
            "$<TARGET_FILE_DIR:psx-runtime>/bezels"
        COMMENT "Staging WipEout bezel artwork"
        VERBATIM)
endif()

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

# Ship the product's known-good enhancement selection with the executable.
# The launcher may still edit this file, but a fresh copy starts with the
# WipEout remaster profile instead of framework-faithful defaults that leave
# 8 MB RAM, PGXP propagation, Fast Loading, or FMV skipping disabled.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mods/state.toml")
    add_custom_command(TARGET psx-runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:psx-runtime>/mods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/mods/state.toml"
            "$<TARGET_FILE_DIR:psx-runtime>/mods/state.toml"
        COMMENT "Staging the WipEout enhancement profile"
        VERBATIM)
endif()
