#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <string.h>

/*
 * Direct Hueponik PAL 25/50/100 selector for the authenticated stock
 * SCES-02845 executable.
 *
 * This semantic title mod owns named scheduler state and replaces only stock
 * instruction sites which consume it. It does not install Hueponik's code
 * island/high-RAM engine and does not write gSimVblankSkip (0x8017F02C) or
 * stock padding at 0x80175064 (repurposed only by the patched executable).
 * All title edits are instruction-site callbacks against stock executable RAM.
 * Keeping the guest text image stock is required for AOT eligibility: generated
 * substitutions paired with stock runtime RAM arm the dirty-code interpreter.
 */

enum {
  WIP3_RATE_WORD = 0x80173AF8u,
  WIP3_RATE_MODE_WORD = 0x80173AFCu,
  WIP3_FRAME_EVENT_WORD = 0x80173AE8u,
  WIP3_RANDOM_A_WORD = 0x80173A8Cu,
  WIP3_SHARED_RECORD_COUNT = 256u
};

typedef struct Wip3PAL100State {
  uint32_t selector;
  uint32_t p;
  uint32_t sim_vblank_skip;
  uint32_t rate;
  uint32_t scale_k;
  uint32_t coefficient;
  uint32_t phase;
  uint32_t phase_wrap;
  uint32_t work_gate;
  uint32_t random_counter;
  uint32_t race_phase;
  uint32_t race_tick;
  uint32_t race_centiseconds;
  uint32_t frame_event;
  uint32_t scheduler_calls;
  uint32_t logical_ticks;
} Wip3PAL100State;

typedef struct Wip3HighRateRecord {
  uint32_t x;
  uint32_t y;
  uint32_t packed;
} Wip3HighRateRecord;

typedef char Wip3PALStateWireFieldCount[
    sizeof(Wip3PAL100State) == 16u * sizeof(uint32_t) ? 1 : -1];
typedef char Wip3PALRecordWireFieldCount[
    sizeof(Wip3HighRateRecord) == 3u * sizeof(uint32_t) ? 1 : -1];

static int s_pal_active;
static int s_pal_host_cadence_active;
static Wip3PAL100State s_timing;
static Wip3HighRateRecord s_high_rate[WIP3_SHARED_RECORD_COUNT];

static void pal_enable_host_cadence(void) {
  if (s_pal_host_cadence_active)
    return;
  s_pal_host_cadence_active = 1;
  (void)psx_mod_set_crtc_refresh_multiplier(s_timing.p ? 2u : 1u);
  (void)psx_mod_set_native_vblank_rate(s_timing.p ? 100u : 50u);
}

typedef struct Wip3HalfPhaseSite {
  uint32_t address;
  uint32_t expected;
} Wip3HalfPhaseSite;

/*
 * Complete authenticated stock-rate -> Hue private half-phase load surface.
 * The patched executable changes each same-register load from 0x3AF8 to
 * 0x56F4. Native state owns the quotient, so guest RAM stays stock/AOT-safe.
 */
static const Wip3HalfPhaseSite s_half_phase_sites[] = {
    {0x800E2F0Cu, 0x94623AF8u}, {0x800E4718u, 0x8C823AF8u},
    {0x800E4760u, 0x94A33AF8u}, {0x800E5110u, 0x8CA63AF8u},
    {0x800E73DCu, 0x8C623AF8u}, {0x800EFA30u, 0x8D033AF8u},
    {0x800F0078u, 0x8C623AF8u}, {0x800F01B0u, 0x94853AF8u},
    {0x8010D8E4u, 0x8C433AF8u}, {0x8011A3A8u, 0x8E423AF8u},
    {0x8011A984u, 0x8C623AF8u}, {0x801280ACu, 0x94853AF8u},
    {0x80128390u, 0x94833AF8u}, {0x8014CD44u, 0x94453AF8u},
    {0x8014EA2Cu, 0x94833AF8u}, {0x801505BCu, 0x94623AF8u},
    {0x80150950u, 0x8CC33AF8u}, {0x80151568u, 0x94833AF8u},
    {0x80151C44u, 0x94623AF8u}, {0x80151C8Cu, 0x94453AF8u},
    {0x8015242Cu, 0x94443AF8u}, {0x801531D0u, 0x94A33AF8u},
    {0x801531F4u, 0x8CA23AF8u}, {0x80153960u, 0x94A33AF8u},
    {0x80153984u, 0x8CA23AF8u}, {0x80153E34u, 0x8FC33AF8u},
    {0x80153E98u, 0x8FC33AF8u}, {0x80153F38u, 0x8FC33AF8u},
    {0x80153F90u, 0x8FC33AF8u}, {0x8015404Cu, 0x8C833AF8u},
    {0x80154A58u, 0x94623AF8u}, {0x80154E64u, 0x94A23AF8u},
    {0x801587C8u, 0x94C23AF8u}, {0x801589E0u, 0x94C23AF8u},
    {0x80158CDCu, 0x94C23AF8u}, {0x80159224u, 0x94443AF8u},
    {0x80159B3Cu, 0x94623AF8u}, {0x8015AA88u, 0x96C33AF8u},
    {0x8015BB74u, 0x94A33AF8u}, {0x8015BB98u, 0x8CA23AF8u},
};

