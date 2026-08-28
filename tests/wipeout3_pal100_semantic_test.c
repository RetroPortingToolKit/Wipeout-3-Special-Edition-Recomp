#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include the title implementation so its bounded static helpers are tested. */
#include "../src/mods/wipeout3_framerate_100_plugin.c"

static uint8_t ram[8u * 1024u * 1024u];
static unsigned checks;

#define CHECK(expr)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(expr)) {                                                             \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr);                  \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static uint32_t phys(uint32_t address) { return address & 0x007FFFFFu; }
static uint8_t mem_r8(uint32_t address) { return ram[phys(address)]; }
static void mem_w8(uint32_t address, uint8_t value) {
  ram[phys(address)] = value;
}
static uint16_t mem_r16(uint32_t address) {
  uint32_t p = phys(address);
  return (uint16_t)(ram[p] | ((uint16_t)ram[p + 1u] << 8));
}
static void mem_w16(uint32_t address, uint16_t value) {
  uint32_t p = phys(address);
  ram[p] = (uint8_t)value;
  ram[p + 1u] = (uint8_t)(value >> 8);
}
static uint32_t mem_r32(uint32_t address) {
  return (uint32_t)mem_r16(address) | ((uint32_t)mem_r16(address + 2u) << 16);
}
static void mem_w32(uint32_t address, uint32_t value) {
  mem_w16(address, (uint16_t)value);
  mem_w16(address + 2u, (uint16_t)(value >> 16));
}

/* Link stubs: constructors register against these in the standalone test. */
int psx_mod_register_activation_plugin(const char *id,
                                       PSXModActivationCallback callback) {
  (void)id;
  (void)callback;
  return 1;
}
int psx_mod_register_instruction_site_plugin(
    const char *id, uint32_t address, PSXModInstructionSiteCallback callback) {
  (void)id;
  (void)address;
  (void)callback;
  return 1;
}
int psx_mod_register_state_plugin(const char *id, uint32_t version,
                                  PSXModStateSizeCallback size_callback,
                                  PSXModStateSaveCallback save_callback,
                                  PSXModStateLoadCallback load_callback) {
  (void)id;
  (void)version;
  (void)size_callback;
  (void)save_callback;
  (void)load_callback;
  return 1;
}
static uint32_t last_crtc_multiplier;
static uint32_t last_native_vblank_rate;
static uint32_t host_cadence_calls;
int psx_mod_set_crtc_refresh_multiplier(uint32_t value) {
  last_crtc_multiplier = value;
  ++host_cadence_calls;
  return value == 1u || value == 2u;
}
int psx_mod_set_native_vblank_rate(uint32_t value) {
  last_native_vblank_rate = value;
  ++host_cadence_calls;
  return value == 50u || value == 100u;
}

static void reset_cpu(CPUState *cpu) {
  memset(cpu, 0, sizeof(*cpu));
  cpu->read_word = mem_r32;
  cpu->write_word = mem_w32;
  cpu->read_half = mem_r16;
  cpu->write_half = mem_w16;
  cpu->read_byte = mem_r8;
  cpu->write_byte = mem_w8;
}

static uint32_t invoke(CPUState *cpu, uint32_t site, uint32_t continuation) {
  uint32_t actual = pal100_title_site(cpu, site);
  if (actual != continuation)
    fprintf(stderr, "site %08X continued at %08X, expected %08X\n", site,
            actual, continuation);
  CHECK(actual == continuation);
  return actual;
}

static void test_arithmetic(void) {
  CHECK(mix_x(0x12345678u, 123) == 0x0F4F795Cu);
  CHECK(mix_y(0x89ABCDEFu, 0x0F4F795Cu, 123) == 0x903D5ABCu);
  CHECK(mix_x(0xFFFFFFFFu, -321) == 0xFFFE6739u);
  CHECK(mix_y(0x80000000u, 0xFFFE6739u, -321) == 0x84332BF3u);
  CHECK(fixed_to_half(0x00010000u) == 1u);
  CHECK(fixed_to_half(0xFFFF0000u) == UINT32_MAX);
  CHECK(fixed_to_half(0x80000000u) == 0x00008000u);
}

