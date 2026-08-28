#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * Direct-native form of Hueponik ac46a5d's complete pre-island 8 MiB title
 * handshake.  The published patch changes exactly these two stock words:
 *
 *   8014BAD0  lw a0,-0x67fc(v0)  -> addiu a0,zero,1
 *   8015FA80  sll a1,a1,15       -> sll   a1,a1,21
 *
 * Keep the authenticated stock words in guest RAM/AOT and execute the patched
 * register semantics only while the host feature is active.  This does not
 * install Hueponik's menu, high-RAM code/data island, or Track 4x4 engine.
 */
enum {
    WIP3_8MB_MODE_ARGUMENT = 0x8014BAD0u,
    WIP3_8MB_RANGE_SCALE   = 0x8015FA80u
};

static int s_wipeout3_hue_8mb_title_active;

static void wipeout3_hue_8mb_title_activate(void) {
    s_wipeout3_hue_8mb_title_active = 1;
}

static uint32_t wipeout3_hue_8mb_title_site(struct CPUState* cpu,
                                             uint32_t address) {
    if (!s_wipeout3_hue_8mb_title_active || !cpu || !cpu->read_word)
        return 0u;

    switch (address) {
        case WIP3_8MB_MODE_ARGUMENT:
            if (cpu->read_word(address) != 0x8C449804u)
                return 0u;
            cpu->gpr[4] = 1u;
            return address + 4u;

        case WIP3_8MB_RANGE_SCALE:
            if (cpu->read_word(address) != 0x00052BC0u)
                return 0u;
            cpu->gpr[5] <<= 21u;
            return address + 4u;

        default:
            return 0u;
    }
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_hue_8mb_title_plugin) {
    static const uint32_t sites[] = {
        WIP3_8MB_MODE_ARGUMENT,
        WIP3_8MB_RANGE_SCALE
    };
    const char* id = "wipeout3.enhancement.hue-8mb-title";
    uint32_t i;

    (void)psx_mod_register_activation_plugin(
        id, wipeout3_hue_8mb_title_activate);
    for (i = 0u; i < (uint32_t)(sizeof(sites) / sizeof(sites[0])); ++i) {
        (void)psx_mod_register_instruction_site_plugin(
            id, sites[i], wipeout3_hue_8mb_title_site);
    }
}