static uint32_t rd32(CPUState *cpu, uint32_t address) {
  return cpu->read_word(address);
}
static uint16_t rd16(CPUState *cpu, uint32_t address) {
  return cpu->read_half(address);
}
static int16_t rds16(CPUState *cpu, uint32_t address) {
  return (int16_t)cpu->read_half(address);
}
static uint8_t rd8(CPUState *cpu, uint32_t address) {
  return cpu->read_byte(address);
}
static void wr32(CPUState *cpu, uint32_t address, uint32_t value) {
  cpu->write_word(address, value);
}
static void wr16(CPUState *cpu, uint32_t address, uint16_t value) {
  cpu->write_half(address, value);
}
static void wr8(CPUState *cpu, uint32_t address, uint8_t value) {
  cpu->write_byte(address, value);
}

/* MIPS MULT/MFLO then signed shift: the shift applies to the low 32 bits. */
static uint32_t mul_low_sra(uint32_t value, uint32_t multiplier,
                            uint32_t shift) {
  uint32_t low =
      (uint32_t)((int64_t)(int32_t)value * (int64_t)(int32_t)multiplier);
  return (uint32_t)((int32_t)low >> shift);
}

static uint32_t rate_scale(uint32_t value) {
  uint32_t low =
      (uint32_t)((int64_t)(int32_t)value * (int64_t)(int32_t)s_timing.scale_k);
  low += 0x8000u;
  return (uint32_t)((int32_t)low >> 16);
}

static uint32_t coefficient_scale(uint32_t value) {
  return mul_low_sra(value, s_timing.coefficient, 16u);
}

static uint32_t loop_scale(uint32_t value) {
  uint32_t multiplier = s_timing.p ? s_timing.rate : 4u;
  return mul_low_sra(value, multiplier, 2u);
}

/* Exact signed MULT/HI/LO recombination from the two shared mixer blocks. */
static uint32_t mul_shift16_64(uint32_t value, uint32_t multiplier) {
  int64_t product = (int64_t)(int32_t)value * (int64_t)(int32_t)multiplier;
  return (uint32_t)(product >> 16);
}

static uint32_t mix_x(uint32_t x, int32_t input) {
  return mul_shift16_64(x, 0xD745u) + (uint32_t)(input * 0x0146);
}

static uint32_t mix_y(uint32_t y, uint32_t x, int32_t input) {
  return mul_shift16_64(y, 0xF798u) + mul_shift16_64(x, 0x2CD9u) +
         (uint32_t)(input * 0x0070);
}

/* func_8014B6A4(value, 16): signed division by 65536, toward zero. */
static uint32_t fixed_to_half(uint32_t value) {
  uint32_t magnitude;
  int32_t shifted;
  if ((int32_t)value > 0)
    return (uint32_t)((int32_t)value >> 16);
  magnitude = 0u - value;
  shifted = (int32_t)magnitude >> 16;
  return 0u - (uint32_t)shifted;
}

static uint32_t load_unaligned_word(CPUState *cpu, uint32_t address) {
  return (uint32_t)rd8(cpu, address) | ((uint32_t)rd8(cpu, address + 1u) << 8) |
         ((uint32_t)rd8(cpu, address + 2u) << 16) |
         ((uint32_t)rd8(cpu, address + 3u) << 24);
}

static void scheduler_update(CPUState *cpu) {
  static const uint32_t coefficients[4] = {0x1111u, 0x1000u, 0x0862u,
                                           0x081Eu};
  uint32_t rate = rd16(cpu, WIP3_RATE_WORD);
  uint32_t phase_mod = 2u << s_timing.p;
  uint32_t next_phase;
  uint32_t next_race_phase;

  /* The guarded stock owner clamps this field to 1..2 before this hook. */
  if (rate < 1u)
    rate = 1u;
  else if (rate > 2u)
    rate = 2u;

  s_timing.rate = rate;
  s_timing.scale_k = (0x8000u >> s_timing.p) << (rate - 1u);

  /*
   * The authenticated island decrements the raw rate before indexing the
   * four-word table: (2 * p) + (rate - 1). Selectors 25/50 share p=0;
   * selector 100 uses p=1.
   */
  s_timing.coefficient = coefficients[2u * s_timing.p + (rate - 1u)];

  /*
   * Hueponik's private 0x56F0/0x56F4 pair is a separate divide-by-two
   * accumulator used by the race-clock delta publisher at 0x8010D8E4.
   * Fixed p=1/rate=1 yields quotient 0,1,0,1...; the stock following shift
   * turns that into 0,2,0,2... centiseconds, exactly 100 cs per 100 VBlanks.
   */
  if (s_timing.p == 0u) {
    s_timing.race_phase = 0u;
    s_timing.race_tick = rate;
  } else {
    next_race_phase = s_timing.race_phase + rate;
    s_timing.race_tick = next_race_phase >> 1;
    s_timing.race_phase = next_race_phase & 1u;
  }
  s_timing.race_centiseconds += s_timing.race_tick << 1;

  next_phase = (s_timing.phase & (phase_mod - 1u)) + rate;
  s_timing.phase_wrap = next_phase >= phase_mod;
  s_timing.phase = next_phase & (phase_mod - 1u);
  if (s_timing.p != 0u)
    s_timing.work_gate = s_timing.phase_wrap;
  else if (rate < 2u)
    s_timing.work_gate = !s_timing.phase_wrap;
  else
    s_timing.work_gate = 1u;
  /* Hue's raw-wrap branch updates the private random counter/event before
   * selector-specific publication transforms that wrap into work_gate. */
  if (s_timing.phase_wrap)
    ++s_timing.random_counter;
  if (s_timing.phase_wrap) {
    uint32_t random_value =
        s_timing.random_counter + rd32(cpu, WIP3_RANDOM_A_WORD);
    uint32_t random7 = ((random_value >> 1) ^ (random_value << 3)) & 0x7Fu;
    s_timing.frame_event = random7 == 4u;
  } else {
    s_timing.frame_event = 0u;
  }
  ++s_timing.scheduler_calls;
  s_timing.logical_ticks += s_timing.work_gate;

  /* 0x80173AFC is stock scheduler state, not replacement-engine RAM. */
  wr32(cpu, WIP3_RATE_MODE_WORD, s_timing.p + 1u);
}

