cmake_minimum_required(VERSION 3.20)
project(Wipeout_3_Special_Edition_Recomp LANGUAGES C CXX)

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

psxrecomp_add_game_runtime(psx-runtime
    ENABLE_NETPLAY_IF_PRESENT
    NETPLAY_LOBBY_URL "ws://netplay.retcomm.net:8765"
    ENABLE_SETUP_WIZARD
    WINDOW_TITLE "Wipeout 3 Special Edition  Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAX_PLAYERS 2
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
endif()
