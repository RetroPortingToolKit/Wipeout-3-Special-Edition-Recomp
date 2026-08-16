#include "mod_plugins.h"

/*
 * WipEout 3's projection, culling, backdrop, and HUD hooks live in game.toml.
 * These activation callbacks let the game-owned mod package choose the
 * presentation aspect before renderer startup.
 */
static void wipeout3_widescreen_16_9_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
}

static void wipeout3_widescreen_21_9_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(21u, 9u);
}

static void wipeout3_widescreen_adaptive_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
    (void)psx_mod_set_adaptive_display_aspect(21u, 9u);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_widescreen_plugins) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.widescreen.16-9", wipeout3_widescreen_16_9_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.widescreen.21-9", wipeout3_widescreen_21_9_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.widescreen.adaptive", wipeout3_widescreen_adaptive_activate);
}
