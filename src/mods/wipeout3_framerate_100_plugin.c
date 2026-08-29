#include "mod_plugins.h"

/*
 * PAL x2 (100 Hz) activation. The title-side PAL transform is supplied by
 * wipeout3.enhancement.framerate with build="pal"; this hook only owns the
 * host CRTC multiplier and native pacing rate.
 */
static void wipeout3_framerate_pal_x2_activate(void) {
    (void)psx_mod_set_crtc_refresh_multiplier(2u);
    (void)psx_mod_set_native_vblank_rate(100u);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_framerate_100_plugins) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.framerate.pal-x2", wipeout3_framerate_pal_x2_activate);
}
