#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <string.h>

/*
 * Generated instruction-site hooks charge the instruction at the hook before
 * calling us.  A callback that replaces a LUI/ORI pair must therefore retire
 * the skipped ORI explicitly before resuming at the comparison.  The test
 * harness overrides this narrow macro to prove the exact count/register.
 */
#ifndef WIP3_SKIPPED_ORI_STEP
#ifdef PSX_ENABLE_BLOCK_CYCLES
#include "psx_cyc.h"
#define WIP3_SKIPPED_ORI_STEP(cpu, reg) psx_cyc_step_1((cpu), (reg))
#else
#define WIP3_SKIPPED_ORI_STEP(cpu, reg) \
    do {                                  \
        (void)(cpu);                      \
        (void)(reg);                      \
    } while (0)
#endif
#endif

/*
 * Direct-native equivalents of three Hueponik ac46a5d menu rows:
 *
 *   SHIP LOD  MAX
 *   SHIP DIST 65K
 *   BIRDS     HIGH
 *
 * The host catalog exposes every authentic 8 MiB choice and defaults to the
 * target values above. Hueponik applies these selections by copying halfwords from its option
 * descriptors into stock instructions.  Here the authenticated stock words
 * remain in guest RAM and in generated AOT.  A callback first verifies the
 * complete stock word, then performs the selected instruction's exact MIPS
 * register semantics.  No menu, code island, high-RAM engine, or code write is
 * installed.  Track 4x4 is deliberately outside this plugin.
 */
enum {
    WIP3_SHIP_LOD_MAX_HI       = 0x80130A20u,
    WIP3_SHIP_LOD_MAX_LO       = 0x80130A24u,
    WIP3_SHIP_DIST_65K_A_HI    = 0x80130A0Cu,
    WIP3_SHIP_DIST_65K_A_LO    = 0x80130A10u,
    WIP3_SHIP_DIST_65K_LIMIT_A = 0x801308E4u,
    WIP3_SHIP_DIST_65K_LIMIT_B = 0x801308FCu,
    WIP3_SHIP_DIST_65K_B_HI    = 0x8013085Cu,
    WIP3_SHIP_DIST_65K_B_LO    = 0x8013086Cu,
    WIP3_SHIP_DIST_65K_C_HI    = 0x80152974u,
    WIP3_SHIP_DIST_65K_C_LO    = 0x80152978u,
    WIP3_BIRDS_HIGH            = 0x800FA224u
};

static int s_ship_lod_active;
static int s_ship_distance_active;
static int s_birds_active;
static uint32_t s_ship_lod_threshold;
static uint32_t s_ship_distance_primary_square;
static uint32_t s_ship_distance_s1_square;
static uint32_t s_ship_distance_radius;
static uint32_t s_birds_value;

static uint32_t rd32(CPUState* cpu, uint32_t address) {
    return cpu->read_word(address);
}

static void activate_ship_lod(void) {
    static const struct {
        const char* name;
        uint32_t threshold;
    } choices[] = {
        {"0", 0xFFFFFFFFu},       {"600", 0x00057E3Fu},
        {"stock", 0x0015F8FFu},   {"1800", 0x0031703Fu},
        {"2400", 0x0057E3FFu},    {"3000", 0x0089543Fu},
        {"3600", 0x00C5C0FFu},    {"4200", 0x010D2A3Fu},
        {"4800", 0x015F8FFFu},    {"5400", 0x01BCF23Fu},
        {"6000", 0x022550FFu},    {"6600", 0x0298AC3Fu},
        {"7200", 0x031703FFu},    {"7800", 0x03A0583Fu},
        {"8400", 0x0434A8FFu},    {"9000", 0x04D3F63Fu},
        {"9600", 0x057E3FFFu},    {"10200", 0x0633863Fu},
        {"10800", 0x06F3C8FFu},  {"11400", 0x07BF083Fu},
        {"12000", 0x089543FFu},  {"max", 0x7FFFFFFFu}
    };
    char selected[16] = "max";
    uint32_t i;

    (void)psx_mod_option_value("wipeout3.enhancement.ship-lod", "ship-lod",
                               "distance", selected,
                               (uint32_t)sizeof(selected));
    s_ship_lod_threshold = 0x7FFFFFFFu;
    for (i = 0u; i < (uint32_t)(sizeof(choices) / sizeof(choices[0])); ++i) {
        if (strcmp(selected, choices[i].name) == 0) {
            s_ship_lod_threshold = choices[i].threshold;
            break;
        }
    }
    s_ship_lod_active = 1;
}

static void activate_ship_distance(void) {
    static const struct {
        const char* name;
        uint32_t primary_square;
        uint32_t radius;
        uint32_t s1_square;
    } choices[] = {
        {"stock", 0x0080538Fu, 0x0BB9u, 0x00895440u},
        {"24k",   0x00895440u, 0x0BB8u, 0x7FFFFFFFu},
        {"40k",   0x017D7840u, 0x1388u, 0x7FFFFFFFu},
        {"52k",   0x0284AF10u, 0x1964u, 0x7FFFFFFFu},
        {"65k",   0x03FFC001u, 0x1FFFu, 0x7FFFFFFFu}
    };
    char selected[16] = "65k";
    uint32_t i;

    (void)psx_mod_option_value(
        "wipeout3.enhancement.ship-distance", "ship-distance", "distance",
        selected, (uint32_t)sizeof(selected));
    s_ship_distance_primary_square = 0x03FFC001u;
    s_ship_distance_radius = 0x1FFFu;
    s_ship_distance_s1_square = 0x7FFFFFFFu;
    for (i = 0u; i < (uint32_t)(sizeof(choices) / sizeof(choices[0])); ++i) {
        if (strcmp(selected, choices[i].name) == 0) {
            s_ship_distance_primary_square = choices[i].primary_square;
            s_ship_distance_radius = choices[i].radius;
            s_ship_distance_s1_square = choices[i].s1_square;
            break;
        }
    }
    s_ship_distance_active = 1;
}

