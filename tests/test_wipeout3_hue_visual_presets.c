#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_BYTES (2u * 1024u * 1024u)
#define MAX_PLUGINS 16u

typedef struct Activation {
    const char* id;
    PSXModActivationCallback callback;
} Activation;

typedef struct Site {
    const char* id;
    uint32_t address;
    PSXModInstructionSiteCallback callback;
} Site;

static uint8_t s_ram[RAM_BYTES];
static Activation s_activations[MAX_PLUGINS];
static Site s_sites[MAX_PLUGINS];
static uint32_t s_activation_count;
static uint32_t s_site_count;
static const char* s_lod_choice;
static const char* s_distance_choice;
static const char* s_birds_choice;
static uint32_t s_skipped_ori_steps;
static uint32_t s_skipped_ori_reg;

static void record_skipped_ori_step(CPUState* cpu, uint32_t reg) {
    (void)cpu;
    ++s_skipped_ori_steps;
    s_skipped_ori_reg = reg;
}

#define WIP3_SKIPPED_ORI_STEP(cpu, reg) \
    record_skipped_ori_step((cpu), (reg))

static uint32_t map(uint32_t address) {
    return address & (RAM_BYTES - 1u);
}

static uint32_t read_word(uint32_t address) {
    uint32_t offset = map(address);
    return (uint32_t)s_ram[offset] |
           ((uint32_t)s_ram[offset + 1u] << 8) |
           ((uint32_t)s_ram[offset + 2u] << 16) |
           ((uint32_t)s_ram[offset + 3u] << 24);
}

static void write_word(uint32_t address, uint32_t value) {
    uint32_t offset = map(address);
    s_ram[offset] = (uint8_t)value;
    s_ram[offset + 1u] = (uint8_t)(value >> 8);
    s_ram[offset + 2u] = (uint8_t)(value >> 16);
    s_ram[offset + 3u] = (uint8_t)(value >> 24);
}

int psx_mod_register_activation_plugin(const char* id,
                                       PSXModActivationCallback callback) {
    s_activations[s_activation_count].id = id;
    s_activations[s_activation_count].callback = callback;
    ++s_activation_count;
    return 1;
}

int psx_mod_register_instruction_site_plugin(
    const char* id, uint32_t address, PSXModInstructionSiteCallback callback) {
    s_sites[s_site_count].id = id;
    s_sites[s_site_count].address = address;
    s_sites[s_site_count].callback = callback;
    ++s_site_count;
    return 1;
}

int psx_mod_option_value(const char* package_id, const char* feature_id,
                         const char* option_id, char* out,
                         uint32_t out_size) {
    const char* value = NULL;
    (void)feature_id;
    (void)option_id;
    if (strcmp(package_id, "wipeout3.enhancement.ship-lod") == 0)
        value = s_lod_choice;
    else if (strcmp(package_id, "wipeout3.enhancement.ship-distance") == 0)
        value = s_distance_choice;
    else if (strcmp(package_id, "wipeout3.enhancement.birds") == 0)
        value = s_birds_choice;
    if (!value || strlen(value) + 1u > out_size)
        return 0;
    memcpy(out, value, strlen(value) + 1u);
    return 1;
}

#include "../src/mods/wipeout3_hue_visual_presets_plugin.c"

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static Activation* activation(const char* id) {
    uint32_t i;
    for (i = 0u; i < s_activation_count; ++i)
        if (strcmp(s_activations[i].id, id) == 0)
            return &s_activations[i];
    return NULL;
}

static Site* site(uint32_t address) {
    uint32_t i;
    for (i = 0u; i < s_site_count; ++i)
        if (s_sites[i].address == address)
            return &s_sites[i];
    return NULL;
}

static void stock_words(void) {
    write_word(0x80130A20u, 0x3C040015u);
    write_word(0x80130A24u, 0x3484F8FFu);
    write_word(0x80130A0Cu, 0x3C020080u);
    write_word(0x80130A10u, 0x3442538Fu);
    write_word(0x801308E4u, 0x28420BB9u);
    write_word(0x801308FCu, 0x28420BB9u);
    write_word(0x8013085Cu, 0x3C110089u);
    write_word(0x8013086Cu, 0x36315440u);
    write_word(0x80152974u, 0x3C020080u);
    write_word(0x80152978u, 0x3442538Fu);
    write_word(0x800FA224u, 0x8E020010u);
}

