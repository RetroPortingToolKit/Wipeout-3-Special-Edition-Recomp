#include "cpu_state.h"
#include "host_time.h"
#include "mod_plugins.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Release-safe, read-only gameplay progression oracle for automated probes.
 *
 * 0x800F04E8 is the stock title's per-ship simulation owner. Its three known
 * callers are mutually exclusive paths in the ship loop, so one stable a0
 * object receives one call per completed simulation update. Counting the
 * busiest object over 100 guest VBlanks measures title progression without
 * confusing a healthy host/audio cadence for healthy gameplay.
 *
 * Nothing is enabled unless the probe explicitly exports
 * PSX_WIP3_PROGRESS_DIAG=1. The callbacks never write guest memory.
 */
enum {
    WIP3_SHIP_UPDATE_OWNER = 0x800F04E8u,
    WIP3_PROGRESS_WINDOW_VBLANKS = 100u,
    WIP3_PROGRESS_OBJECT_CAPACITY = 32u
};

typedef struct Wip3ProgressObject {
    uint32_t address;
    uint32_t updates;
} Wip3ProgressObject;

static int s_progress_enabled;
static int s_progress_tracking;
static uint32_t s_progress_vblanks;
static uint32_t s_first_update_vblank;
static uint32_t s_last_update_vblank;
static uint32_t s_total_updates;
static uint32_t s_window_scheduler_calls;
static uint32_t s_window_logical_ticks;
static uint32_t s_window_race_centiseconds;
static uint64_t s_window_wall_start_ms;
static Wip3ProgressObject s_objects[WIP3_PROGRESS_OBJECT_CAPACITY];

/* Read-only fixed-selector timing state exported by the title plugin. */
extern uint32_t wipeout3_pal100_scheduler_calls(void);
extern uint32_t wipeout3_pal100_logical_ticks(void);
extern uint32_t wipeout3_pal100_race_centiseconds(void);
extern uint32_t wipeout3_pal100_current_rate(void);
extern uint32_t wipeout3_pal100_current_coefficient(void);

static double wall_seconds_since(uint64_t start_ms) {
    return (double)(psx_host_mono_ms() - start_ms) / 1000.0;
}

static void wipeout3_progress_reset_window(void) {
    uint32_t i;
    s_progress_vblanks = 0;
    s_first_update_vblank = WIP3_PROGRESS_WINDOW_VBLANKS;
    s_last_update_vblank = 0;
    s_total_updates = 0;
    s_window_scheduler_calls = wipeout3_pal100_scheduler_calls();
    s_window_logical_ticks = wipeout3_pal100_logical_ticks();
    s_window_race_centiseconds = wipeout3_pal100_race_centiseconds();
    s_window_wall_start_ms = psx_host_mono_ms();
    for (i = 0; i < WIP3_PROGRESS_OBJECT_CAPACITY; ++i) {
        s_objects[i].address = 0;
        s_objects[i].updates = 0;
    }
}

static void wipeout3_progress_activate(void) {
    const char* value = getenv("PSX_WIP3_PROGRESS_DIAG");
    s_progress_enabled = value && value[0] && value[0] != '0';
    s_progress_tracking = 0;
    wipeout3_progress_reset_window();
    if (s_progress_enabled) {
        fputs("psxrecomp: WipEout title progression diagnostics enabled\n",
              stdout);
        fflush(stdout);
    }
}

static void wipeout3_progress_ship_update(struct CPUState* cpu,
                                          uint32_t address) {
    uint32_t object;
    uint32_t i;
    (void)address;
    if (!s_progress_enabled || !psx_mod_game_started())
        return;

    object = cpu->gpr[4];
    if ((object & 3u) != 0u || (object & 0x1FFFFFFFu) < 0x00010000u ||
        (object & 0x1FFFFFFFu) >= 0x00800000u)
        return;

    if (!s_progress_tracking) {
        s_progress_tracking = 1;
        wipeout3_progress_reset_window();
    }
    if (s_first_update_vblank == WIP3_PROGRESS_WINDOW_VBLANKS)
        s_first_update_vblank = s_progress_vblanks;
    s_last_update_vblank = s_progress_vblanks;
    ++s_total_updates;

    for (i = 0; i < WIP3_PROGRESS_OBJECT_CAPACITY; ++i) {
        if (s_objects[i].address == object || s_objects[i].address == 0u) {
            if (s_objects[i].address == 0u)
                s_objects[i].address = object;
            ++s_objects[i].updates;
            return;
        }
    }
}