static uint32_t shared_high_rate(CPUState *cpu, uint32_t address) {
  uint32_t s0 = cpu->gpr[16];
  uint32_t index = rd8(cpu, s0 + 0x15Eu);
  uint32_t packed = (uint32_t)rd16(cpu, s0 + 0x128u) |
                    ((uint32_t)rd16(cpu, s0 + 0x122u) << 16);
  uint32_t marker = (address == 0x800F1D18u) ? 1u : 0u;
  int32_t input = rds16(cpu, s0 + 0x124u);
  Wip3HighRateRecord *record = &s_high_rate[index];
  uint32_t i;

  cpu->gpr[12] = marker; /* patched jump delay slot: t4 = 0 / 1 */

  if (packed != record->packed) {
    record->x = cpu->gpr[10]; /* t2 at both bounded owner sites */
    record->y = marker;       /* patched delay slot's t4 = 0 / 1 */
    record->packed = packed;
  }

  for (i = 0; i < s_timing.rate; ++i) {
    record->x = mix_x(record->x, input);
    record->y = mix_y(record->y, record->x, input);
  }

  wr16(cpu, s0 + 0x128u, (uint16_t)fixed_to_half(record->x));
  wr16(cpu, s0 + 0x122u, (uint16_t)fixed_to_half(record->y));
  record->packed = (uint32_t)rd16(cpu, s0 + 0x128u) |
                   ((uint32_t)rd16(cpu, s0 + 0x122u) << 16);
  return 0x800F1D5Cu;
}

static uint32_t pal100_half_phase_site(CPUState *cpu, uint32_t address) {
  uint32_t lo = 0u;
  uint32_t hi =
      (uint32_t)(sizeof(s_half_phase_sites) / sizeof(s_half_phase_sites[0]));

  if (!s_pal_active)
    return 0u;
  pal_enable_host_cadence();

  /* The table is address-sorted. Keep this callback bounded because several
   * of these loads execute in per-object hot loops at PAL100. */
  while (lo < hi) {
    uint32_t mid = lo + ((hi - lo) >> 1);
    if (s_half_phase_sites[mid].address < address)
      lo = mid + 1u;
    else
      hi = mid;
  }
  if (lo >= (uint32_t)(sizeof(s_half_phase_sites) /
                        sizeof(s_half_phase_sites[0])) ||
      s_half_phase_sites[lo].address != address ||
      rd32(cpu, address) != s_half_phase_sites[lo].expected)
    return 0u;

  cpu->gpr[(s_half_phase_sites[lo].expected >> 16) & 31u] =
      s_timing.p ? s_timing.race_tick : s_timing.rate;
  return PSX_MOD_INSTRUCTION_SUBSTITUTE_LOAD;
}

