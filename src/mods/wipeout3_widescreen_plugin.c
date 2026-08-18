#include "mod_plugins.h"

/*
 * WipEout 3's projection, culling, backdrop, and HUD hooks live in game.toml.
 * These activation callbacks let the game-owned mod package choose the
 * presentation aspect before renderer startup.
 */
/* 4:3 with the feature ON. The squash reduces to identity (xnum == xden), so
 * psx_ws_x_margin() returns just the guard band and every widened cull bound
 * collapses back to the stock verdict. Offered as an explicit choice rather
 * than only as "turn the mod off", so the aspect list reads as a complete set
 * and switching back to original framing does not mean hunting for a toggle. */
static void wipeout3_widescreen_4_3_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(4u, 3u);
}

static void wipeout3_widescreen_16_9_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
}

static void wipeout3_widescreen_21_9_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(21u, 9u);
}

/* 32:9 (super-ultrawide). The squash reduces to 3/8, so psx_ws_x_margin()
 * returns 283 -- the widened cull bounds scale with it automatically, since
 * [[widescreen.cull.widen]] moves the bound by the live margin rather than
 * pinning a verdict. */
static void wipeout3_widescreen_32_9_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(32u, 9u);
}

/* Adaptive follows the live window, bounded so a freak window shape cannot
 * drive the squash somewhere untested. The bound is 32:9 now that 32:9 is a
 * validated fixed choice -- capping adaptive at 21:9 while offering 32:9
 * explicitly meant a 32:9 monitor got pillarboxed unless the player knew to
 * leave Adaptive. */
static void wipeout3_widescreen_adaptive_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
    (void)psx_mod_set_adaptive_display_aspect(32u, 9u);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_widescreen_plugins) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.widescreen.4-3", wipeout3_widescreen_4_3_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.widescreen.16-9", wipeout3_widescreen_16_9_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.widescreen.21-9", wipeout3_widescreen_21_9_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.widescreen.32-9", wipeout3_widescreen_32_9_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.widescreen.adaptive", wipeout3_widescreen_adaptive_activate);
}
