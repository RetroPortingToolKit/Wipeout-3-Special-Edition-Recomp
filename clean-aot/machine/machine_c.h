#ifndef W3_CLEAN_MACHINE_H
#define W3_CLEAN_MACHINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the deliberately small boundary between a deterministic machine
 * scheduler and its consumers.  It contains no window, audio device, sleep,
 * renderer, disc, or title-specific policy.
 */

#define W3_MACHINE_VBLANK_25 25u
#define W3_MACHINE_VBLANK_50 50u
#define W3_MACHINE_VBLANK_100 100u

#define W3_MACHINE_STATUS_OK 0u
#define W3_MACHINE_STATUS_INVALID_ARGUMENT 1u
#define W3_MACHINE_STATUS_INVALID_RATE 2u

#define W3_MACHINE_EVENT_MODE_SWITCH 1u
#define W3_MACHINE_EVENT_VBLANK 2u
#define W3_MACHINE_EVENT_CPU_CYCLES 3u
#define W3_MACHINE_EVENT_SPU_SAMPLES 4u
#define W3_MACHINE_EVENT_CDDA_SECTORS 5u

typedef uint32_t w3_machine_status;
typedef uint32_t w3_machine_event_kind;

typedef struct w3_machine_event {
    /* A monotonically increasing, zero-based event sequence. */
    uint64_t sequence;
    /* The zero-based frame this event belongs to. */
    uint64_t frame_index;
    /* One of W3_MACHINE_EVENT_*. */
    uint32_t kind;
    /* The VBlank mode in effect for the event. */
    uint32_t vblank_hz;
    /* Event quantity: new Hz for MODE_SWITCH, otherwise domain ticks. */
    uint64_t amount;
    /* Domain total after this event; frame count for VBLANK. */
    uint64_t cumulative;
    /* VBLANK: 1/100-second quanta; MODE_SWITCH: old Hz; CDDA: phase / 100. */
    uint64_t auxiliary;
} w3_machine_event;

typedef void (*w3_machine_event_callback)(const w3_machine_event* event,
                                           void* user);

typedef struct w3_machine_frame_receipt {
    /* The completed frame's zero-based index. */
    uint64_t frame_index;
    uint32_t vblank_hz;
    /* Exact elapsed quanta at 1/100 second per frame-timebase tick. */
    uint32_t timebase_ticks;
    uint64_t cpu_cycles;
    uint64_t spu_samples;
    uint64_t cdda_sectors;
    uint64_t total_cpu_cycles;
    uint64_t total_spu_samples;
    uint64_t total_cdda_sectors;
    /* CDDA accumulator remainder, with an invariant denominator of 100. */
    uint64_t cdda_phase;
    uint64_t elapsed_timebase_ticks;
    uint64_t event_sequence_begin;
    uint64_t event_sequence_end;
    /* FNV-1a receipt digest after this frame's events. */
    uint64_t digest;
} w3_machine_frame_receipt;

typedef struct w3_machine w3_machine;

w3_machine* w3_machine_create(uint32_t initial_vblank_hz);
void w3_machine_destroy(w3_machine* machine);

w3_machine_status w3_machine_set_vblank_rate(w3_machine* machine,
                                               uint32_t vblank_hz);
uint32_t w3_machine_get_vblank_rate(const w3_machine* machine);

w3_machine_status w3_machine_step_frame(w3_machine* machine,
                                         w3_machine_event_callback callback,
                                         void* user,
                                         w3_machine_frame_receipt* receipt);

void w3_machine_reset(w3_machine* machine);
uint64_t w3_machine_frame_count(const w3_machine* machine);
uint64_t w3_machine_receipt_digest(const w3_machine* machine);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* W3_CLEAN_MACHINE_H */
