#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_BYTES (2u * 1024u * 1024u)

static uint8_t s_ram[RAM_BYTES];
static PSXModActivationCallback s_activation;
static PSXModInstructionSiteCallback s_site_callback;
static uint32_t s_sites[2];
static uint32_t s_site_count;

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
    if (strcmp(id, "wipeout3.enhancement.hue-8mb-title") == 0)
        s_activation = callback;
    return 1;
}

int psx_mod_register_instruction_site_plugin(
    const char* id, uint32_t address, PSXModInstructionSiteCallback callback) {
    if (strcmp(id, "wipeout3.enhancement.hue-8mb-title") == 0 &&
        s_site_count < 2u) {
        s_sites[s_site_count++] = address;
        s_site_callback = callback;
    }
    return 1;
}

#include "../src/mods/wipeout3_hue_8mb_title_plugin.c"

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(void) {
    CPUState cpu;

    memset(&cpu, 0, sizeof(cpu));
    cpu.read_word = read_word;
    write_word(0x8014BAD0u, 0x8C449804u);
    write_word(0x8015FA80u, 0x00052BC0u);

    require(s_activation != NULL, "activation registered");
    require(s_site_callback != NULL && s_site_count == 2u,
            "exactly two instruction sites registered");
    require(s_sites[0] == 0x8014BAD0u && s_sites[1] == 0x8015FA80u,
            "authenticated site order");
    require(s_site_callback(&cpu, 0x8014BAD0u) == 0u,
            "inactive feature executes stock");

    s_activation();
    cpu.gpr[4] = 0xDEADBEEFu;
    require(s_site_callback(&cpu, 0x8014BAD0u) == 0x8014BAD4u &&
                cpu.gpr[4] == 1u,
            "8 MB mode argument is one");

    cpu.gpr[5] = 3u;
    require(s_site_callback(&cpu, 0x8015FA80u) == 0x8015FA84u &&
                cpu.gpr[5] == 0x00600000u,
            "range argument is shifted by twenty-one");

    write_word(0x8015FA80u, 0u);
    cpu.gpr[5] = 7u;
    require(s_site_callback(&cpu, 0x8015FA80u) == 0u && cpu.gpr[5] == 7u,
            "non-stock word fails closed");
    require(s_site_callback(&cpu, 0x8014BAD4u) == 0u,
            "unknown address fails closed");

    puts("PASS: exact Hueponik 8 MB title semantics; two guarded native sites");
    return 0;
}