static void test_native_state_roundtrip(void) {
  uint8_t payload[WIP3_PAL_STATE_BYTES];
  Wip3PAL100State expected_timing;
  Wip3HighRateRecord expected_high_rate[WIP3_SHARED_RECORD_COUNT];

  pal100_activate();
  pal_enable_host_cadence();
  s_timing.phase = 7u;
  s_timing.phase_wrap = 1u;
  s_timing.work_gate = 1u;
  s_timing.random_counter = 0x12345678u;
  s_timing.race_phase = 1u;
  s_timing.race_tick = 1u;
  s_timing.race_centiseconds = 54321u;
  s_timing.frame_event = 1u;
  s_timing.scheduler_calls = 2345u;
  s_timing.logical_ticks = 1172u;
  s_high_rate[0].x = 0x11223344u;
  s_high_rate[0].y = 0x55667788u;
  s_high_rate[0].packed = 0x99AABBCCu;
  s_high_rate[255].x = 0xDEADBEEFu;
  s_high_rate[255].y = 0x10203040u;
  s_high_rate[255].packed = 0x50607080u;
  expected_timing = s_timing;
  memcpy(expected_high_rate, s_high_rate, sizeof(expected_high_rate));

  CHECK(pal_state_size() == sizeof(payload));
  CHECK(pal_state_save(payload, sizeof(payload)));

  pal25_activate();
  CHECK(pal100_state_load(payload, sizeof(payload), 0));
  CHECK(s_timing.selector == 0u);
  CHECK(!pal25_state_load(payload, sizeof(payload), 0));
  CHECK(!pal100_state_load(payload, sizeof(payload) - 4u, 0));

  CHECK(pal100_state_load(payload, sizeof(payload), 1));
  CHECK(s_pal_active == 1 && s_pal_host_cadence_active == 1);
  CHECK(memcmp(&s_timing, &expected_timing, sizeof(s_timing)) == 0);
  CHECK(memcmp(s_high_rate, expected_high_rate, sizeof(s_high_rate)) == 0);
  CHECK(last_crtc_multiplier == 2u && last_native_vblank_rate == 100u);

  /* Validation is fail-closed and non-mutating. selector is word 2. */
  payload[8] = 1u;
  CHECK(!pal100_state_load(payload, sizeof(payload), 0));
  CHECK(memcmp(&s_timing, &expected_timing, sizeof(s_timing)) == 0);

  /* A checkpoint before the first title timing site must resume at PAL50,
   * then arm PAL100 exactly when that guarded site is reached again. */
  pal100_activate();
  CHECK(pal_state_save(payload, sizeof(payload)));
  last_crtc_multiplier = 99u;
  last_native_vblank_rate = 99u;
  CHECK(pal100_state_load(payload, sizeof(payload), 1));
  CHECK(s_pal_host_cadence_active == 0);
  CHECK(last_crtc_multiplier == 1u && last_native_vblank_rate == 50u);
  pal_enable_host_cadence();
  CHECK(last_crtc_multiplier == 2u && last_native_vblank_rate == 100u);
}

static void stock_word(uint32_t address, uint32_t value) {
  mem_w32(address, value);
}

