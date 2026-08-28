#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * Native form of the four widescreen type-4 billboard guard instructions.
 * The stock text image remains untouched.  These exact operations mirror the
 * FLAG rejection already used by the three sibling billboard paths.
 */
enum {
    WIP3_BILLBOARD_FLAG_READ = 0x8013AD58u,
    WIP3_BILLBOARD_MASK_HI   = 0x8013AD6Cu,
    WIP3_BILLBOARD_MASK_AND  = 0x8013AD70u,
    WIP3_BILLBOARD_MASK_TEST = 0x8013AD74u
};

static int s_billboard_type4_cull_active;

static void activate_billboard_type4_cull(void) {
    s_billboard_type4_cull_active = 1;
}

static uint32_t billboard_type4_cull_site(CPUState* cpu, uint32_t address) {
    uint32_t expected;

    if (!s_billboard_type4_cull_active || !cpu)
        return 0u;

    switch (address) {
        case WIP3_BILLBOARD_FLAG_READ:
            expected = 0x00000000u;
            if (cpu->read_word(address) != expected)
                return 0u;
            /* cfc2 t5,FLAG */
            psx_gte_read(cpu, 13u);
            cpu->gpr[13] = gte_read_ctrl(cpu, 31u);
            return address + 4u;
        case WIP3_BILLBOARD_MASK_HI:
            expected = 0x3C031F80u;
            if (cpu->read_word(address) != expected)
                return 0u;
            /* lui v1,0x8046 */
            cpu->gpr[3] = 0x80460000u;
            return address + 4u;
        case WIP3_BILLBOARD_MASK_AND:
            expected = 0x8C630010u;
            if (cpu->read_word(address) != expected)
                return 0u;
            /* and t5,t5,v1 */
            cpu->gpr[13] &= cpu->gpr[3];
            return address + 4u;
        case WIP3_BILLBOARD_MASK_TEST:
            expected = 0x00000000u;
            if (cpu->read_word(address) != expected)
                return 0u;
            /* sltiu v1,t5,1 */
            cpu->gpr[3] = (cpu->gpr[13] < 1u) ? 1u : 0u;
            return address + 4u;
        default:
            return 0u;
    }
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_billboard_type4_cull_plugin) {
    static const uint32_t sites[] = {
        WIP3_BILLBOARD_FLAG_READ, WIP3_BILLBOARD_MASK_HI,
        WIP3_BILLBOARD_MASK_AND, WIP3_BILLBOARD_MASK_TEST
    };
    const char* id = "wipeout3.widescreen.billboard-type4-cull";
    uint32_t i;

    (void)psx_mod_register_activation_plugin(
        id, activate_billboard_type4_cull);
    for (i = 0u; i < (uint32_t)(sizeof(sites) / sizeof(sites[0])); ++i)
        (void)psx_mod_register_instruction_site_plugin(
            id, sites[i], billboard_type4_cull_site);
}