static uint32_t pal100_title_site(CPUState *cpu, uint32_t address) {
  uint32_t value;

  if (!s_pal_active)
    return 0u;
  /* Hueponik does not alter BIOS/boot-media cadence before its title code is
   * reached. Keep that boundary: boot runs at PAL50, then the first guarded
   * Hue timing site atomically enables the selected host/CRTC pair. */
  pal_enable_host_cadence();

  switch (address) {
  /*
   * Exact selector-graph instruction replacements. Guard the stock guest
   * word at the callback boundary so a different executable executes its own
   * instruction instead of receiving a silent semantic patch.
   */
  case 0x800EE290u:
    if (rd32(cpu, address) != 0x24040002u)
      return 0u;
    cpu->gpr[4] = s_timing.sim_vblank_skip;
    return address + 4u;
  case 0x800EEBF0u:
    if (rd32(cpu, address) != 0x24030002u)
      return 0u;
    cpu->gpr[3] = s_timing.sim_vblank_skip;
    return address + 4u;
  case 0x800E99C4u:
    /* Own the patched constant/nop/subtract sequence atomically. Returning
     * from separate C4/C8 callbacks split the AOT block immediately after the
     * preceding timed load and produced a hybrid camera-easing value. */
    if (rd32(cpu, address) != 0x240200C8u ||
        rd32(cpu, address + 4u) != 0x00031843u ||
        rd32(cpu, address + 8u) != 0x00431023u)
      return 0u;
    cpu->gpr[2] = 400u - cpu->gpr[3];
    return 0x800E99D0u;
  case 0x800E99D8u:
    if (rd32(cpu, address) != 0x24030002u)
      return 0u;
    cpu->gpr[3] = 1u;
    return address + 4u;
  case 0x800E9A04u:
    if (rd32(cpu, address) != 0x00052843u)
      return 0u;
    cpu->gpr[5] = (uint32_t)((int32_t)cpu->gpr[5] >> 3);
    return address + 4u;
  case 0x801234C0u:
    if (rd32(cpu, address) != 0x28420002u)
      return 0u;
    cpu->gpr[2] = ((int32_t)cpu->gpr[2] < 1) ? 1u : 0u;
    return address + 4u;
  case 0x801234C4u: {
    uint32_t take;
    if (rd32(cpu, address) != 0x10400002u ||
        rd32(cpu, address + 4u) != 0x24020002u)
      return 0u;
    take = cpu->gpr[2] == 0u;
    cpu->gpr[2] = 1u; /* patched branch delay slot at 0x801234C8 */
    return take ? 0x801234D0u : 0x801234CCu;
  }
  case 0x801234DCu:
    if (rd32(cpu, address) != 0x28420007u)
      return 0u;
    cpu->gpr[2] = ((int32_t)cpu->gpr[2] < 3) ? 1u : 0u;
    return address + 4u;
  case 0x801234E8u:
    if (rd32(cpu, address) != 0x24020006u)
      return 0u;
    cpu->gpr[2] = 2u;
    return address + 4u;
  case 0x801234F8u: {
    uint32_t take;
    if (rd32(cpu, address) != 0x14620004u ||
        rd32(cpu, address + 4u) != 0x24020002u)
      return 0u;
    take = cpu->gpr[3] != cpu->gpr[2];
    cpu->gpr[2] = 1u; /* patched branch delay slot at 0x801234FC */
    return take ? 0x8012350Cu : 0x80123500u;
  }
  case 0x800E4640u:
    if (rd32(cpu, address) != 0x000A51C3u)
      return 0u;
    cpu->gpr[10] =
        (uint32_t)((int32_t)cpu->gpr[10] >> (7u + s_timing.p));
    return address + 4u;
  case 0x800E4650u:
    if (rd32(cpu, address) != 0x000B59C3u)
      return 0u;
    cpu->gpr[11] =
        (uint32_t)((int32_t)cpu->gpr[11] >> (7u + s_timing.p));
    return address + 4u;
  case 0x800E4660u:
    if (rd32(cpu, address) != 0x000529C3u)
      return 0u;
    cpu->gpr[5] =
        (uint32_t)((int32_t)cpu->gpr[5] >> (7u + s_timing.p));
    return address + 4u;
  case 0x801192E4u:
    /*
     * Own the complete Hue reduction through the stock loop branch.  E8/F0
     * are not AOT continuation points; returning to either one falls into the
     * interpreter and bypasses their native callbacks.  Keeping the preceding
     * load, comparison, branch-delay store, and loop decision atomic also
     * preserves the R3000 load-delay boundary.
     */
    if (rd32(cpu, address) != 0x26830001u ||
        rd32(cpu, address + 4u) != 0x00031C00u ||
        rd32(cpu, address + 8u) != 0x86A473D0u ||
        rd32(cpu, address + 12u) != 0x0003A403u ||
        rd32(cpu, address + 16u) != 0x0284202Au ||
        rd32(cpu, address + 20u) != 0x1480FFC5u ||
        rd32(cpu, address + 24u) != 0xA6220146u)
      return 0u;
    cpu->gpr[20] += 1u;
    cpu->gpr[2] <<= s_timing.p;
    cpu->gpr[4] = (uint32_t)(int32_t)rds16(cpu, cpu->gpr[21] + 0x73D0u);
    cpu->gpr[4] = ((int32_t)cpu->gpr[20] < (int32_t)cpu->gpr[4]) ? 1u : 0u;
    wr16(cpu, cpu->gpr[17] + 0x146u, (uint16_t)cpu->gpr[2]);
    return (cpu->gpr[4] != 0u) ? 0x80119210u : 0x80119300u;
  case 0x800F1DA4u:
    if (rd32(cpu, address) != 0x8E843AF8u)
      return 0u;
    cpu->gpr[4] = s_timing.p ? 0u : s_timing.rate;
    return address + 4u;

  case 0x800E39D4u:
    if (rd32(cpu, address) != 0x00852023u)
      return 0u;
    if (s_timing.work_gate)
      cpu->gpr[4] -= cpu->gpr[5];
    return address + 4u;

  case 0x800E3BB4u:
    if (rd32(cpu, 0x800E3BB4u) != 0x00024080u ||
        rd32(cpu, 0x800E3BB8u) != 0x01024021u ||
        rd32(cpu, 0x800E3BBCu) != 0x00084080u ||
        rd32(cpu, 0x800E3BC0u) != 0x01024023u ||
        rd32(cpu, 0x800E3BC4u) != 0x00084080u ||
        rd32(cpu, 0x800E3BC8u) != 0x01024021u)
      return 0u;
    cpu->gpr[8] = cpu->gpr[2] * (77u - 4u * s_timing.p);
    return 0x800E3BCCu;

  case 0x800E484Cu:
    if (rd32(cpu, 0x800E484Cu) != 0x0C041287u ||
        rd32(cpu, 0x800E4850u) != 0x00000000u ||
        rd32(cpu, 0x800E4854u) != 0x3042007Fu ||
        rd32(cpu, 0x800E4858u) != 0x24030004u)
      return 0u;
    cpu->gpr[2] = s_timing.frame_event;
    cpu->gpr[3] = 1u;
    return 0x800E485Cu;

  case 0x8011A0BCu:
  case 0x8011A12Cu:
    if (rd32(cpu, address) != 0x00441023u)
      return 0u;
    cpu->gpr[2] = cpu->gpr[4] - cpu->gpr[2];
    return address + 4u;
  case 0x8011A0C4u:
  case 0x8011A134u:
    if (rd32(cpu, address) != 0x00431804u)
      return 0u;
    cpu->gpr[3] =
        (uint32_t)((int32_t)cpu->gpr[3] >> (cpu->gpr[2] & 31u));
    return address + 4u;

  case 0x8011A650u:
    if (rd32(cpu, 0x8011A650u) != 0x8E433AF8u ||
        rd32(cpu, 0x8011A654u) != 0x8E653AFCu ||
        rd32(cpu, 0x8011A658u) != 0x00031080u ||
        rd32(cpu, 0x8011A65Cu) != 0x00431021u ||
        rd32(cpu, 0x8011A660u) != 0x00021080u ||
        rd32(cpu, 0x8011A664u) != 0x00431023u ||
        rd32(cpu, 0x8011A668u) != 0x00852023u ||
        rd32(cpu, 0x8011A66Cu) != 0x96030134u ||
        rd32(cpu, 0x8011A670u) != 0x00821004u ||
        rd32(cpu, 0x8011A674u) != 0x00621823u)
      return 0u;
    if (!s_timing.work_gate)
      return 0x8011A700u;
    cpu->gpr[3] = (uint32_t)rd16(cpu, cpu->gpr[16] + 0x134u) - 0x26u;
    return 0x8011A678u;

  case 0x8012BBF4u:
    if (rd32(cpu, address) != 0x3C04801Du)
      return 0u;
    s_timing.random_counter = 0u;
    s_timing.frame_event = 0u;
    return 0u; /* preserve the stock lui a0,0x801d */

  case 0x801234F0u:
    scheduler_update(cpu);
    cpu->gpr[3] = rd32(cpu, WIP3_FRAME_EVENT_WORD);
    cpu->gpr[2] = UINT32_MAX;
    return 0x801234F8u;

  case 0x800F15DCu:
  case 0x800F1D18u:
    if (s_timing.p == 0u) {
      if (address == 0x800F15DCu)
        cpu->gpr[3] = rd32(cpu, cpu->gpr[19] + 0x3AFCu);
      else
        cpu->gpr[3] = rd16(cpu, cpu->gpr[16] + 0x128u);
      return address + 4u;
    }
    return shared_high_rate(cpu, address);

  /* Published temporal-work-gate consumers. */
  case 0x800E2D40u:
    cpu->gpr[10] = load_unaligned_word(cpu, cpu->gpr[12] + 4u);
    if (!s_timing.work_gate) {
      cpu->gpr[3] = 0u;
      cpu->gpr[10] = 0u;
    }
    return 0x800E2D48u;

  case 0x800E2F38u:
    cpu->gpr[3] += cpu->gpr[4];
    cpu->gpr[2] = rd16(cpu, cpu->gpr[3]);
    if (!s_timing.work_gate)
      cpu->gpr[2] = 0u;
    cpu->gpr[2] = cpu->gpr[5] + cpu->gpr[2];
    return 0x800E2FE8u;

  case 0x800E2FBCu:
    cpu->gpr[2] = cpu->gpr[5] + cpu->gpr[2];
    cpu->gpr[3] = cpu->gpr[5];
    if (!s_timing.work_gate)
      cpu->gpr[2] = cpu->gpr[3];
    wr16(cpu, cpu->gpr[16] + 0x132u, (uint16_t)cpu->gpr[2]);
    return 0x800E2FECu;

  case 0x800E2FE4u:
    cpu->gpr[2] = cpu->gpr[3] + cpu->gpr[2];
    if (!s_timing.work_gate)
      cpu->gpr[2] = cpu->gpr[3];
    wr16(cpu, cpu->gpr[16] + 0x132u, (uint16_t)cpu->gpr[2]);
    return 0x800E2FECu;

  case 0x800E37F4u:
  case 0x800E3860u:
    if (s_timing.work_gate) {
      value = (uint32_t)((int32_t)cpu->gpr[2] >>
                         (2u * s_timing.rate + 10u - 2u * s_timing.p));
      cpu->gpr[2] = value;
      cpu->gpr[3] -= value;
    }
    cpu->gpr[31] = address + 8u;
    return address + 8u;

  case 0x800E4580u:
    cpu->gpr[2] = cpu->gpr[3] - s_timing.work_gate;
    wr16(cpu, cpu->gpr[16] + 0x154u, (uint16_t)cpu->gpr[2]);
    return 0x800E458Cu;

  case 0x8011C2DCu:
    if (!s_timing.work_gate)
      return cpu->gpr[31];
    cpu->gpr[2] = rd32(cpu, cpu->gpr[5]);
    return 0x8011C2E0u;

  case 0x8011DD7Cu:
    cpu->gpr[29] -= 0x38u;
    value = rd32(cpu, 0x801E9DA4u + 4u * cpu->gpr[5]);
    if ((int32_t)value <= 0 || s_timing.work_gate) {
      wr32(cpu, cpu->gpr[29] + 0x28u, cpu->gpr[20]);
      return 0x8011DD84u;
    }
    cpu->gpr[29] += 0x38u;
    return cpu->gpr[31];

  case 0x80124AE8u:
    /* Patched jump delay slot preserves the pre-add value unconditionally. */
    wr32(cpu, cpu->gpr[5] + 0x5EC0u, cpu->gpr[2]);
    if (s_timing.work_gate) {
      cpu->gpr[2] += cpu->gpr[3];
      wr32(cpu, cpu->gpr[5] + 0x5EC0u, cpu->gpr[2]);
    }
    return 0x80124AF0u;

  case 0x801396CCu:
    if (!s_timing.work_gate)
      return cpu->gpr[31];
    cpu->gpr[2] = 0x801E0000u;
    cpu->gpr[3] = rd32(cpu, 0x801E1B20u);
    return 0x801396D4u;

  case 0x8013BEF0u:
    /* Patched jump delay slot stores the original value on every path. */
    wr8(cpu, cpu->gpr[17] + 0x2Du, (uint8_t)cpu->gpr[2]);
    if (s_timing.work_gate) {
      cpu->gpr[2] -= 1u;
      wr8(cpu, cpu->gpr[17] + 0x2Du, (uint8_t)cpu->gpr[2]);
    }
    return 0x8013BEF8u;

  /* coefficient(x) = (x*coefficient) >> 16. */
  case 0x800E45FCu:
    cpu->gpr[2] = coefficient_scale(cpu->gpr[2]);
    cpu->gpr[6] -= cpu->gpr[2];
    return 0x800E4604u;
  case 0x800E460Cu:
    cpu->gpr[3] = coefficient_scale(cpu->gpr[3]);
    cpu->gpr[8] -= cpu->gpr[3];
    return 0x800E4614u;
  case 0x800E4628u:
    cpu->gpr[9] = coefficient_scale(cpu->gpr[9]);
    cpu->gpr[7] -= cpu->gpr[9];
    return 0x800E4630u;

  /* Bounded replacement for the patched title's two random-helper calls. */
  case 0x800E4880u:
  case 0x800E4894u:
    cpu->gpr[31] = address + 8u;
    cpu->gpr[4] = 0x80170000u;
    value = rd32(cpu, WIP3_RANDOM_A_WORD) + 0xA00u + s_timing.random_counter;
    cpu->gpr[3] = value;
    cpu->gpr[2] = ((value >> 1) ^ (value << 3)) & 0x7FFFu;
    return address + 8u;

  /* scale(x) = (x*K + 0x8000) >> 16. */
  case 0x8013A600u:
    cpu->gpr[3] = rate_scale(cpu->gpr[3]);
    cpu->gpr[2] += cpu->gpr[3];
    wr32(cpu, cpu->gpr[16] + 0x24u, cpu->gpr[2]);
    return 0x8013A608u;
  case 0x8013BED0u:
    value = rate_scale(rd32(cpu, cpu->gpr[17] + 0x10u));
    cpu->gpr[2] += value;
    cpu->gpr[3] = rd32(cpu, cpu->gpr[17] + 8u);
    return 0x8013BED8u;
  case 0x8013BED8u:
    cpu->gpr[5] = rate_scale(cpu->gpr[5]);
    cpu->gpr[4] += cpu->gpr[5];
    wr32(cpu, cpu->gpr[17] + 4u, cpu->gpr[4]);
    return 0x8013BEE0u;
  case 0x8013BEE0u:
    cpu->gpr[4] = rate_scale(rd32(cpu, cpu->gpr[17] + 0x18u));
    cpu->gpr[3] += cpu->gpr[4];
    wr32(cpu, cpu->gpr[17], cpu->gpr[2]);
    cpu->gpr[2] = rd8(cpu, cpu->gpr[17] + 0x2Du);
    return 0x8013BEF0u;
  case 0x80154088u:
    cpu->gpr[2] += s_timing.scale_k >> 9;
    wr16(cpu, cpu->gpr[4] + 6u, (uint16_t)cpu->gpr[2]);
    return 0x80154090u;
  case 0x80159070u:
    cpu->gpr[5] = rate_scale(cpu->gpr[5]);
    cpu->gpr[3] -= cpu->gpr[5];
    cpu->gpr[2] = rd32(cpu, cpu->gpr[18] + 0x10u);
    return 0x80159078u;
  case 0x80159078u:
    cpu->gpr[10] += rate_scale(cpu->gpr[3]);
    wr32(cpu, cpu->gpr[18] + 0x1Cu, cpu->gpr[3]);
    return 0x80159080u;
  case 0x8015908Cu:
    cpu->gpr[5] = rate_scale(cpu->gpr[5]);
    cpu->gpr[4] -= cpu->gpr[5];
    cpu->gpr[2] = rd32(cpu, cpu->gpr[18] + 0x24u);
    return 0x80159094u;
  case 0x80159094u:
    cpu->gpr[8] += rate_scale(cpu->gpr[4]);
    wr32(cpu, cpu->gpr[18] + 0x20u, cpu->gpr[4]);
    return 0x8015909Cu;
  case 0x801590A4u:
    cpu->gpr[5] = rate_scale((uint32_t)((int32_t)cpu->gpr[2] >> 6));
    cpu->gpr[2] -= cpu->gpr[5];
    return 0x801590ACu;
  case 0x801590ACu:
    cpu->gpr[9] += rate_scale(cpu->gpr[2]);
    wr32(cpu, cpu->gpr[18] + 0x24u, cpu->gpr[2]);
    return 0x801590B4u;

  /* loop_scale(x) = (x*rate) >> 2. */
  case 0x80119FC4u:
    cpu->gpr[31] = address + 8u;
    cpu->gpr[2] = (uint32_t)(int32_t)rds16(cpu, cpu->gpr[16] + 0x13Cu);
    cpu->gpr[2] = loop_scale(cpu->gpr[2]);
    cpu->gpr[1] = cpu->gpr[2];
    cpu->gpr[2] = ((int32_t)cpu->gpr[2] < (int32_t)cpu->gpr[3]) ? 1u : 0u;
    cpu->gpr[3] = cpu->gpr[1];
    return 0x80119FD4u;
  case 0x8011A010u:
    cpu->gpr[31] = address + 8u;
    cpu->gpr[2] = (uint32_t)(int32_t)rds16(cpu, cpu->gpr[16] + 0x13Cu);
    cpu->gpr[2] = loop_scale(cpu->gpr[2]);
    cpu->gpr[1] = cpu->gpr[2];
    return address + 8u;
  case 0x8011A040u:
    cpu->gpr[1] = s_timing.p ? s_timing.rate : 4u;
    cpu->gpr[2] = cpu->gpr[3] - loop_scale(cpu->gpr[2]);
    return 0x8011A058u;
  case 0x8011A048u:
    cpu->gpr[31] = address + 8u;
    cpu->gpr[2] = rd16(cpu, cpu->gpr[16] + 0x13Cu);
    cpu->gpr[2] = loop_scale(cpu->gpr[2]);
    cpu->gpr[1] = cpu->gpr[2];
    return address + 8u;
  default:
    return 0u;
  }
}

