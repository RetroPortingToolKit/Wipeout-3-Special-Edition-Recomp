#include "mod_plugins.h"

/*
 * NTSC x2 (120 Hz) activation: half the guest CRTC VBlank period and pace the
 * host at 120 Hz so wall-clock matches guest time (audio stays in pitch).
 * Game patches live in wipeout3.enhancement.framerate-120.
 */
static void wipeout3_framerate_ntsc_x2_activate(void) {
    (void)psx_mod_set_crtc_refresh_multiplier(2u);
    (void)psx_mod_set_native_vblank_rate(120u);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_framerate_120_plugins) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.framerate.ntsc-x2", wipeout3_framerate_ntsc_x2_activate);
}
