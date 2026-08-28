#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_BYTES (2u * 1024u * 1024u)

static uint8_t s_ram[RAM_BYTES];
static PSXModActivationCallback s_activation;
static PSXModInstructionSiteCallback s_hooks[4];
static uint32_t s_sites[4];
static uint32_t s_site_count;
static uint32_t s_flag;
static uint32_t s_gte_read_register;

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
    if (strcmp(id, "wipeout3.widescreen.billboard-type4-cull") == 0)
        s_activation = callback;
    return 1;
}

int psx_mod_register_instruction_site_plugin(
    const char* id, uint32_t address, PSXModInstructionSiteCallback callback) {
    if (strcmp(id, "wipeout3.widescreen.billboard-type4-cull") != 0)
        return 0;
    s_sites[s_site_count] = address;
    s_hooks[s_site_count] = callback;
    ++s_site_count;
    return 1;
}

void psx_gte_read(CPUState* cpu, uint32_t rt) {
    (void)cpu;
    s_gte_read_register = rt;
}

uint32_t gte_read_ctrl(CPUState* cpu, uint8_t reg) {
    (void)cpu;
    return reg == 31u ? s_flag : 0u;
}

#include "../src/mods/wipeout3_billboard_type4_cull_plugin.c"

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(void) {
    CPUState cpu;
    uint32_t i;

    memset(&cpu, 0, sizeof(cpu));
    cpu.read_word = read_word;
    write_word(0x8013AD58u, 0x00000000u);
    write_word(0x8013AD6Cu, 0x3C031F80u);
    write_word(0x8013AD70u, 0x8C630010u);
    write_word(0x8013AD74u, 0x00000000u);

    require(s_activation && s_site_count == 4u, "widescreen guard registration");
    require(s_hooks[0](&cpu, s_sites[0]) == 0u, "inactive guard executes stock");
    s_activation();
    s_flag = 0x80460000u;
    for (i = 0u; i < 4u; ++i)
        require(s_hooks[i](&cpu, s_sites[i]) == s_sites[i] + 4u,
                "guard site continuation");
    require(s_gte_read_register == 13u, "CFC2 preserves t5 load destination");
    require(cpu.gpr[13] == 0x80460000u, "FLAG mask matches sibling paths");
    require(cpu.gpr[3] == 0u, "masked error rejects billboard");

    s_flag = 0u;
    for (i = 0u; i < 4u; ++i)
        s_hooks[i](&cpu, s_sites[i]);
    require(cpu.gpr[3] == 1u, "clean FLAG accepts billboard");

    write_word(0x8013AD70u, 0u);
    require(s_hooks[2](&cpu, s_sites[2]) == 0u,
            "non-stock instruction fails closed");
    puts("PASS: native type-4 billboard FLAG rejection and stock guards");
    return 0;
}
