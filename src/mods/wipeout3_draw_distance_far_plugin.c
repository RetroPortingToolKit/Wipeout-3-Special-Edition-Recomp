#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>
#include <string.h>

/*
 * Direct stock-title equivalent of Hueponik's complete 8 MiB DRAW DIST row.
 *
 * The published patch replaces the stock dual-view scene-mask owner with a
 * guest code engine and precomputes expanded visibility lists in high RAM.
 * This plugin instead keeps the authenticated stock owner intact and applies
 * the selected LOW/STOCK/NEAR/MID/FAR result at its return site using the same
 * signed MIPS formulas. STOCK executes the owner result unchanged. NEAR, MID,
 * and FAR union geometry with the stock-authored result. LOW deliberately
 * replaces the stock result with geometry-only visibility, matching mode 2.
 * It installs no guest code, allocates no guest/high RAM, and never reads or
 * writes Hueponik's TRACK 4X4 state.
 */
enum {
    WIP3_SCENE_MASK_OWNER_RETURN = 0x801422F4u,
    WIP3_TRACK_ROOT_PTR          = 0x801985D4u,
    WIP3_SCENE_MASK              = 0x801762CCu,
    WIP3_PRIMARY_VIEW_STATE      = 0x801762D4u,
    WIP3_SECONDARY_VIEW_STATE    = 0x801762D8u,
    WIP3_SECTION_STRIDE          = 0x40u,
    WIP3_SECTION_LIMIT           = 682u,
    WIP3_DRAW_FAR_VERTICAL       = 0x7FFEu
};

typedef struct Wip3DrawProfile {
    uint32_t longitudinal;
    uint32_t lateral;
    uint32_t max_planar;
    uint32_t mode;
} Wip3DrawProfile;

enum {
    WIP3_DRAW_MODE_UNION = 0u,
    WIP3_DRAW_MODE_STOCK = 1u,
    WIP3_DRAW_MODE_GEOMETRY_ONLY = 2u
};

typedef struct Wip3FarView {
    uint32_t current;
    int32_t current_x;
    int32_t current_y;
    int32_t current_z;
    int32_t direction_x;
    int32_t direction_z;
    int valid;
} Wip3FarView;

static int s_wipeout3_draw_far_active;
static Wip3DrawProfile s_wipeout3_draw_profile;

static uint32_t rd32(CPUState* cpu, uint32_t address) {
    return cpu->read_word(address);
}

static void wr32(CPUState* cpu, uint32_t address, uint32_t value) {
    cpu->write_word(address, value);
}

/* MIPS SUBU/absolute-value behavior, including two's-complement wrap. */
static uint32_t mips_abs32(uint32_t value) {
    uint32_t sign = 0u - (value >> 31);
    return (value ^ sign) - sign;
}

static uint32_t mips_mul_low(int32_t lhs, int32_t rhs) {
    return (uint32_t)((int64_t)lhs * (int64_t)rhs);
}

static int32_t coord(CPUState* cpu, uint32_t section, uint32_t offset) {
    return (int32_t)rd32(cpu, section + offset);
}

static Wip3FarView make_far_view(CPUState* cpu, uint32_t state,
                                  uint32_t sections, uint32_t count) {
    Wip3FarView view = {0u, 0, 0, 0, 0, 0, 0};
    uint32_t current;
    uint32_t relative;
    uint32_t index;
    uint32_t next;
    uint32_t dx_word;
    uint32_t dz_word;
    uint32_t abs_x;
    uint32_t abs_z;
    int32_t scale;

    if (!state)
        return view;
    current = rd32(cpu, state + 4u);
    if (current < sections)
        return view;
    relative = current - sections;
    if ((relative % WIP3_SECTION_STRIDE) != 0u)
        return view;
    index = relative / WIP3_SECTION_STRIDE;
    if (index >= count)
        return view;

    next = sections + ((index + 1u == count) ? 0u : index + 1u) *
                          WIP3_SECTION_STRIDE;
    /* Coordinates are immutable while this return-site callback runs. Cache
     * the view origin once; the previous implementation re-read all three
     * origin words for every one of up to 682 candidate sections. */
    view.current_x = coord(cpu, current, 0x10u);
    view.current_y = coord(cpu, current, 0x14u);
    view.current_z = coord(cpu, current, 0x18u);
    dx_word = (uint32_t)coord(cpu, next, 0x10u) -
              (uint32_t)view.current_x;
    dz_word = (uint32_t)coord(cpu, next, 0x18u) -
              (uint32_t)view.current_z;
    abs_x = mips_abs32(dx_word);
    abs_z = mips_abs32(dz_word);
    scale = (int32_t)(((int32_t)abs_x < (int32_t)abs_z) ? abs_z : abs_x);
    if (scale <= 0)
        return view;

    /* Hueponik performs SLL 8 before signed DIV, so preserve 32-bit wrap. */
    view.direction_x = (int32_t)(dx_word << 8) / scale;
    view.direction_z = (int32_t)(dz_word << 8) / scale;
    view.current = current;
    view.valid = 1;
    return view;
}