static void test_exact_instruction_replacements(void) {
  CPUState c;
  uint32_t i;

  memset(ram, 0, sizeof(ram));
  reset_cpu(&c);
  pal100_activate();

  stock_word(0x800EE290u, 0x24040002u);
  invoke(&c, 0x800EE290u, 0x800EE294u);
  CHECK(c.gpr[4] == 1u);
  stock_word(0x800EEBF0u, 0x24030002u);
  invoke(&c, 0x800EEBF0u, 0x800EEBF4u);
  CHECK(c.gpr[3] == 1u);

  /* Every authenticated 0x3AF8 -> 0x56F4 load uses the native quotient. */
  s_timing.race_tick = 1u;
  for (i = 0; i < (uint32_t)(sizeof(s_half_phase_sites) /
                              sizeof(s_half_phase_sites[0]));
       ++i) {
    uint32_t expected = s_half_phase_sites[i].expected;
    uint32_t rt = (expected >> 16) & 31u;
    stock_word(s_half_phase_sites[i].address, expected);
    c.gpr[rt] = 0xA5A5A5A5u;
    CHECK(pal100_half_phase_site(&c, s_half_phase_sites[i].address) ==
          PSX_MOD_INSTRUCTION_SUBSTITUTE_LOAD);
    CHECK(c.gpr[rt] == 1u);
  }

  stock_word(0x800F1DA4u, 0x8E843AF8u);
  c.gpr[4] = 99u;
  invoke(&c, 0x800F1DA4u, 0x800F1DA8u);
  CHECK(c.gpr[4] == 0u);

  stock_word(0x800E39D4u, 0x00852023u);
  s_timing.work_gate = 1u;
  c.gpr[4] = 20u;
  c.gpr[5] = 3u;
  invoke(&c, 0x800E39D4u, 0x800E39D8u);
  CHECK(c.gpr[4] == 17u);
  s_timing.work_gate = 0u;
  invoke(&c, 0x800E39D4u, 0x800E39D8u);
  CHECK(c.gpr[4] == 17u);

  stock_word(0x800E3BB4u, 0x00024080u);
  stock_word(0x800E3BB8u, 0x01024021u);
  stock_word(0x800E3BBCu, 0x00084080u);
  stock_word(0x800E3BC0u, 0x01024023u);
  stock_word(0x800E3BC4u, 0x00084080u);
  stock_word(0x800E3BC8u, 0x01024021u);
  c.gpr[2] = 7u;
  invoke(&c, 0x800E3BB4u, 0x800E3BCCu);
  CHECK(c.gpr[8] == 511u);

  stock_word(0x800E484Cu, 0x0C041287u);
  stock_word(0x800E4850u, 0x00000000u);
  stock_word(0x800E4854u, 0x3042007Fu);
  stock_word(0x800E4858u, 0x24030004u);
  s_timing.frame_event = 1u;
  invoke(&c, 0x800E484Cu, 0x800E485Cu);
  CHECK(c.gpr[2] == 1u && c.gpr[3] == 1u);

  stock_word(0x8011A0BCu, 0x00441023u);
  c.gpr[2] = 1u;
  c.gpr[4] = 2u;
  invoke(&c, 0x8011A0BCu, 0x8011A0C0u);
  CHECK(c.gpr[2] == 1u);
  stock_word(0x8011A0C4u, 0x00431804u);
  c.gpr[3] = 0xFFFFFFF8u;
  invoke(&c, 0x8011A0C4u, 0x8011A0C8u);
  CHECK(c.gpr[3] == 0xFFFFFFFCu);
  stock_word(0x8011A12Cu, 0x00441023u);
  c.gpr[2] = 1u;
  invoke(&c, 0x8011A12Cu, 0x8011A130u);
  stock_word(0x8011A134u, 0x00431804u);
  invoke(&c, 0x8011A134u, 0x8011A138u);

  stock_word(0x8011A650u, 0x8E433AF8u);
  stock_word(0x8011A654u, 0x8E653AFCu);
  stock_word(0x8011A658u, 0x00031080u);
  stock_word(0x8011A65Cu, 0x00431021u);
  stock_word(0x8011A660u, 0x00021080u);
  stock_word(0x8011A664u, 0x00431023u);
  stock_word(0x8011A668u, 0x00852023u);
  stock_word(0x8011A66Cu, 0x96030134u);
  stock_word(0x8011A670u, 0x00821004u);
  stock_word(0x8011A674u, 0x00621823u);
  c.gpr[16] = 0x80020000u;
  mem_w16(c.gpr[16] + 0x134u, 100u);
  s_timing.work_gate = 1u;
  invoke(&c, 0x8011A650u, 0x8011A678u);
  CHECK(c.gpr[3] == 62u);
  s_timing.work_gate = 0u;
  invoke(&c, 0x8011A650u, 0x8011A700u);

  stock_word(0x8012BBF4u, 0x3C04801Du);
  s_timing.random_counter = 9u;
  s_timing.frame_event = 1u;
  CHECK(pal100_title_site(&c, 0x8012BBF4u) == 0u);
  CHECK(s_timing.random_counter == 0u && s_timing.frame_event == 0u);
  stock_word(0x800E99C4u, 0x240200C8u);
  stock_word(0x800E99C8u, 0x00031843u);
  stock_word(0x800E99CCu, 0x00431023u);
  c.gpr[3] = 73u;
  invoke(&c, 0x800E99C4u, 0x800E99D0u);
  CHECK(c.gpr[2] == 327u && c.gpr[3] == 73u);
  stock_word(0x800E99D8u, 0x24030002u);
  invoke(&c, 0x800E99D8u, 0x800E99DCu);
  CHECK(c.gpr[3] == 1u);
  stock_word(0x800E9A04u, 0x00052843u);
  c.gpr[5] = 0xFFFFFFF8u;
  invoke(&c, 0x800E9A04u, 0x800E9A08u);
  CHECK(c.gpr[5] == 0xFFFFFFFFu);

  stock_word(0x801234C0u, 0x28420002u);
  c.gpr[2] = 1u;
  invoke(&c, 0x801234C0u, 0x801234C4u);
  CHECK(c.gpr[2] == 0u);
  stock_word(0x801234C4u, 0x10400002u);
  stock_word(0x801234C8u, 0x24020002u);
  invoke(&c, 0x801234C4u, 0x801234D0u);
  CHECK(c.gpr[2] == 1u);
  stock_word(0x801234DCu, 0x28420007u);
  c.gpr[2] = 2u;
  invoke(&c, 0x801234DCu, 0x801234E0u);
  CHECK(c.gpr[2] == 1u);
  stock_word(0x801234E8u, 0x24020006u);
  invoke(&c, 0x801234E8u, 0x801234ECu);
  CHECK(c.gpr[2] == 2u);
  stock_word(0x801234F8u, 0x14620004u);
  stock_word(0x801234FCu, 0x24020002u);
  c.gpr[3] = 7u;
  c.gpr[2] = UINT32_MAX;
  invoke(&c, 0x801234F8u, 0x8012350Cu);
  CHECK(c.gpr[2] == 1u);

  stock_word(0x800E4640u, 0x000A51C3u);
  c.gpr[10] = 0xFFFFFF00u;
  invoke(&c, 0x800E4640u, 0x800E4644u);
  CHECK(c.gpr[10] == 0xFFFFFFFFu);
  stock_word(0x800E4650u, 0x000B59C3u);
  c.gpr[11] = 0x00000100u;
  invoke(&c, 0x800E4650u, 0x800E4654u);
  CHECK(c.gpr[11] == 1u);
  stock_word(0x800E4660u, 0x000529C3u);
  c.gpr[5] = 0x00000200u;
  invoke(&c, 0x800E4660u, 0x800E4664u);
  CHECK(c.gpr[5] == 2u);
  stock_word(0x801192E4u, 0x26830001u);
  stock_word(0x801192E8u, 0x00031C00u);
  stock_word(0x801192ECu, 0x86A473D0u);
  stock_word(0x801192F0u, 0x0003A403u);
  stock_word(0x801192F4u, 0x0284202Au);
  stock_word(0x801192F8u, 0x1480FFC5u);
  stock_word(0x801192FCu, 0xA6220146u);
  c.gpr[20] = 0u;
  c.gpr[2] = 7u;
  c.gpr[21] = 0x80190000u;
  c.gpr[17] = 0x801A0000u;
  mem_w16(c.gpr[21] + 0x73D0u, 2u);
  invoke(&c, 0x801192E4u, 0x80119210u);
  CHECK(c.gpr[20] == 1u);
  CHECK(c.gpr[2] == 14u);
  CHECK(c.gpr[4] == 1u);
  CHECK(mem_r16(c.gpr[17] + 0x146u) == 14u);

  /* The atomic owner also executes the comparison and branch delay store. */
  c.gpr[20] = 1u;
  c.gpr[2] = 9u;
  mem_w16(c.gpr[21] + 0x73D0u, 2u);
  invoke(&c, 0x801192E4u, 0x80119300u);
  CHECK(c.gpr[20] == 2u);
  CHECK(c.gpr[2] == 18u);
  CHECK(c.gpr[4] == 0u);
  CHECK(mem_r16(c.gpr[17] + 0x146u) == 18u);

  stock_word(0x8010D8E4u, 0x8C433AF8u);
  s_timing.race_tick = 1u;
  c.gpr[3] = 99u;
  CHECK(pal100_half_phase_site(&c, 0x8010D8E4u) ==
        PSX_MOD_INSTRUCTION_SUBSTITUTE_LOAD);
  CHECK(c.gpr[3] == 1u);

  /* A mismatched executable word fails closed to the original instruction. */
  stock_word(0x800EE290u, 0u);
  CHECK(pal100_title_site(&c, 0x800EE290u) == 0u);
}