static void pal_activate(uint32_t selector) {
  s_pal_active = 1;
  s_pal_host_cadence_active = 0;
  memset(&s_timing, 0, sizeof(s_timing));
  memset(s_high_rate, 0, sizeof(s_high_rate));
  s_timing.selector = selector;
  s_timing.p = selector >= 2u;
  s_timing.sim_vblank_skip = (selector < 1u) + 1u;
  s_timing.rate = selector == 0u ? 2u : 1u;
  s_timing.scale_k = (0x8000u >> s_timing.p)
                       << (s_timing.rate - 1u);
  if (s_timing.p)
    s_timing.coefficient = s_timing.rate == 1u ? 0x0862u : 0x081Eu;
  else
    s_timing.coefficient = s_timing.rate == 1u ? 0x1111u : 0x1000u;
}

static void pal25_activate(void) { pal_activate(0u); }
static void pal50_activate(void) { pal_activate(1u); }
static void pal100_activate(void) { pal_activate(2u); }

enum {
  WIP3_PAL_STATE_SCHEMA = 1u,
  WIP3_PAL_STATE_HEADER_WORDS = 2u,
  WIP3_PAL_STATE_TIMING_WORDS = 16u,
  WIP3_PAL_STATE_RECORD_WORDS = 3u,
  WIP3_PAL_STATE_BYTES =
      4u * (WIP3_PAL_STATE_HEADER_WORDS + WIP3_PAL_STATE_TIMING_WORDS +
            WIP3_SHARED_RECORD_COUNT * WIP3_PAL_STATE_RECORD_WORDS)
};