int main(void) {
    static const char* lod_choices[] = {
        "0", "600", "stock", "1800", "2400", "3000", "3600", "4200",
        "4800", "5400", "6000", "6600", "7200", "7800", "8400",
        "9000", "9600", "10200", "10800", "11400", "12000", "max"
    };
    static const uint32_t lod_thresholds[] = {
        0xFFFFFFFFu, 0x00057E3Fu, 0x0015F8FFu, 0x0031703Fu,
        0x0057E3FFu, 0x0089543Fu, 0x00C5C0FFu, 0x010D2A3Fu,
        0x015F8FFFu, 0x01BCF23Fu, 0x022550FFu, 0x0298AC3Fu,
        0x031703FFu, 0x03A0583Fu, 0x0434A8FFu, 0x04D3F63Fu,
        0x057E3FFFu, 0x0633863Fu, 0x06F3C8FFu, 0x07BF083Fu,
        0x089543FFu, 0x7FFFFFFFu
    };
    CPUState cpu;
    Site* hook;
    uint32_t i;

    memset(&cpu, 0, sizeof(cpu));
    cpu.read_word = read_word;
    stock_words();

    require(s_activation_count == 3u, "three independent feature activations");
    require(s_site_count == 8u, "eight semantic instruction sites");

    hook = site(0x80130A20u);
    require(hook && hook->callback(&cpu, hook->address) == 0u,
            "inactive LOD executes stock");
    require(s_skipped_ori_steps == 0u,
            "inactive LOD does not retire a replacement ORI");
    activation("wipeout3.enhancement.ship-lod")->callback();
    require(hook->callback(&cpu, hook->address) == 0x80130A28u,
            "LOD pair continues after both words");
    require(cpu.gpr[4] == 0x7FFFFFFFu, "default LOD is exact Max threshold");
    require(s_skipped_ori_steps == 1u && s_skipped_ori_reg == 4u,
            "LOD pair retires exactly the skipped a0 ORI");
    write_word(0x80130A24u, 0u);
    require(hook->callback(&cpu, hook->address) == 0u,
            "LOD pair rejects a non-stock second word");
    require(s_skipped_ori_steps == 1u,
            "rejected LOD guard does not retire an ORI");
    write_word(0x80130A24u, 0x3484F8FFu);
    s_lod_choice = "stock";
    activate_ship_lod();
    hook->callback(&cpu, hook->address);
    require(cpu.gpr[4] == 0x0015F8FFu, "LOD Stock choice is exact");

    s_skipped_ori_steps = 0u;
    for (i = 0u; i < 22u; ++i) {
        s_lod_choice = lod_choices[i];
        activate_ship_lod();
        require(hook->callback(&cpu, hook->address) == 0x80130A28u &&
                    cpu.gpr[4] == lod_thresholds[i],
                "all 22 authenticated LOD choices are exact");
    }
    require(s_skipped_ori_steps == 22u && s_skipped_ori_reg == 4u,
            "each authenticated LOD choice retires one a0 ORI");
    s_skipped_ori_steps = 0u;

    activation("wipeout3.enhancement.ship-distance")->callback();
    hook = site(0x80130A0Cu);
    require(hook->callback(&cpu, hook->address) == 0x80130A14u,
            "distance primary pair skips both words");
    require(cpu.gpr[2] == 0x03FFC001u, "default 65K primary square");
    require(s_skipped_ori_steps == 1u && s_skipped_ori_reg == 2u,
            "first distance pair retires exactly the skipped v0 ORI");
    cpu.gpr[2] = 8190u;
    require(site(0x801308E4u)->callback(&cpu, 0x801308E4u) == 0x801308E8u &&
                cpu.gpr[2] == 1u,
            "65K signed radius accepts 8190");
    cpu.gpr[2] = 8191u;
    site(0x801308FCu)->callback(&cpu, 0x801308FCu);
    require(cpu.gpr[2] == 0u, "65K signed radius excludes 8191");
    site(0x8013085Cu)->callback(&cpu, 0x8013085Cu);
    site(0x8013086Cu)->callback(&cpu, 0x8013086Cu);
    require(cpu.gpr[17] == 0x7FFFFFFFu, "65K s1 square");
    hook = site(0x80152974u);
    require(hook->callback(&cpu, hook->address) == 0x8015297Cu &&
                cpu.gpr[2] == 0x03FFC001u,
            "second 65K primary pair");
    require(s_skipped_ori_steps == 2u && s_skipped_ori_reg == 2u,
            "second distance pair retires exactly the skipped v0 ORI");
    s_distance_choice = "stock";
    activate_ship_distance();
    site(0x80130A0Cu)->callback(&cpu, 0x80130A0Cu);
    require(cpu.gpr[2] == 0x0080538Fu, "distance Stock primary square");

    activation("wipeout3.enhancement.birds")->callback();
    hook = site(0x800FA224u);
    require(hook->callback(&cpu, hook->address) == 0x800FA228u &&
                cpu.gpr[2] == 0x7FFF0000u,
            "default Birds High instruction");
    s_birds_choice = "stock";
    activate_birds();
    hook->callback(&cpu, hook->address);
    require(cpu.gpr[2] == 0x00003000u,
            "Birds Stock is Hueponik constant, not original load");
    s_birds_choice = "low";
    activate_birds();
    hook->callback(&cpu, hook->address);
    require(cpu.gpr[2] == 0u, "Birds Low instruction");

    require(read_word(0x80130A20u) == 0x3C040015u &&
                read_word(0x800FA224u) == 0x8E020010u,
            "callbacks never modify guest text");
    puts("PASS: all direct visual selectors, defaults, guards, and stock text");
    return 0;
}
