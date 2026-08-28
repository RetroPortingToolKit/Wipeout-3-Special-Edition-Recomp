#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * Stock SCES-02845 first scans PSYGLEED.STR, WO3.STR, PSYG.STR,
 * CURLY44.STR, and CURLY44B.STR at 0x801162CC..0x8011631C. Those calls are
 * resource initialization: they populate the start/end-sector tables at
 * 0x801D1B94/0x801D1BA8. They must run unchanged.
 *
 * The five sites below are instead the complete direct-call set for the
 * presentation routine at 0x80115F34. The three calls in main show the
 * publisher/title movies (indices 2, 0, 1); the other two show the paired
 * CURLY intro movies (indices 3, 4). Replacing only these JALs preserves the
 * file-table scan and every surrounding initialization/cleanup path. Later
 * race/event FMVs use other call paths and remain enabled.
 *
 * A replaced MIPS JAL owns its delay slot. The callback therefore reproduces
 * each authenticated `addiu a0,zero,index`, writes the architectural link
 * register, and resumes at PC+8. Guest text is never modified.
 */
enum {
    WIP3_INTRO_PLAY_CURLY44  = 0x80100390u,
    WIP3_INTRO_PLAY_CURLY44B = 0x80100398u,
    WIP3_INTRO_PLAY_PSYG     = 0x801163C0u,
    WIP3_INTRO_PLAY_PSYGLEED = 0x801163C8u,
    WIP3_INTRO_PLAY_WO3      = 0x801163D0u,
    WIP3_INTRO_PLAYER_JAL    = 0x0C0457CDu
};

static int s_wipeout3_intro_skip_active;

static void wipeout3_intro_skip_activate(void) {
    s_wipeout3_intro_skip_active = 1;
}

static int intro_index(uint32_t address, uint32_t* index) {
    if (!index)
        return 0;
    switch (address) {
        case WIP3_INTRO_PLAY_PSYG:
            *index = 2u;
            return 1;
        case WIP3_INTRO_PLAY_PSYGLEED:
            *index = 0u;
            return 1;
        case WIP3_INTRO_PLAY_WO3:
            *index = 1u;
            return 1;
        case WIP3_INTRO_PLAY_CURLY44:
            *index = 3u;
            return 1;
        case WIP3_INTRO_PLAY_CURLY44B:
            *index = 4u;
            return 1;
        default:
            return 0;
    }
}

static uint32_t wipeout3_intro_skip_call(CPUState* cpu, uint32_t address) {
    uint32_t index;
    uint32_t expected_delay;

    if (!s_wipeout3_intro_skip_active || !cpu || !cpu->read_word ||
        !intro_index(address, &index))
        return 0u;

    expected_delay = (address == WIP3_INTRO_PLAY_PSYGLEED)
                         ? 0x00002021u /* move a0,zero */
                         : 0x24040000u | index; /* addiu a0,zero,index */
    if (cpu->read_word(address) != WIP3_INTRO_PLAYER_JAL ||
        cpu->read_word(address + 4u) != expected_delay)
        return 0u;

    cpu->gpr[4] = index;         /* execute the JAL delay slot */
    cpu->gpr[31] = address + 8u; /* architectural JAL return PC */
    return address + 8u;         /* empty presentation; resume caller */
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_intro_skip_plugin) {
    static const uint32_t sites[] = {
        WIP3_INTRO_PLAY_CURLY44,
        WIP3_INTRO_PLAY_CURLY44B,
        WIP3_INTRO_PLAY_PSYG,
        WIP3_INTRO_PLAY_PSYGLEED,
        WIP3_INTRO_PLAY_WO3
    };
    const char* id = "wipeout3.convenience.skip-intro";
    uint32_t i;

    (void)psx_mod_register_activation_plugin(id,
                                             wipeout3_intro_skip_activate);
    for (i = 0u; i < (uint32_t)(sizeof(sites) / sizeof(sites[0])); ++i) {
        (void)psx_mod_register_instruction_site_plugin(
            id, sites[i], wipeout3_intro_skip_call);
    }
}
