#include "mod_plugins.h"

#include <string.h>

/*
 * Margin artwork, moved out of game.toml ([video] bezel is now "off") and into
 * the mod catalog, joining widescreen and the frame-rate packages WipEout 3
 * already owns as mods. Display enhancements are trusted-mod-only on PSX --
 * see runtime/tests/test_mod_owned_display_controls.py -- and this is the same
 * move applied to the one presentation setting that was still config-only.
 *
 * Gating it also puts the credit where players can see it: the art is
 * toolboxio's, released publicly with no formal licence, so shipping it
 * opt-in with an author link beats shipping it on by default with the
 * attribution buried in a text file.
 *
 * One plugin reading one option, rather than one plugin per value gated by
 * `when` -- the older idiom the widescreen package still uses. Nine marks
 * would mean nine registrations, and psx_mod_option_value exists precisely to
 * collapse that (see the rationale at mod_plugins.h).
 */
#define PKG "wipeout3.enhancement.bezel"
#define FEATURE "bezel"

static void wipeout3_bezel_activate(void) {
    char team[32];

    /* An unreadable or empty value falls back to the manifest default. Passing
     * "random" explicitly rather than returning early keeps the decision here:
     * game.toml pins [video] bezel to "off", so doing nothing would disable
     * the artwork the player just switched on. */
    if (!psx_mod_option_value(PKG, FEATURE, "team", team, sizeof team) ||
        team[0] == '\0') {
        (void)strcpy(team, "random");
    }

    (void)psx_mod_set_bezel(team);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_bezel_plugin) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.bezel", wipeout3_bezel_activate);
}