static void pal_state_put_u32(uint8_t **cursor, uint32_t value) {
  uint8_t *p = *cursor;
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
  *cursor = p + 4u;
}

static uint32_t pal_state_get_u32(const uint8_t **cursor) {
  const uint8_t *p = *cursor;
  uint32_t value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  *cursor = p + 4u;
  return value;
}

static uint32_t pal_state_size(void) { return WIP3_PAL_STATE_BYTES; }

static int pal_state_save(uint8_t *out, uint32_t size) {
  uint8_t *cursor = out;
  uint32_t timing[WIP3_PAL_STATE_TIMING_WORDS];
  uint32_t i;
  if (!out || size != WIP3_PAL_STATE_BYTES)
    return 0;
  memcpy(timing, &s_timing, sizeof(timing));
  pal_state_put_u32(&cursor, s_pal_active ? 1u : 0u);
  pal_state_put_u32(&cursor, s_pal_host_cadence_active ? 1u : 0u);
  for (i = 0; i < WIP3_PAL_STATE_TIMING_WORDS; ++i)
    pal_state_put_u32(&cursor, timing[i]);
  for (i = 0; i < WIP3_SHARED_RECORD_COUNT; ++i) {
    pal_state_put_u32(&cursor, s_high_rate[i].x);
    pal_state_put_u32(&cursor, s_high_rate[i].y);
    pal_state_put_u32(&cursor, s_high_rate[i].packed);
  }
  return (uint32_t)(cursor - out) == size;
}