static int profile_visible(const Wip3FarView* view, int32_t candidate_x,
                           int32_t candidate_y, int32_t candidate_z,
                           const Wip3DrawProfile* profile) {
    uint32_t dx_word;
    uint32_t dz_word;
    uint32_t forward_word;
    uint32_t lateral_word;
    uint32_t vertical_word;
    int32_t forward;
    int32_t lateral;
    int32_t vertical;

    if (!view->valid)
        return 0;
    dx_word = (uint32_t)candidate_x - (uint32_t)view->current_x;
    dz_word = (uint32_t)candidate_z - (uint32_t)view->current_z;

    forward_word = mips_mul_low((int32_t)dz_word, view->direction_z) +
                   mips_mul_low((int32_t)dx_word, view->direction_x);
    forward = (int32_t)mips_abs32(forward_word) >> 8;
    if (forward > (int32_t)profile->longitudinal)
        return 0;

    lateral_word = mips_mul_low((int32_t)dx_word, view->direction_z) -
                   mips_mul_low((int32_t)dz_word, view->direction_x);
    lateral = (int32_t)mips_abs32(lateral_word) >> 8;
    if (lateral > (int32_t)profile->lateral)
        return 0;

    if ((forward > lateral ? forward : lateral) >
        (int32_t)profile->max_planar)
        return 0;

    vertical_word = (uint32_t)candidate_y - (uint32_t)view->current_y;
    vertical = (int32_t)mips_abs32(vertical_word);
    return vertical <= (int32_t)WIP3_DRAW_FAR_VERTICAL;
}

static void apply_draw_profile(CPUState* cpu, const Wip3DrawProfile* profile) {
    uint32_t root = rd32(cpu, WIP3_TRACK_ROOT_PTR);
    uint32_t count;
    uint32_t sections;
    uint32_t scene_mask;
    Wip3FarView primary;
    Wip3FarView secondary;
    uint32_t index;

    if (!root)
        return;
    count = rd32(cpu, root + 8u);
    sections = rd32(cpu, root + 0x14u);
    if (!sections || count == 0u || count > WIP3_SECTION_LIMIT)
        return;

    primary = make_far_view(cpu, rd32(cpu, WIP3_PRIMARY_VIEW_STATE),
                            sections, count);
    secondary = make_far_view(cpu, rd32(cpu, WIP3_SECONDARY_VIEW_STATE),
                              sections, count);
    if (!primary.valid && !secondary.valid)
        return;

    scene_mask = rd32(cpu, WIP3_SCENE_MASK);
    for (index = 0u; index < count; ++index) {
        uint32_t section = sections + index * WIP3_SECTION_STRIDE;
        uint32_t old_mask = rd32(cpu, section);
        uint32_t bits = 0u;
        int32_t candidate_x = coord(cpu, section, 0x10u);
        int32_t candidate_y = coord(cpu, section, 0x14u);
        int32_t candidate_z = coord(cpu, section, 0x18u);

        if (profile->mode == WIP3_DRAW_MODE_UNION &&
            (old_mask & ~3u) == scene_mask)
            bits = old_mask & 3u;

        if (profile_visible(&primary, candidate_x, candidate_y, candidate_z,
                            profile))
            bits |= 1u;
        if (profile_visible(&secondary, candidate_x, candidate_y, candidate_z,
                            profile))
            bits |= 2u;
        if (bits || profile->mode == WIP3_DRAW_MODE_GEOMETRY_ONLY)
            wr32(cpu, section, scene_mask | bits);
    }
}

static void wipeout3_draw_far_activate(void) {
    static const struct {
        const char* name;
        Wip3DrawProfile profile;
    } choices[] = {
        {"low",   {16000u, 14000u, 12000u, WIP3_DRAW_MODE_GEOMETRY_ONLY}},
        {"stock", {40000u, 34000u, 32000u, WIP3_DRAW_MODE_STOCK}},
        {"near",  {40000u, 34000u, 32000u, WIP3_DRAW_MODE_UNION}},
        {"mid",   {56000u, 48000u, 44000u, WIP3_DRAW_MODE_UNION}},
        {"far",   {65535u, 65535u, 65535u, WIP3_DRAW_MODE_UNION}},
    };
    char selected[16] = "far";
    uint32_t i;

    (void)psx_mod_option_value("wipeout3.enhancement.draw-distance-far",
                               "draw-distance-far", "distance", selected,
                               (uint32_t)sizeof(selected));
    s_wipeout3_draw_profile = choices[4].profile;
    for (i = 0u; i < (uint32_t)(sizeof(choices) / sizeof(choices[0])); ++i) {
        if (strcmp(selected, choices[i].name) == 0) {
            s_wipeout3_draw_profile = choices[i].profile;
            break;
        }
    }
    s_wipeout3_draw_far_active = 1;
}

static uint32_t wipeout3_draw_far_return(CPUState* cpu, uint32_t address) {
    uint32_t continuation;

    if (!s_wipeout3_draw_far_active || !cpu ||
        address != WIP3_SCENE_MASK_OWNER_RETURN)
        return 0u;

    /* STOCK is exactly the authenticated owner, including its own JR/delay. */
    if (s_wipeout3_draw_profile.mode == WIP3_DRAW_MODE_STOCK)
        return 0u;

    apply_draw_profile(cpu, &s_wipeout3_draw_profile);

    /* Replace stock `jr ra`; its delay slot is `addiu sp,sp,0x28`. */
    continuation = cpu->gpr[31];
    cpu->gpr[29] += 0x28u;
    return continuation;
}

PSX_MOD_CONSTRUCTOR(wipeout3_register_draw_distance_far_plugin) {
    const char* id = "wipeout3.enhancement.draw-distance-far";
    (void)psx_mod_register_activation_plugin(id,
                                             wipeout3_draw_far_activate);
    (void)psx_mod_register_instruction_site_plugin(
        id, WIP3_SCENE_MASK_OWNER_RETURN, wipeout3_draw_far_return);
}
