/* WipEout-owned activation for the upstream generic FMV detector. */
#include "mod_plugins.h"

extern int psx_mod_set_auto_skip_fmv(int enabled);

static void wipeout3_skip_fmv_activate(void) {
    (void)psx_mod_set_auto_skip_fmv(1);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_skip_fmv_plugin) {
    (void)psx_mod_register_activation_plugin("wipeout3.skip_fmv",
                                             wipeout3_skip_fmv_activate);
}
