#include "mod_plugins.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Margin artwork, moved out of game.toml (the framework no longer has a
 * [video] bezel key at all) and into the mod catalog, joining widescreen and
 * the frame-rate package WipEout 3 already owns as mods. Display enhancements
 * are trusted-mod-only on PSX -- see
 * runtime/tests/test_mod_owned_display_controls.py -- and this is the same
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

/*
 * The team marks this title ships. Keep in sync with BOTH the non-"random"
 * [[option.choice]] values in
 * mods/preloaded/packages/wipeout3.enhancement.bezel/1.0.0/manifest.toml and
 * the PNGs in assets/bezels (staged beside the exe as bezels/). The list, not
 * the option string, decides which file is opened, so a stale or hand-edited
 * value can never name a path outside bezels/.
 */
static const char *const k_teams[] = {
    "ag-systems", "assegai", "auricom",  "feisar",
    "goteki-45",  "icaras",  "piranha",  "qirex",
};
#define TEAM_COUNT (sizeof k_teams / sizeof k_teams[0])

/* "Random" is per launch: activation runs once, so the mark it lands on stays
 * for the whole session. The wall clock is mixed rather than fed to srand(),
 * which would reseed a PRNG the rest of the process may share. */
static const char *wipeout3_bezel_random_team(void) {
    unsigned int seed = (unsigned int)time(NULL);
    seed ^= seed >> 16;
    seed *= 0x45D9F3Bu;
    seed ^= seed >> 16;
    return k_teams[seed % TEAM_COUNT];
}

static void wipeout3_bezel_activate(void) {
    char team[32];
    const char *pick = NULL;
    char path[64];
    size_t i;

    /* An unreadable, empty or unrecognised value falls back to the manifest
     * default, "random". Choosing here rather than returning early keeps the
     * decision in one place: doing nothing would leave the margins black, which
     * is not what the player who just switched the artwork on asked for. */
    if (psx_mod_option_value(PKG, FEATURE, "team", team, sizeof team) &&
        team[0] != '\0') {
        for (i = 0; i < TEAM_COUNT; ++i) {
            if (strcmp(team, k_teams[i]) == 0) {
                pick = k_teams[i];
                break;
            }
        }
    }
    if (!pick) pick = wipeout3_bezel_random_team();

    /* Relative on purpose: psx_mod_set_bezel_artwork resolves it against the
     * executable's directory, where CMakeLists stages assets/bezels. */
    (void)snprintf(path, sizeof path, "bezels/%s.png", pick);
    (void)psx_mod_set_bezel_artwork(path);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_bezel_plugin) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.bezel", wipeout3_bezel_activate);
}
