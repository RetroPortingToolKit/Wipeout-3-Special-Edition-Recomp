#include "mod_plugins.h"

/*
 * AOT cadence for wipeout3.enhancement.framerate (1.0.14+).
 *
 * Probe finding: a VBlank-only write to gSimVblankSkip loses a race — hueponik
 * island/menu can store skip=1 after mod_runtime_on_vblank() and before
 * SimVblankGate's AOT `lw`. Title "Game 60 fps" is CRTC vblanks (expected);
 * doubled *sim* is that lw seeing 1.
 *
 * Fix: also hook SimVblankGate (0x800EDFC8) via mod_function_entry, emitted
 * BEFORE the CPS continuation switch so mid-function re-entries still force
 * skip=2 immediately before the gate body / skip load. VBlank write remains
 * as a backup. Address: lui 0x8018 + signed 0xF02C → 0x8017F02C.
 */
enum { WIP3_SKIP_ADDR = 0x8017F02Cu };
enum { WIP3_SIM_GATE = 0x800EDFC8u };

static int s_skip2_active;

static void wipeout3_force_skip2(void) {
    psx_mod_write_word(WIP3_SKIP_ADDR, 2u);
}

static void wipeout3_framerate_skip2_activate(void) {
    s_skip2_active = 1;
}

static void wipeout3_framerate_skip2_vblank(void) {
    if (!s_skip2_active || !psx_mod_game_started())
        return;
    wipeout3_force_skip2();
}

static void wipeout3_framerate_skip2_entry(struct CPUState* cpu, uint32_t address) {
    (void)cpu;
    (void)address;
    if (!s_skip2_active)
        return;
    wipeout3_force_skip2();
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_framerate_30_plugins) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.framerate.skip2-on", wipeout3_framerate_skip2_activate);
    (void)psx_mod_register_vblank_plugin(
        "wipeout3.framerate.skip2", wipeout3_framerate_skip2_vblank);
    (void)psx_mod_register_function_entry_plugin(
        "wipeout3.framerate.skip2-entry", WIP3_SIM_GATE,
        wipeout3_framerate_skip2_entry);
}