static void test_fixed_selector_scheduler(void) {
  CPUState c;
  uint32_t i;

  memset(ram, 0, sizeof(ram));
  reset_cpu(&c);
  pal100_activate();
  CHECK(s_timing.rate == 1u);
  CHECK(s_timing.scale_k == 0x4000u);
  CHECK(s_timing.coefficient == 0x0862u);

  mem_w16(WIP3_RATE_WORD, 1u);
  mem_w32(WIP3_RANDOM_A_WORD, 135u);
  for (i = 0; i < 4u; ++i)
    scheduler_update(&c);
  CHECK(s_timing.scheduler_calls == 4u);
  CHECK(s_timing.logical_ticks == 1u);
  CHECK(s_timing.race_centiseconds == 4u);
  CHECK(s_timing.phase == 0u && s_timing.phase_wrap == 1u &&
        s_timing.work_gate == 1u);
  CHECK(s_timing.coefficient == 0x0862u);
  CHECK(s_timing.frame_event == 1u);

  mem_w16(WIP3_RATE_WORD, 2u);
  scheduler_update(&c);
  CHECK(s_timing.rate == 2u && s_timing.scale_k == 0x8000u);
  CHECK(s_timing.coefficient == 0x081Eu);
  CHECK(s_timing.logical_ticks == 1u);
  CHECK(s_timing.race_tick == 1u);
  scheduler_update(&c);
  CHECK(s_timing.logical_ticks == 2u);
  CHECK(s_timing.race_centiseconds == 8u);
}

