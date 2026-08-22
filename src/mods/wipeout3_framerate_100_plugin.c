#include "mod_plugins.h"

/*
 * PAL x2 activation for the user-derived PAL 100-FPS title patch.
 *
 * The title patch is intentionally separate from this runtime hook: the
 * plugin cannot make an unmodified PAL executable simulate twice per PAL
 * VBlank.  A package must provide the authenticated title transformation and
 * select this hook explicitly.  Keeping the hook dormant by default prevents
 * a display-only multiplier from being mistaken for a 100 Hz simulation.
 */
static void wipeout3_framerate_pal_x2_activate(void) {
    (void)psx_mod_set_native_vblank_rate(100u);
    (void)psx_mod_set_crtc_refresh_multiplier(2u);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_framerate_100_plugins) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.framerate.pal-x2", wipeout3_framerate_pal_x2_activate);
}
