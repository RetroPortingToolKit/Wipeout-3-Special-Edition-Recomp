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
    clean_aot_machine* machine = clean_aot_machine_create(
        CLEAN_AOT_MACHINE_VBLANK_100);

    if (machine == NULL) {
        return 1;
    }
    if (clean_aot_machine_step_frame(machine, c_header_sink, &log, &receipt) !=
        CLEAN_AOT_MACHINE_STATUS_OK) {
        clean_aot_machine_destroy(machine);
        return 2;
    }
    if (receipt.cpu_cycles != UINT64_C(338688) ||
        receipt.spu_samples != UINT64_C(441) || log.count != 4u) {
        clean_aot_machine_destroy(machine);
        return 3;
    }
    if (log.kinds[0] != CLEAN_AOT_MACHINE_EVENT_VBLANK ||
        log.kinds[1] != CLEAN_AOT_MACHINE_EVENT_CPU_CYCLES ||
        log.kinds[2] != CLEAN_AOT_MACHINE_EVENT_SPU_SAMPLES ||
        log.kinds[3] != CLEAN_AOT_MACHINE_EVENT_CDDA_SECTORS) {
        clean_aot_machine_destroy(machine);
        return 4;
    }
    clean_aot_machine_destroy(machine);
    return 0;
}