static void test_selector_phase_sequences(void) {
  static const uint32_t phase25[] = {0u, 0u, 0u, 0u};
  static const uint32_t wrap25[] = {1u, 1u, 1u, 1u};
  static const uint32_t work25[] = {1u, 1u, 1u, 1u};
  static const uint32_t random25[] = {1u, 2u, 3u, 4u};
  static const uint32_t event25[] = {1u, 0u, 0u, 0u};
  static const uint32_t phase50[] = {1u, 0u, 1u, 0u};
  static const uint32_t wrap50[] = {0u, 1u, 0u, 1u};
  static const uint32_t work50[] = {1u, 0u, 1u, 0u};
  static const uint32_t random50[] = {0u, 1u, 1u, 2u};
  static const uint32_t event50[] = {0u, 1u, 0u, 0u};
  static const uint32_t phase100[] = {1u, 2u, 3u, 0u};
  static const uint32_t wrap100[] = {0u, 0u, 0u, 1u};
  static const uint32_t work100[] = {0u, 0u, 0u, 1u};
  static const uint32_t random100[] = {0u, 0u, 0u, 1u};
  static const uint32_t event100[] = {0u, 0u, 0u, 1u};
  static const struct {
    uint32_t selector;
    uint32_t rate;
    const uint32_t *phase;
    const uint32_t *wrap;
    const uint32_t *work;
    const uint32_t *random_counter;
    const uint32_t *event;
  } cases[] = {
      {0u, 2u, phase25, wrap25, work25, random25, event25},
      {1u, 1u, phase50, wrap50, work50, random50, event50},
      {2u, 1u, phase100, wrap100, work100, random100, event100},
  };
  CPUState c;
  uint32_t case_index;
  uint32_t i;

  for (case_index = 0u;
       case_index < (uint32_t)(sizeof(cases) / sizeof(cases[0]));
       ++case_index) {
    memset(ram, 0, sizeof(ram));
    reset_cpu(&c);
    pal_activate(cases[case_index].selector);
    mem_w16(WIP3_RATE_WORD, (uint16_t)cases[case_index].rate);
    /* counter 1 plus 135 produces Hue's event value 4 after xor/mask. */
    mem_w32(WIP3_RANDOM_A_WORD, 135u);
    for (i = 0u; i < 4u; ++i) {
      scheduler_update(&c);
      CHECK(s_timing.phase == cases[case_index].phase[i]);
      CHECK(s_timing.phase_wrap == cases[case_index].wrap[i]);
      CHECK(s_timing.work_gate == cases[case_index].work[i]);
      CHECK(s_timing.random_counter ==
            cases[case_index].random_counter[i]);
      CHECK(s_timing.frame_event == cases[case_index].event[i]);
    }
  }
}

static void test_race_clock_window(void) {
  CPUState c;
  uint32_t i;

  memset(ram, 0, sizeof(ram));
  reset_cpu(&c);
  pal100_activate();
  mem_w16(WIP3_RATE_WORD, 1u);
  for (i = 0; i < 100u; ++i)
    scheduler_update(&c);
  CHECK(s_timing.scheduler_calls == 100u);
  CHECK(s_timing.logical_ticks == 25u);
  CHECK(s_timing.race_centiseconds == 100u);
  CHECK(s_timing.race_phase == 0u);
}

static void test_selector_mode(uint32_t selector, uint32_t calls,
                               uint32_t rate, uint32_t p,
                               uint32_t skip, uint32_t scale_k,
                               uint32_t coefficient) {
  CPUState c;
  uint32_t i;
  uint32_t half_site = s_half_phase_sites[0].address;
  uint32_t half_expected = s_half_phase_sites[0].expected;

  memset(ram, 0, sizeof(ram));
  reset_cpu(&c);
  s_timing.phase = 3u;
  s_timing.race_phase = 1u;
  s_timing.random_counter = 7u;
  s_timing.frame_event = 1u;
  s_high_rate[3].x = 0x12345678u;
  host_cadence_calls = 0u;
  last_crtc_multiplier = 0u;
  last_native_vblank_rate = 0u;
  pal_activate(selector);

  CHECK(s_timing.selector == selector);
  CHECK(s_timing.p == p);
  CHECK(s_timing.sim_vblank_skip == skip);
  CHECK(s_timing.rate == rate);
  CHECK(s_timing.scale_k == scale_k);
  CHECK(s_timing.coefficient == coefficient);
  CHECK(s_timing.phase == 0u && s_timing.phase_wrap == 0u &&
        s_timing.work_gate == 0u);
  CHECK(s_timing.race_phase == 0u && s_timing.race_tick == 0u);
  CHECK(s_timing.random_counter == 0u && s_timing.frame_event == 0u);
  CHECK(s_high_rate[3].x == 0u && s_high_rate[3].y == 0u &&
        s_high_rate[3].packed == 0u);

  stock_word(0x800EE290u, 0x24040002u);
  invoke(&c, 0x800EE290u, 0x800EE294u);
  CHECK(c.gpr[4] == skip);
  CHECK(last_crtc_multiplier == (p ? 2u : 1u));
  CHECK(last_native_vblank_rate == (p ? 100u : 50u));
  CHECK(host_cadence_calls == 2u);
  invoke(&c, 0x800EE290u, 0x800EE294u);
  CHECK(host_cadence_calls == 2u);

  mem_w16(WIP3_RATE_WORD, (uint16_t)rate);
  for (i = 0; i < calls; ++i)
    scheduler_update(&c);
  CHECK(s_timing.scheduler_calls == calls);
  CHECK(s_timing.logical_ticks == 25u);
  CHECK(s_timing.race_centiseconds == 100u);
  CHECK(mem_r32(WIP3_RATE_MODE_WORD) == p + 1u);
  CHECK(s_timing.phase == 0u);
  CHECK(s_timing.race_phase == 0u);

  stock_word(half_site, half_expected);
  s_timing.race_tick = 0u;
  c.gpr[(half_expected >> 16) & 31u] = 99u;
  CHECK(pal100_half_phase_site(&c, half_site) ==
        PSX_MOD_INSTRUCTION_SUBSTITUTE_LOAD);
  CHECK(c.gpr[(half_expected >> 16) & 31u] == (p ? 0u : rate));

  stock_word(0x800F1DA4u, 0x8E843AF8u);
  c.gpr[4] = 99u;
  invoke(&c, 0x800F1DA4u, 0x800F1DA8u);
  CHECK(c.gpr[4] == (p ? 0u : rate));

  CHECK(loop_scale(8u) == (p ? (2u * rate) : 8u));
}

