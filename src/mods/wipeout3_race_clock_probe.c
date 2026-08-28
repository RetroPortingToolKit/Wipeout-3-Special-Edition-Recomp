#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/*
 * Release-safe oracle for real gameplay speed.
 *
 * In stock SCES-02845, 0x8010DECC is `sw v0, 0x114(a1)` in race-update
 * owner 0x8010D808. The preceding instructions load that per-racer elapsed
 * race clock, add the title's centisecond delta, and leave the new value in
 * v0. Observing the old guest word and the pending v0 at this exact site lets
 * a probe compare title-owned race time with a monotonic host clock.
 *
 * The callback returns zero, so the original store still executes. It never
 * writes guest memory and is inert unless PSX_WIP3_RACE_CLOCK_DIAG is set.
 */
enum {
    WIP3_RACE_CLOCK_STORE = 0x8010DECCu,
    WIP3_RACE_CLOCK_OFFSET = 0x114u
};

static const double WIP3_RACE_CLOCK_WINDOW_SECONDS = 2.0;
static const double WIP3_RACE_CLOCK_MAX_GAP_SECONDS = 0.75;

static int s_race_clock_enabled;
static int s_race_clock_tracking;
static uint32_t s_race_object;
static uint32_t s_start_cs;
static uint32_t s_last_cs;
static uint32_t s_samples;
static uint32_t s_zero_steps;
static uint32_t s_two_steps;
static double s_start_host_seconds;
static double s_last_host_seconds;

static double wipeout3_race_clock_host_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        !QueryPerformanceCounter(&counter))
        return 0.0;
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0.0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
#endif
}

static int wipeout3_race_clock_valid_object(uint32_t object) {
    uint32_t physical = object & 0x1FFFFFFFu;
    return (object & 3u) == 0u && physical >= 0x00010000u &&
           physical + WIP3_RACE_CLOCK_OFFSET + 4u <= 0x00800000u;
}

static void wipeout3_race_clock_begin(uint32_t object, uint32_t value_cs,
                                      uint32_t step_cs,
                                      double host_seconds) {
    s_race_clock_tracking = 1;
    s_race_object = object;
    s_start_cs = value_cs;
    s_last_cs = value_cs;
    s_samples = 1u;
    s_zero_steps = step_cs == 0u ? 1u : 0u;
    s_two_steps = step_cs == 2u ? 1u : 0u;
    s_start_host_seconds = host_seconds;
    s_last_host_seconds = host_seconds;
}

static void wipeout3_race_clock_activate(void) {
    const char* value = getenv("PSX_WIP3_RACE_CLOCK_DIAG");
    s_race_clock_enabled = value && value[0] && value[0] != '0';
    s_race_clock_tracking = 0;
    s_race_object = 0u;
    s_samples = 0u;
    if (s_race_clock_enabled) {
        fputs("psxrecomp: WipEout race-clock diagnostics enabled\n", stdout);
        fflush(stdout);
    }
}

static uint32_t wipeout3_race_clock_store(struct CPUState* cpu,
                                           uint32_t address) {
    uint32_t object;
    uint32_t old_cs;
    uint32_t new_cs;
    uint32_t step_cs;
    double now;
    double wall_seconds;
    double game_seconds;
    double ratio;
    double average_step_cs;
    int eligible;

    (void)address;
    if (!s_race_clock_enabled || !cpu || !psx_mod_game_started())
        return 0u;

    object = cpu->gpr[5];
    new_cs = cpu->gpr[2];
    if (!wipeout3_race_clock_valid_object(object))
        return 0u;

    old_cs = psx_mod_read_word(object + WIP3_RACE_CLOCK_OFFSET);
    if (new_cs < old_cs) {
        if (object == s_race_object)
            s_race_clock_tracking = 0;
        return 0u;
    }
    step_cs = new_cs - old_cs;
    if (step_cs != 0u && step_cs != 2u) {
        if (object == s_race_object)
            s_race_clock_tracking = 0;
        return 0u;
    }

    now = wipeout3_race_clock_host_seconds();
    if (now <= 0.0)
        return 0u;

    if (!s_race_clock_tracking) {
        wipeout3_race_clock_begin(object, new_cs, step_cs, now);
        return 0u;
    }

    if (object != s_race_object) {
        if (now - s_last_host_seconds > WIP3_RACE_CLOCK_MAX_GAP_SECONDS)
            wipeout3_race_clock_begin(object, new_cs, step_cs, now);
        return 0u;
    }

    if (old_cs != s_last_cs || now <= s_last_host_seconds ||
        now - s_last_host_seconds > WIP3_RACE_CLOCK_MAX_GAP_SECONDS) {
        wipeout3_race_clock_begin(object, new_cs, step_cs, now);
        return 0u;
    }

    s_last_cs = new_cs;
    s_last_host_seconds = now;
    ++s_samples;
    if (step_cs == 0u)
        ++s_zero_steps;
    else
        ++s_two_steps;
    wall_seconds = now - s_start_host_seconds;
    if (wall_seconds < WIP3_RACE_CLOCK_WINDOW_SECONDS)
        return 0u;

    game_seconds = (double)(new_cs - s_start_cs) / 100.0;
    ratio = game_seconds / wall_seconds;
    average_step_cs = (double)(s_two_steps * 2u) / (double)s_samples;
    eligible = s_zero_steps != 0u && s_two_steps != 0u;
    printf("psxrecomp: wip3 race clock: object=0x%08X "
           "start_cs=%u end_cs=%u game_seconds=%.6f wall_seconds=%.6f "
           "ratio=%.6f samples=%u zero_steps=%u two_steps=%u "
           "average_step_cs=%.6f eligible=%d\n",
           object, s_start_cs, new_cs, game_seconds, wall_seconds, ratio,
           s_samples, s_zero_steps, s_two_steps, average_step_cs, eligible);
    fflush(stdout);

    wipeout3_race_clock_begin(object, new_cs, step_cs, now);
    return 0u;
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_race_clock_probe_plugins) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.diagnostic.race-clock-on", wipeout3_race_clock_activate);
    (void)psx_mod_register_instruction_site_plugin(
        "wipeout3.diagnostic.race-clock-store", WIP3_RACE_CLOCK_STORE,
        wipeout3_race_clock_store);
}
