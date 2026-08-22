#include "machine_c.h"

#include <stddef.h>

typedef struct c_event_log {
    uint32_t count;
    uint32_t kinds[5];
} c_event_log;

static void c_header_sink(const clean_aot_machine_event* event, void* user) {
    c_event_log* log = (c_event_log*)user;
    if (log->count < 5u) {
        log->kinds[log->count] = event->kind;
    }
    ++log->count;
}

/* A real C translation unit that links to and executes the C++ implementation. */
int main(void) {
    clean_aot_machine_frame_receipt receipt = {0};
    c_event_log log = {0};
    uint64_t numerator = 0;
    uint64_t denominator = 0;
    clean_aot_machine* machine = clean_aot_machine_create(
        CLEAN_AOT_MACHINE_VBLANK_100);

    if (machine == NULL) {
        return 1;
    }
    if (clean_aot_machine_get_cpu_multiplier(
            machine, &numerator, &denominator) !=
            CLEAN_AOT_MACHINE_STATUS_OK ||
        numerator != UINT64_C(1) || denominator != UINT64_C(1)) {
        clean_aot_machine_destroy(machine);
        return 2;
    }
    if (clean_aot_machine_step_frame(machine, c_header_sink, &log, &receipt) !=
        CLEAN_AOT_MACHINE_STATUS_OK) {
        clean_aot_machine_destroy(machine);
        return 3;
    }
    if (receipt.cpu_cycles != UINT64_C(338688) ||
        receipt.spu_samples != UINT64_C(441) ||
        receipt.cpu_multiplier_numerator != UINT64_C(1) ||
        receipt.cpu_multiplier_denominator != UINT64_C(1) ||
        receipt.cpu_retirement_total != UINT64_C(338688) || log.count != 4u) {
        clean_aot_machine_destroy(machine);
        return 4;
    }
    if (log.kinds[0] != CLEAN_AOT_MACHINE_EVENT_VBLANK ||
        log.kinds[1] != CLEAN_AOT_MACHINE_EVENT_CPU_CYCLES ||
        log.kinds[2] != CLEAN_AOT_MACHINE_EVENT_SPU_SAMPLES ||
        log.kinds[3] != CLEAN_AOT_MACHINE_EVENT_CDDA_SECTORS) {
        clean_aot_machine_destroy(machine);
        return 5;
    }
    clean_aot_machine_destroy(machine);

    if (clean_aot_machine_canonicalize_cpu_multiplier(
            UINT64_C(266), UINT64_C(200), &numerator, &denominator) !=
            CLEAN_AOT_MACHINE_STATUS_OK ||
        numerator != UINT64_C(133) || denominator != UINT64_C(100)) {
        return 6;
    }
    if (clean_aot_machine_canonicalize_cpu_multiplier(
            UINT64_C(1), UINT64_C(1), &numerator, &numerator) !=
        CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT) {
        return 7;
    }

    machine = clean_aot_machine_create_with_cpu_multiplier(
        CLEAN_AOT_MACHINE_VBLANK_100, UINT64_C(266), UINT64_C(200));
    if (machine == NULL) {
        return 8;
    }
    for (uint32_t frame = 0; frame < 25u; ++frame) {
        if (clean_aot_machine_step_frame(machine, NULL, NULL, &receipt) !=
            CLEAN_AOT_MACHINE_STATUS_OK) {
            clean_aot_machine_destroy(machine);
            return 9;
        }
    }
    if (receipt.total_cpu_cycles != UINT64_C(11261376) ||
        receipt.cpu_retirement_total != UINT64_C(11261376) ||
        receipt.total_spu_samples != UINT64_C(11025) ||
        receipt.total_cdda_sectors != UINT64_C(18) ||
        receipt.cdda_phase != UINT64_C(75) ||
        receipt.cpu_multiplier_numerator != UINT64_C(133) ||
        receipt.cpu_multiplier_denominator != UINT64_C(100)) {
        clean_aot_machine_destroy(machine);
        return 10;
    }
    clean_aot_machine_destroy(machine);
    return 0;
}