static void test_all_selector_cadences(void) {
  CPUState c;
  uint32_t base = 0x80020000u;

  test_selector_mode(0u, 25u, 2u, 0u, 2u, 0x10000u, 0x1000u);
  test_selector_mode(1u, 50u, 1u, 0u, 1u, 0x8000u, 0x1111u);
  test_selector_mode(2u, 100u, 1u, 1u, 1u, 0x4000u, 0x0862u);

  /* p=0 executes the exact stock shared-owner loads, never the 100 Hz mixer. */
  memset(ram, 0, sizeof(ram));
  reset_cpu(&c);
  pal50_activate();
  c.gpr[19] = base;
  mem_w32(base + 0x3AFCu, 0x12345678u);
  stock_word(0x800F15DCu, 0x8E633AFCu);
  invoke(&c, 0x800F15DCu, 0x800F15E0u);
  CHECK(c.gpr[3] == 0x12345678u);
  c.gpr[16] = base;
  mem_w16(base + 0x128u, 0xBEEFu);
  stock_word(0x800F1D18u, 0x96030128u);
  invoke(&c, 0x800F1D18u, 0x800F1D1Cu);
  CHECK(c.gpr[3] == 0xBEEFu);
  CHECK(s_high_rate[0].x == 0u && s_high_rate[0].y == 0u &&
        s_high_rate[0].packed == 0u);

  /* Selector-derived motion and special formulas differ only through p. */
  pal50_activate();
  stock_word(0x800E4640u, 0x000A51C3u);
  c.gpr[10] = 0x100u;
  invoke(&c, 0x800E4640u, 0x800E4644u);
  CHECK(c.gpr[10] == 2u);
  s_timing.rate = 1u;
  s_timing.work_gate = 1u;
  c.gpr[2] = 0x1000u;
  c.gpr[3] = 10u;
  invoke(&c, 0x800E37F4u, 0x800E37FCu);
  CHECK(c.gpr[3] == 9u); /* special_shift = 12 */
  stock_word(0x800E3BB4u, 0x00024080u);
  stock_word(0x800E3BB8u, 0x01024021u);
  stock_word(0x800E3BBCu, 0x00084080u);
  stock_word(0x800E3BC0u, 0x01024023u);
  stock_word(0x800E3BC4u, 0x00084080u);
  stock_word(0x800E3BC8u, 0x01024021u);
  c.gpr[2] = 7u;
  invoke(&c, 0x800E3BB4u, 0x800E3BCCu);
  CHECK(c.gpr[8] == 539u); /* special_mul = 77 */
  stock_word(0x801192E4u, 0x26830001u);
  stock_word(0x801192E8u, 0x00031C00u);
  stock_word(0x801192ECu, 0x86A473D0u);
  stock_word(0x801192F0u, 0x0003A403u);
  stock_word(0x801192F4u, 0x0284202Au);
  stock_word(0x801192F8u, 0x1480FFC5u);
  stock_word(0x801192FCu, 0xA6220146u);
  c.gpr[20] = 0u;
  c.gpr[2] = 7u;
  c.gpr[21] = 0x80030000u;
  c.gpr[17] = 0x80040000u;
  mem_w16(c.gpr[21] + 0x73D0u, 2u);
  invoke(&c, 0x801192E4u, 0x80119210u);
  CHECK(c.gpr[2] == 7u); /* iteration shift by p=0 is identity */
  CHECK(mem_r16(c.gpr[17] + 0x146u) == 7u);

  pal100_activate();
  c.gpr[10] = 0x100u;
  invoke(&c, 0x800E4640u, 0x800E4644u);
  CHECK(c.gpr[10] == 1u);
  s_timing.rate = 1u;
  s_timing.work_gate = 1u;
  c.gpr[2] = 0x400u;
  c.gpr[3] = 10u;
  invoke(&c, 0x800E37F4u, 0x800E37FCu);
  CHECK(c.gpr[3] == 9u); /* special_shift = 10 */
}