static int pal_state_load_expected(const uint8_t *data, uint32_t size,
                                   int apply, uint32_t expected_selector) {
  const uint8_t *cursor = data;
  Wip3PAL100State timing;
  Wip3HighRateRecord high_rate[WIP3_SHARED_RECORD_COUNT];
  uint32_t timing_words[WIP3_PAL_STATE_TIMING_WORDS];
  uint32_t active, cadence, i;
  uint32_t expected_p = expected_selector >= 2u;
  uint32_t expected_rate = expected_selector == 0u ? 2u : 1u;
  uint32_t expected_skip = (expected_selector < 1u) + 1u;
  uint32_t expected_scale =
      (0x8000u >> expected_p) << (expected_rate - 1u);
  uint32_t expected_coefficient = expected_p
      ? (expected_rate == 1u ? 0x0862u : 0x081Eu)
      : (expected_rate == 1u ? 0x1111u : 0x1000u);

  if (!data || size != WIP3_PAL_STATE_BYTES || expected_selector > 2u)
    return 0;
  active = pal_state_get_u32(&cursor);
  cadence = pal_state_get_u32(&cursor);
  for (i = 0; i < WIP3_PAL_STATE_TIMING_WORDS; ++i)
    timing_words[i] = pal_state_get_u32(&cursor);
  memcpy(&timing, timing_words, sizeof(timing));
  for (i = 0; i < WIP3_SHARED_RECORD_COUNT; ++i) {
    high_rate[i].x = pal_state_get_u32(&cursor);
    high_rate[i].y = pal_state_get_u32(&cursor);
    high_rate[i].packed = pal_state_get_u32(&cursor);
  }

  if (active != 1u || cadence > 1u || timing.selector != expected_selector ||
      timing.p != expected_p || timing.sim_vblank_skip != expected_skip ||
      timing.rate != expected_rate || timing.scale_k != expected_scale ||
      timing.coefficient != expected_coefficient ||
      timing.phase >= (4u << timing.p) || timing.phase_wrap > 1u ||
      timing.work_gate > 1u || timing.race_phase > 1u ||
      timing.frame_event > 1u || (uint32_t)(cursor - data) != size)
    return 0;

  if (apply) {
    s_pal_active = 1;
    s_pal_host_cadence_active = (int)cadence;
    s_timing = timing;
    memcpy(s_high_rate, high_rate, sizeof(s_high_rate));
    /* Restore the frontend side of the same cadence boundary. A checkpoint
     * before the first guarded title site is PAL50; after it, selector 100
     * owns a 2x CRTC and 100 native VBlanks per wall-clock second. */
    (void)psx_mod_set_crtc_refresh_multiplier(
        cadence && timing.p ? 2u : 1u);
    (void)psx_mod_set_native_vblank_rate(
        cadence && timing.p ? 100u : 50u);
  }
  return 1;
}

