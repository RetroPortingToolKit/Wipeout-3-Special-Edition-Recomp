#include "mod_plugins.h"
#include "cpu_state.h"

/*
 * Null-pool guard for the effect spawner at 0x8014AAD4.
 *
 * The spawner writes 72-byte records into a pool passed in $a0 with no null
 * check: `sw idx,0(a0)` then `sb 1,68(a0 + idx*72 + 4)` plus three vector
 * fills. At the checkpoint-timeout / race-teardown transition the pool table
 * at 0x80191134 holds NULL, and the spawn lands in the BIOS kernel Table of
 * Tables — record 3's flag byte hits 0x120, the EvCB base pointer. The next
 * CD-completion DeliverEvent walks the corrupted pointer and ADEL-crashes
 * minutes later (witnessed: base 0xA000E028 -> 0xA000E001, writer PC
 * 0x8014ABD4 with $t1=0, wtrace slot on 0x120).
 *
 * Real hardware corrupts the same bytes; the retail flow just never reaches
 * this state. Repairing $a0 to a decoy arena at entry keeps the buggy spawn
 * harmless without changing what any other reader of the pool table sees.
 *
 * True-entry discriminator: the hook is emitted pre-CPS, so it also runs on
 * mid-function continuation re-entries where $a0 is dead scratch. A genuine
 * prologue entry has cpu->pc == 0 (compiled direct call) or cpu->pc == the
 * function address (interp dispatch at the entry); continuations carry an
 * interior pc. Only those two shapes are repaired.
 */
enum { WIP3_EFFECT_SPAWNER = 0x8014AAD4u };
enum { WIP3_DECOY_POOL_SIZE = 8192u };

static uint32_t s_decoy_pool;   /* guest KSEG0 address; 0 until allocated */

static void wipeout3_nullpool_guard_activate(void) {
    if (!s_decoy_pool)
        s_decoy_pool = psx_mod_alloc_guest_memory(WIP3_DECOY_POOL_SIZE, 16u);
}

static void wipeout3_nullpool_guard_entry(struct CPUState* cpu,
                                          uint32_t address) {
    if (!cpu || !s_decoy_pool) return;
    if (cpu->gpr[4] != 0u) return;                    /* $a0: pool present */
    if (cpu->pc != 0u && cpu->pc != address) return;  /* continuation re-entry */
    cpu->gpr[4] = s_decoy_pool;
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_nullpool_guard_plugins) {
    (void)psx_mod_register_activation_plugin(
        "wipeout3.fix.null-pool-guard-on", wipeout3_nullpool_guard_activate);
    (void)psx_mod_register_function_entry_plugin(
        "wipeout3.fix.null-pool-guard", WIP3_EFFECT_SPAWNER,
        wipeout3_nullpool_guard_entry);
}