static void test_all_sites(void) {
  CPUState c;
  uint32_t base = 0x80020000u;

  memset(ram, 0, sizeof(ram));
  reset_cpu(&c);
  pal100_activate();

  /* Scheduler and both C-owned high-rate records. */
  mem_w16(WIP3_RATE_WORD, 1u);
  mem_w32(WIP3_FRAME_EVENT_WORD, 7u);
  invoke(&c, 0x801234F0u, 0x801234F8u);
  CHECK(c.gpr[3] == 7u && c.gpr[2] == UINT32_MAX);
  CHECK(s_timing.rate == 1u && s_timing.scale_k == 0x4000u);
  CHECK(s_timing.coefficient == 0x0862u);

  c.gpr[16] = base;
  c.gpr[10] = 0x01000000u;
  mem_w8(base + 0x15Eu, 1u);
  mem_w16(base + 0x128u, 1u);
  mem_w16(base + 0x122u, 2u);
  mem_w16(base + 0x124u, 3u);
  invoke(&c, 0x800F15DCu, 0x800F1D5Cu);
  CHECK(c.gpr[12] == 0u);
  mem_w8(base + 0x15Eu, 2u);
  mem_w16(base + 0x128u, 4u);
  invoke(&c, 0x800F1D18u, 0x800F1D5Cu);
  CHECK(c.gpr[12] == 1u);

  s_timing.work_gate = 1u;
  s_timing.rate = 1u;
  c.gpr[12] = base;
  mem_w32(base + 4u, 0x44332211u);
  invoke(&c, 0x800E2D40u, 0x800E2D48u);
  CHECK(c.gpr[10] == 0x44332211u);
  c.gpr[3] = base;
  c.gpr[4] = 0x20u;
  c.gpr[5] = 9u;
  mem_w16(base + 0x20u, 5u);
  invoke(&c, 0x800E2F38u, 0x800E2FE8u);
  CHECK(c.gpr[2] == 14u);
  c.gpr[16] = base;
  c.gpr[5] = 8u;
  c.gpr[2] = 3u;
  invoke(&c, 0x800E2FBCu, 0x800E2FECu);
  CHECK(mem_r16(base + 0x132u) == 11u);
  c.gpr[3] = 4u;
  c.gpr[2] = 5u;
  invoke(&c, 0x800E2FE4u, 0x800E2FECu);
  CHECK(mem_r16(base + 0x132u) == 9u);

  c.gpr[2] = 0x4000u;
  c.gpr[3] = 100u;
  invoke(&c, 0x800E37F4u, 0x800E37FCu);
  CHECK(c.gpr[3] == 84u);
  c.gpr[2] = 0x2000u;
  c.gpr[3] = 50u;
  invoke(&c, 0x800E3860u, 0x800E3868u);
  CHECK(c.gpr[3] == 42u);
  c.gpr[16] = base;
  c.gpr[3] = 20u;
  invoke(&c, 0x800E4580u, 0x800E458Cu);
  CHECK(mem_r16(base + 0x154u) == 19u);

  c.gpr[5] = base;
  mem_w32(base, 0xABCDEF01u);
  invoke(&c, 0x8011C2DCu, 0x8011C2E0u);
  CHECK(c.gpr[2] == 0xABCDEF01u);
  c.gpr[29] = 0x80030000u;
  c.gpr[5] = 2u;
  c.gpr[20] = 0x55u;
  mem_w32(0x801E9DA4u + 8u, 1u);
  invoke(&c, 0x8011DD7Cu, 0x8011DD84u);
  CHECK(mem_r32(c.gpr[29] + 0x28u) == 0x55u);
  c.gpr[5] = base;
  c.gpr[2] = 10u;
  c.gpr[3] = 7u;
  invoke(&c, 0x80124AE8u, 0x80124AF0u);
  CHECK(mem_r32(base + 0x5EC0u) == 17u);
  s_timing.work_gate = 0u;
  c.gpr[2] = 12u;
  c.gpr[3] = 7u;
  invoke(&c, 0x80124AE8u, 0x80124AF0u);
  CHECK(mem_r32(base + 0x5EC0u) == 12u);
  s_timing.work_gate = 1u;
  c.gpr[31] = 0x81234560u;
  mem_w32(0x801E1B20u, 0x77u);
  invoke(&c, 0x801396CCu, 0x801396D4u);
  CHECK(c.gpr[3] == 0x77u);
  c.gpr[17] = base;
  c.gpr[2] = 9u;
  invoke(&c, 0x8013BEF0u, 0x8013BEF8u);
  CHECK(mem_r8(base + 0x2Du) == 8u);
  s_timing.work_gate = 0u;
  c.gpr[2] = 11u;
  invoke(&c, 0x8013BEF0u, 0x8013BEF8u);
  CHECK(mem_r8(base + 0x2Du) == 11u);
  s_timing.work_gate = 1u;

  /* Coefficient sites. */
  s_timing.coefficient = 0x0862u;
  c.gpr[2] = 0x10000u;
  c.gpr[6] = 100u;
  invoke(&c, 0x800E45FCu, 0x800E4604u);
  c.gpr[3] = 0x20000u;
  c.gpr[8] = 200u;
  invoke(&c, 0x800E460Cu, 0x800E4614u);
  c.gpr[9] = 0x30000u;
  c.gpr[7] = 300u;
  invoke(&c, 0x800E4628u, 0x800E4630u);

  /* Both authenticated random-helper replacements. */
  mem_w32(WIP3_RANDOM_A_WORD, 0x10u);
  s_timing.random_counter = 3u;
  invoke(&c, 0x800E4880u, 0x800E4888u);
  CHECK(c.gpr[2] == 0x5591u);
  invoke(&c, 0x800E4894u, 0x800E489Cu);
  CHECK(c.gpr[2] == 0x5591u);

  /* Rounded K-scale sites, each with its own memory-delay fixture. */
  s_timing.scale_k = 0x4000u;
  c.gpr[16] = base;
  c.gpr[3] = 0x40000u;
  c.gpr[2] = 1u;
  invoke(&c, 0x8013A600u, 0x8013A608u);
  CHECK(mem_r32(base + 0x24u) == c.gpr[2]);
  c.gpr[17] = base;
  c.gpr[2] = 2u;
  mem_w32(base + 0x10u, 0x40000u);
  mem_w32(base + 8u, 7u);
  invoke(&c, 0x8013BED0u, 0x8013BED8u);
  CHECK(c.gpr[3] == 7u);
  c.gpr[5] = 0x40000u;
  c.gpr[4] = 3u;
  invoke(&c, 0x8013BED8u, 0x8013BEE0u);
  CHECK(mem_r32(base + 4u) == c.gpr[4]);
  mem_w32(base + 0x18u, 0x40000u);
  c.gpr[3] = 4u;
  c.gpr[2] = 6u;
  mem_w8(base + 0x2Du, 9u);
  invoke(&c, 0x8013BEE0u, 0x8013BEF0u);
  CHECK(c.gpr[2] == 9u);
  c.gpr[4] = base;
  c.gpr[2] = 10u;
  invoke(&c, 0x80154088u, 0x80154090u);
  CHECK(mem_r16(base + 6u) == 42u);
  c.gpr[18] = base;
  c.gpr[5] = 0x40000u;
  c.gpr[3] = 0x20000u;
  mem_w32(base + 0x10u, 0x11u);
  invoke(&c, 0x80159070u, 0x80159078u);
  CHECK(c.gpr[2] == 0x11u);
  c.gpr[3] = 0x40000u;
  c.gpr[10] = 1u;
  invoke(&c, 0x80159078u, 0x80159080u);
  CHECK(mem_r32(base + 0x1Cu) == 0x40000u);
  c.gpr[5] = 0x40000u;
  c.gpr[4] = 0x20000u;
  mem_w32(base + 0x24u, 0x22u);
  invoke(&c, 0x8015908Cu, 0x80159094u);
  CHECK(c.gpr[2] == 0x22u);
  c.gpr[4] = 0x40000u;
  c.gpr[8] = 1u;
  invoke(&c, 0x80159094u, 0x8015909Cu);
  CHECK(mem_r32(base + 0x20u) == 0x40000u);
  c.gpr[2] = 0x40000u;
  invoke(&c, 0x801590A4u, 0x801590ACu);
  c.gpr[9] = 1u;
  invoke(&c, 0x801590ACu, 0x801590B4u);
  CHECK(mem_r32(base + 0x24u) == c.gpr[2]);

  /* All four rate-loop paths, including signed vs unsigned halfword loads. */
  s_timing.rate = 1u;
  c.gpr[16] = base;
  mem_w16(base + 0x13Cu, 0xFFF0u);
  c.gpr[3] = 2u;
  invoke(&c, 0x80119FC4u, 0x80119FD4u);
  CHECK(c.gpr[2] == 1u && c.gpr[3] == 0xFFFFFFFCu);
  invoke(&c, 0x8011A010u, 0x8011A018u);
  CHECK(c.gpr[2] == 0xFFFFFFFCu);
  c.gpr[2] = 8u;
  c.gpr[3] = 20u;
  invoke(&c, 0x8011A040u, 0x8011A058u);
  CHECK(c.gpr[2] == 18u);
  invoke(&c, 0x8011A048u, 0x8011A050u);
  CHECK(c.gpr[2] == 0x00003FFCu);
}

int main(void) {
  test_arithmetic();
  test_native_state_roundtrip();
  test_exact_instruction_replacements();
  test_fixed_selector_scheduler();
  test_selector_phase_sequences();
  test_race_clock_window();
  test_all_selector_cadences();
  test_all_sites();
  printf("PAL100 semantic fixtures: PASS (%u checks, 100 hook sites)\n",
         checks);
  return 0;
}