static int pal25_state_load(const uint8_t *data, uint32_t size, int apply) {
  return pal_state_load_expected(data, size, apply, 0u);
}
static int pal50_state_load(const uint8_t *data, uint32_t size, int apply) {
  return pal_state_load_expected(data, size, apply, 1u);
}
static int pal100_state_load(const uint8_t *data, uint32_t size, int apply) {
  return pal_state_load_expected(data, size, apply, 2u);
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_framerate_100_plugins) {
  static const uint32_t sites[] = {
      0x800EE290u, 0x800EEBF0u, 0x800E99C4u, 0x800E99D8u,
      0x800E9A04u, 0x801234C0u, 0x801234C4u, 0x801234DCu, 0x801234E8u,
      0x801234F8u, 0x800E4640u, 0x800E4650u, 0x800E4660u, 0x801192E4u,
      0x800F1DA4u, 0x800E39D4u, 0x800E3BB4u, 0x800E484Cu,
      0x8011A0BCu, 0x8011A0C4u, 0x8011A12Cu, 0x8011A134u,
      0x8011A650u, 0x8012BBF4u,
      0x800E2D40u, 0x800E2F38u, 0x800E2FBCu, 0x800E2FE4u, 0x800E37F4u,
      0x800E3860u, 0x800E4580u, 0x800E45FCu, 0x800E460Cu, 0x800E4628u,
      0x800E4880u, 0x800E4894u, 0x800F15DCu, 0x800F1D18u, 0x80119FC4u,
      0x8011A010u, 0x8011A040u, 0x8011A048u, 0x8011C2DCu, 0x8011DD7Cu,
      0x801234F0u, 0x80124AE8u, 0x801396CCu, 0x8013A600u, 0x8013BED0u,
      0x8013BED8u, 0x8013BEE0u, 0x8013BEF0u, 0x80154088u, 0x80159070u,
      0x80159078u, 0x8015908Cu, 0x80159094u, 0x801590A4u, 0x801590ACu};
  uint32_t i;

  (void)psx_mod_register_activation_plugin("wipeout3.framerate.pal25",
                                           pal25_activate);
  (void)psx_mod_register_activation_plugin("wipeout3.framerate.pal50",
                                           pal50_activate);
  (void)psx_mod_register_activation_plugin("wipeout3.framerate.pal-x2",
                                           pal100_activate);
  (void)psx_mod_register_state_plugin(
      "wipeout3.framerate.pal25", WIP3_PAL_STATE_SCHEMA,
      pal_state_size, pal_state_save, pal25_state_load);
  (void)psx_mod_register_state_plugin(
      "wipeout3.framerate.pal50", WIP3_PAL_STATE_SCHEMA,
      pal_state_size, pal_state_save, pal50_state_load);
  (void)psx_mod_register_state_plugin(
      "wipeout3.framerate.pal-x2", WIP3_PAL_STATE_SCHEMA,
      pal_state_size, pal_state_save, pal100_state_load);
  for (i = 0; i < (uint32_t)(sizeof(sites) / sizeof(sites[0])); ++i) {
    (void)psx_mod_register_instruction_site_plugin(
        "wipeout3.framerate.pal100-title", sites[i], pal100_title_site);
  }
  for (i = 0; i < (uint32_t)(sizeof(s_half_phase_sites) /
                              sizeof(s_half_phase_sites[0]));
       ++i) {
    (void)psx_mod_register_instruction_site_plugin(
        "wipeout3.framerate.pal100-title", s_half_phase_sites[i].address,
        pal100_half_phase_site);
  }
}

/* Read-only counters consumed by the release timing diagnostic. */
uint32_t wipeout3_pal100_scheduler_calls(void) {
  return s_timing.scheduler_calls;
}

uint32_t wipeout3_pal100_logical_ticks(void) {
  return s_timing.logical_ticks;
}

uint32_t wipeout3_pal100_race_centiseconds(void) {
  return s_timing.race_centiseconds;
}

uint32_t wipeout3_pal100_current_rate(void) { return s_timing.rate; }

uint32_t wipeout3_pal100_current_coefficient(void) {
  return s_timing.coefficient;
}
