#include "mod_plugins.h"

/*
 * High-frame-rate output without touching the game. WipEout 3's simulation,
 * VBlank cadence, timers, and audio stay stock; these callbacks only select how
 * often PSXrecomp's OpenGL presentation thread blends between the two most
 * recent completed game frames. This replaces the old 120 FPS package, which
 * swapped in a patched "unlocked" game binary and needed a CRTC-rate hook the
 * framework no longer has.
 *
 * Motion-adaptive rather than linear: WipEout is fast, and a plain crossfade
 * ghosts the track edges at speed.
 */
static void wipeout3_frame_rate_set(unsigned frames_per_second) {
    (void)psx_mod_set_frame_interpolation_blend(
        PSX_MOD_FRAME_INTERPOLATION_MOTION_ADAPTIVE);
    (void)psx_mod_set_frame_interpolation(frames_per_second);
}

static void wipeout3_frame_rate_60_activate(void) {
    wipeout3_frame_rate_set(60u);
}

static void wipeout3_frame_rate_120_activate(void) {
    wipeout3_frame_rate_set(120u);
}

static void wipeout3_frame_rate_144_activate(void) {
    wipeout3_frame_rate_set(144u);
}

static void wipeout3_frame_rate_165_activate(void) {
    wipeout3_frame_rate_set(165u);
}

/* 0 = follow the display's refresh rate. */
static void wipeout3_frame_rate_display_activate(void) {
    wipeout3_frame_rate_set(0u);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_frame_interpolation_plugin) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.frame-interpolation.60", wipeout3_frame_rate_60_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.frame-interpolation.120", wipeout3_frame_rate_120_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.frame-interpolation.144", wipeout3_frame_rate_144_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.frame-interpolation.165", wipeout3_frame_rate_165_activate);
    (void)psx_mod_register_activation_plugin(
        "wipeout3.frame-interpolation.display",
        wipeout3_frame_rate_display_activate);
}
