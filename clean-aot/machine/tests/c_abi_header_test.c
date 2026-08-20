#include "machine_c.h"

static void c_header_sink(const w3_machine_event* event, void* user) {
    (void)event;
    (void)user;
}

/* Compile-only translation unit proving the public header is C-compatible. */
void w3_clean_machine_c_header_test(void) {
    w3_machine* machine = w3_machine_create(W3_MACHINE_VBLANK_100);
    w3_machine_frame_receipt receipt = {0};
    (void)w3_machine_set_vblank_rate(machine, W3_MACHINE_VBLANK_50);
    (void)w3_machine_step_frame(machine, c_header_sink, 0, &receipt);
    w3_machine_destroy(machine);
}