static void wipeout3_progress_vblank(void) {
    uint32_t i;
    uint32_t unique = 0;
    uint32_t max_updates = 0;
    int eligible;
    double ratio;
    uint32_t scheduler_calls;
    uint32_t logical_ticks;
    uint32_t rate;
    uint32_t coefficient;
    uint32_t race_centiseconds;
    double wall_seconds;
    if (!s_progress_enabled || !psx_mod_game_started())
        return;
    if (!s_progress_tracking) {
        s_progress_tracking = 1;
        wipeout3_progress_reset_window();
    }
    if (++s_progress_vblanks < WIP3_PROGRESS_WINDOW_VBLANKS)
        return;

    for (i = 0; i < WIP3_PROGRESS_OBJECT_CAPACITY; ++i) {
        if (s_objects[i].address != 0u) {
            ++unique;
            if (s_objects[i].updates > max_updates)
                max_updates = s_objects[i].updates;
        }
    }
    eligible = unique != 0u && s_first_update_vblank <= 10u &&
               s_last_update_vblank >= 89u;
    ratio = (double)max_updates / (double)WIP3_PROGRESS_WINDOW_VBLANKS;
    scheduler_calls = wipeout3_pal100_scheduler_calls() -
                      s_window_scheduler_calls;
    logical_ticks = wipeout3_pal100_logical_ticks() -
                    s_window_logical_ticks;
    race_centiseconds = wipeout3_pal100_race_centiseconds() -
                        s_window_race_centiseconds;
    rate = wipeout3_pal100_current_rate();
    coefficient = wipeout3_pal100_current_coefficient();
    wall_seconds = wall_seconds_since(s_window_wall_start_ms);
    printf("psxrecomp: wip3 timing: vblanks=%u scheduler_calls=%u "
           "logical_ticks=%u race_centiseconds=%u calls_per_vblank=%.4f "
           "ticks_per_vblank=%.4f wall_seconds=%.6f "
           "vblanks_per_wall_second=%.4f ticks_per_wall_second=%.4f "
           "race_cs_per_wall_second=%.4f "
           "rate=%u coefficient=0x%04X exact_pal100=%d\n",
           WIP3_PROGRESS_WINDOW_VBLANKS, scheduler_calls, logical_ticks,
           race_centiseconds,
           (double)scheduler_calls / (double)WIP3_PROGRESS_WINDOW_VBLANKS,
           (double)logical_ticks / (double)WIP3_PROGRESS_WINDOW_VBLANKS,
           wall_seconds,
           wall_seconds > 0.0 ?
               (double)WIP3_PROGRESS_WINDOW_VBLANKS / wall_seconds : 0.0,
           wall_seconds > 0.0 ? (double)logical_ticks / wall_seconds : 0.0,
           wall_seconds > 0.0 ?
               (double)race_centiseconds / wall_seconds : 0.0,
           rate, coefficient,
           scheduler_calls == 100u && logical_ticks == 25u &&
                   race_centiseconds == 100u && rate == 1u &&
                   coefficient == 0x0862u
               ? 1
               : 0);
    printf("psxrecomp: wip3 progression: vblanks=%u "
           "max_object_updates=%u ratio=%.4f unique_objects=%u "
           "total_updates=%u first_update_vblank=%u "
           "last_update_vblank=%u eligible=%d\n",
           WIP3_PROGRESS_WINDOW_VBLANKS, max_updates, ratio, unique,
           s_total_updates, s_first_update_vblank, s_last_update_vblank,
           eligible);
    fflush(stdout);
    wipeout3_progress_reset_window();
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_progress_probe_plugins) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.diagnostic.progress-on", wipeout3_progress_activate);
    (void)psx_mod_register_function_entry_plugin(
        "wipeout3.diagnostic.progress-ship-update", WIP3_SHIP_UPDATE_OWNER,
        wipeout3_progress_ship_update);
    (void)psx_mod_register_vblank_plugin(
        "wipeout3.diagnostic.progress-vblank", wipeout3_progress_vblank);
}
