#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_BYTES (2u * 1024u * 1024u)
#define MAX_SITES 8u

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
static Activation s_activation;
static Site s_sites[MAX_SITES];
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
    s_activation.id = id;
    s_activation.callback = callback;
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

#include "../src/mods/wipeout3_intro_skip_plugin.c"

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static Site* site(uint32_t address) {
    uint32_t i;
    for (i = 0u; i < s_site_count; ++i)
        if (s_sites[i].address == address)
            return &s_sites[i];
    return NULL;
}

static void install_stock_words(void) {
    static const uint32_t addresses[] = {
        0x80100390u, 0x80100398u, 0x801163C0u, 0x801163C8u, 0x801163D0u
    };
    static const uint32_t delays[] = {
        0x24040003u, 0x24040004u, 0x24040002u, 0x00002021u, 0x24040001u
    };
    uint32_t i;
    for (i = 0u; i < 5u; ++i) {
        write_word(addresses[i], 0x0C0457CDu);
        write_word(addresses[i] + 4u, delays[i]);
    }
}

int main(void) {
    static const uint32_t addresses[] = {
        0x80100390u, 0x80100398u, 0x801163C0u, 0x801163C8u, 0x801163D0u
    };
    static const uint32_t indices[] = {3u, 4u, 2u, 0u, 1u};
    CPUState cpu;
    uint32_t i;

    memset(&cpu, 0, sizeof(cpu));
    cpu.read_word = read_word;
    install_stock_words();

    require(s_activation.callback != NULL, "activation registered");
    require(s_site_count == 5u, "exactly five player sites registered");
    require(site(0x801162CCu) == NULL && site(0x8011631Cu) == NULL,
            "file-scan/LBA sites remain unhooked");
    require(site(addresses[0])->callback(&cpu, addresses[0]) == 0u,
            "inactive plugin executes stock player");

    s_activation.callback();
    for (i = 0u; i < 5u; ++i) {
        Site* hook = site(addresses[i]);
        cpu.gpr[4] = 0xFFFFFFFFu;
        cpu.gpr[31] = 0xDEADBEEFu;
        require(hook != NULL, "authenticated player site registered");
        require(hook->callback(&cpu, addresses[i]) == addresses[i] + 8u,
                "hook resumes after JAL delay slot");
        require(cpu.gpr[4] == indices[i], "delay-slot a0 is reproduced");
        require(cpu.gpr[31] == addresses[i] + 8u,
                "architectural JAL link is reproduced");
    }

    write_word(0x801163C0u, 0u);
    require(site(0x801163C0u)->callback(&cpu, 0x801163C0u) == 0u,
            "mutated JAL fails closed to stock execution");
    write_word(0x801163C0u, 0x0C0457CDu);
    write_word(0x801163C4u, 0u);
    require(site(0x801163C0u)->callback(&cpu, 0x801163C0u) == 0u,
            "mutated delay slot fails closed to stock execution");

    puts("PASS: five player calls skipped; delay/link semantics and guards exact");
    return 0;
}