static void activate_birds(void) {
    char selected[16] = "high";

    (void)psx_mod_option_value("wipeout3.enhancement.birds", "birds",
                               "detail", selected,
                               (uint32_t)sizeof(selected));
    if (strcmp(selected, "low") == 0)
        s_birds_value = 0u;
    else if (strcmp(selected, "stock") == 0)
        s_birds_value = 0x00003000u;
    else
        s_birds_value = 0x7FFF0000u;
    s_birds_active = 1;
}

static uint32_t ship_lod_site(CPUState* cpu, uint32_t address) {
    if (!s_ship_lod_active || !cpu || address != WIP3_SHIP_LOD_MAX_HI)
        return 0u;

    if (rd32(cpu, WIP3_SHIP_LOD_MAX_HI) != 0x3C040015u ||
        rd32(cpu, WIP3_SHIP_LOD_MAX_LO) != 0x3484F8FFu)
        return 0u;

    /* Model the selected LUI/ORI pair and continue after both words. */
    WIP3_SKIPPED_ORI_STEP(cpu, 4u);
    cpu->gpr[4] = s_ship_lod_threshold;
    return address + 8u;
}

static uint32_t ship_distance_site(CPUState* cpu, uint32_t address) {
    if (!s_ship_distance_active || !cpu)
        return 0u;

    switch (address) {
        case WIP3_SHIP_DIST_65K_A_HI:
        case WIP3_SHIP_DIST_65K_C_HI:
            if (rd32(cpu, address) != 0x3C020080u ||
                rd32(cpu, address + 4u) != 0x3442538Fu)
                return 0u;
            WIP3_SKIPPED_ORI_STEP(cpu, 2u);
            cpu->gpr[2] = s_ship_distance_primary_square;
            return address + 8u;
        case WIP3_SHIP_DIST_65K_LIMIT_A:
        case WIP3_SHIP_DIST_65K_LIMIT_B:
            if (rd32(cpu, address) != 0x28420BB9u)
                return 0u;
            /* slti v0,v0,0x0fa0 (signed, exactly as the selected word). */
            cpu->gpr[2] =
                ((int32_t)cpu->gpr[2] < (int32_t)s_ship_distance_radius)
                    ? 1u : 0u;
            return address + 4u;
        case WIP3_SHIP_DIST_65K_B_HI:
            if (rd32(cpu, address) != 0x3C110089u)
                return 0u;
            cpu->gpr[17] = s_ship_distance_s1_square & 0xFFFF0000u;
            return address + 4u;
        case WIP3_SHIP_DIST_65K_B_LO:
            if (rd32(cpu, address) != 0x36315440u)
                return 0u;
            cpu->gpr[17] |= s_ship_distance_s1_square & 0x0000FFFFu;
            return address + 4u;
        default:
            return 0u;
    }
}

static uint32_t birds_site(CPUState* cpu, uint32_t address) {
    if (!s_birds_active || !cpu || address != WIP3_BIRDS_HIGH ||
        rd32(cpu, address) != 0x8E020010u)
        return 0u;

    cpu->gpr[2] = s_birds_value;
    return address + 4u;
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_hue_visual_presets_plugin) {
    static const uint32_t distance_sites[] = {
        WIP3_SHIP_DIST_65K_A_HI,
        WIP3_SHIP_DIST_65K_LIMIT_A, WIP3_SHIP_DIST_65K_LIMIT_B,
        WIP3_SHIP_DIST_65K_B_HI, WIP3_SHIP_DIST_65K_B_LO,
        WIP3_SHIP_DIST_65K_C_HI
    };
    const char* lod_id = "wipeout3.enhancement.ship-lod";
    const char* distance_id = "wipeout3.enhancement.ship-distance";
    const char* birds_id = "wipeout3.enhancement.birds";
    uint32_t i;

    (void)psx_mod_register_activation_plugin(lod_id, activate_ship_lod);
    (void)psx_mod_register_instruction_site_plugin(
        lod_id, WIP3_SHIP_LOD_MAX_HI, ship_lod_site);

    (void)psx_mod_register_activation_plugin(
        distance_id, activate_ship_distance);
    for (i = 0u;
         i < (uint32_t)(sizeof(distance_sites) / sizeof(distance_sites[0]));
         ++i)
        (void)psx_mod_register_instruction_site_plugin(
            distance_id, distance_sites[i], ship_distance_site);

    (void)psx_mod_register_activation_plugin(birds_id, activate_birds);
    (void)psx_mod_register_instruction_site_plugin(
        birds_id, WIP3_BIRDS_HIGH, birds_site);
}
