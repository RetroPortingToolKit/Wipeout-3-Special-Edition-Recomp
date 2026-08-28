#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_BYTES (2u * 1024u * 1024u)

static uint8_t s_ram[RAM_BYTES];
static PSXModActivationCallback s_activation;
static PSXModInstructionSiteCallback s_instruction;
static uint32_t s_site;
static const char* s_activation_id;
static const char* s_instruction_id;
static const char* s_draw_choice;
static uint32_t s_read_count;

static uint32_t map(uint32_t address) {
    return address & (RAM_BYTES - 1u);
}

static uint32_t read_word(uint32_t address) {
    uint32_t offset = map(address);
    ++s_read_count;
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
    s_activation_id = id;
    s_activation = callback;
    return 1;
}

int psx_mod_register_instruction_site_plugin(
    const char* id, uint32_t address, PSXModInstructionSiteCallback callback) {
    s_instruction_id = id;
    s_site = address;
    s_instruction = callback;
    return 1;
}

int psx_mod_option_value(const char* package_id, const char* feature_id,
                         const char* option_id, char* out,
                         uint32_t out_size) {
    const char* value = s_draw_choice;
    (void)feature_id;
    (void)option_id;
    if (strcmp(package_id, "wipeout3.enhancement.draw-distance-far") != 0 ||
        !value || strlen(value) + 1u > out_size)
        return 0;
    memcpy(out, value, strlen(value) + 1u);
    return 1;
}

#include "../src/mods/wipeout3_draw_distance_far_plugin.c"

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void set_section(uint32_t section, uint32_t mask, int32_t x, int32_t y,
                        int32_t z) {
    write_word(section, mask);
    write_word(section + 0x10u, (uint32_t)x);
    write_word(section + 0x14u, (uint32_t)y);
    write_word(section + 0x18u, (uint32_t)z);
}

int main(void) {
    CPUState cpu;
    const uint32_t root = 0x80190000u;
    const uint32_t sections = 0x80101000u;
    const uint32_t primary_state = 0x80180000u;
    const uint32_t secondary_state = 0x80180040u;
    const uint32_t mask = 0x100u;
    uint32_t continuation;

    memset(&cpu, 0, sizeof(cpu));
    cpu.read_word = read_word;
    cpu.write_word = write_word;

    require(s_activation && s_instruction, "constructor registered callbacks");
    require(strcmp(s_activation_id, "wipeout3.enhancement.draw-distance-far") == 0,
            "activation id");
    require(strcmp(s_instruction_id, s_activation_id) == 0, "shared plugin id");
    require(s_site == 0x801422F4u, "exact stock return site");

    cpu.gpr[29] = 0x1000u;
    cpu.gpr[31] = 0x80123860u;
    require(s_instruction(&cpu, s_site) == 0u, "inactive plugin executes stock JR");
    require(cpu.gpr[29] == 0x1000u, "inactive plugin leaves SP alone");

    write_word(0x801985D4u, root);
    write_word(root + 8u, 5u);
    write_word(root + 0x14u, sections);
    write_word(0x801762CCu, mask);
    write_word(0x801762D4u, primary_state);
    write_word(0x801762D8u, secondary_state);
    write_word(primary_state + 4u, sections);
    write_word(secondary_state + 4u, sections);

    set_section(sections + 0x00u, 0u, 0, 0, 0);
    set_section(sections + 0x40u, 0u, 1000, 0, 0);
    set_section(sections + 0x80u, 0u, 65535, 32766, 65535);
    set_section(sections + 0xC0u, 0xA5A50000u, 65536, 0, 0);
    /* Outside FAR, but already selected by the stock primary list. */
    set_section(sections + 0x100u, mask | 1u, 0, 0, 65536);

    s_activation();
    s_read_count = 0u;
    continuation = s_instruction(&cpu, s_site);
    require(s_read_count <= 40u,
            "far callback caches view/candidate coordinates per invocation");
    require(continuation == 0x80123860u, "JR redirects to RA");
    require(cpu.gpr[29] == 0x1028u, "JR delay slot restores SP");
    require(read_word(sections + 0x00u) == (mask | 3u), "current visible to both");
    require(read_word(sections + 0x40u) == (mask | 3u), "near section visible");
    require(read_word(sections + 0x80u) == (mask | 3u), "inclusive FAR bounds");
    require(read_word(sections + 0xC0u) == 0xA5A50000u,
            "longitudinal value above FAR stays untouched");
    require(read_word(sections + 0x100u) == (mask | 1u),
            "stock-only visibility is preserved");
    require(read_word(0x801762CCu) == mask, "stock scene-mask owner remains owner");

    /* STOCK executes the authenticated owner return without interception. */
    s_draw_choice = "stock";
    s_activation();
    cpu.gpr[29] = 0x2000u;
    require(s_instruction(&cpu, s_site) == 0u,
            "Stock executes authenticated JR");
    require(cpu.gpr[29] == 0x2000u, "Stock leaves delay slot to guest code");

    /* LOW is geometry-only: it clears a stock-only current-generation bit. */
    s_draw_choice = "low";
    s_activation();
    cpu.gpr[29] = 0x3000u;
    set_section(sections + 0x00u, mask | 1u, 0, 0, 0);
    set_section(sections + 0x40u, mask | 1u, 1000, 0, 0);
    set_section(sections + 0x80u, mask | 1u, 12001, 0, 0);
    set_section(sections + 0xC0u, mask | 1u, 14001, 0, 0);
    set_section(sections + 0x100u, mask | 1u, 0, 0, 16001);
    s_instruction(&cpu, s_site);
    require(read_word(sections + 0x00u) == (mask | 3u),
            "Low keeps geometry-visible current section");
    require(read_word(sections + 0x80u) == mask,
            "Low clears stock-only max-planar entry");
    require(read_word(sections + 0xC0u) == mask,
            "Low clears stock-only lateral entry");
    require(read_word(sections + 0x100u) == mask,
            "Low clears stock-only longitudinal entry");

    /* NEAR unions stock entries but observes its 32K max-planar bound. */
    s_draw_choice = "near";
    s_activation();
    cpu.gpr[29] = 0x4000u;
    set_section(sections + 0x80u, 0u, 32001, 0, 0);
    set_section(sections + 0xC0u, mask | 1u, 34001, 0, 0);
    s_instruction(&cpu, s_site);
    require(read_word(sections + 0x80u) == 0u,
            "Near rejects above max-planar");
    require(read_word(sections + 0xC0u) == (mask | 1u),
            "Near preserves stock-only entry");

    /* MID uses the authenticated 56K/48K/44K limits. */
    s_draw_choice = "mid";
    s_activation();
    cpu.gpr[29] = 0x5000u;
    set_section(sections + 0x80u, 0u, 44000, 0, 0);
    set_section(sections + 0xC0u, 0u, 44001, 0, 0);
    s_instruction(&cpu, s_site);
    require(read_word(sections + 0x80u) == (mask | 3u),
            "Mid includes max-planar boundary");
    require(read_word(sections + 0xC0u) == 0u,
            "Mid rejects above max-planar");

    require(s_instruction(&cpu, 0x801422F0u) == 0u, "wrong site is rejected");
    puts("PASS: direct Low/Stock/Near/Mid/Far draw-distance semantics");
    return 0;
}
