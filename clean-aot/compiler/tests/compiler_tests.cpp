#include "clean_aot/compiler/cpp_emitter.hpp"
#include "clean_aot/compiler/mips_decoder.hpp"
#include "clean_aot/compiler/region_builder.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using clean_aot::compiler::EdgeKind;
using clean_aot::compiler::GuestAddress;
using clean_aot::compiler::InstructionKind;
using clean_aot::compiler::PsxExeImage;
using clean_aot::compiler::Region;
using clean_aot::compiler::RegionBuildOptions;
using clean_aot::compiler::RegionBuilder;

class TestFailure final : public std::runtime_error {
public:
    explicit TestFailure(const std::string& message)
        : std::runtime_error(message) {}
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw TestFailure(std::string(message));
    }
}

template <typename Function>
void require_throws(Function&& function, std::string_view message) {
    bool threw = false;
    try {
        std::invoke(std::forward<Function>(function));
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

void write_le32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    require(offset <= bytes.size() && bytes.size() - offset >= 4U,
            "fixture write must be within the byte vector");
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::uint32_t encode_jump(GuestAddress address, GuestAddress target) {
    require(((address + 4U) & 0xf0000000U) == (target & 0xf0000000U),
            "synthetic jump target must share the instruction region high bits");
    return 0x08000000U | ((target >> 2U) & 0x03ffffffU);
}

std::uint32_t encode_beq(std::int16_t immediate) {
    return 0x10000000U | static_cast<std::uint16_t>(immediate);
}

std::uint32_t encode_call(GuestAddress address, GuestAddress target) {
    require(((address + 4U) & 0xf0000000U) == (target & 0xf0000000U),
            "synthetic call target must share the instruction region high bits");
    return 0x0c000000U | ((target >> 2U) & 0x03ffffffU);
}

std::uint32_t encode_regimm(std::uint32_t rs, std::uint32_t function,
                            std::int16_t immediate) {
    return 0x04000000U | ((rs & 0x1fU) << 21U) | ((function & 0x1fU) << 16U) |
           static_cast<std::uint16_t>(immediate);
}

std::uint32_t encode_jr(std::uint32_t rs) {
    return ((rs & 0x1fU) << 21U) | 0x08U;
}

std::uint32_t encode_jalr(std::uint32_t rs, std::uint32_t rd) {
    return ((rs & 0x1fU) << 21U) | ((rd & 0x1fU) << 11U) | 0x09U;
}

std::vector<std::uint8_t> make_exe(GuestAddress load,
                                   GuestAddress entry,
                                   std::vector<std::uint8_t> text) {
    require(text.size() % 4U == 0U && !text.empty(),
            "synthetic text must be a non-empty word sequence");
    std::vector<std::uint8_t> bytes(PsxExeImage::kHeaderSize + text.size(), 0U);
    const std::string magic = "PS-X EXE";
    for (std::size_t index = 0; index < magic.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(magic[index]);
    }
    write_le32(bytes, 0x10U, entry);
    write_le32(bytes, 0x18U, load);
    write_le32(bytes, 0x1cU, static_cast<std::uint32_t>(text.size()));
    std::copy(text.begin(), text.end(), bytes.begin() + static_cast<std::ptrdiff_t>(
        PsxExeImage::kHeaderSize));
    return bytes;
}

PsxExeImage make_image(GuestAddress load,
                       GuestAddress entry,
                       std::vector<std::uint8_t> text) {
    const std::vector<std::uint8_t> bytes = make_exe(load, entry, std::move(text));
    return PsxExeImage::parse(bytes);
}

void test_checked_exe_ingestion() {
    std::vector<std::uint8_t> text(8U, 0U);
    write_le32(text, 0U, 0x12345678U);
    const std::vector<std::uint8_t> bytes = make_exe(0x00001000U, 0x00001000U, std::move(text));
    const PsxExeImage image = PsxExeImage::parse(bytes);
    require(image.entry_pc() == 0x00001000U, "entry PC was not parsed");
    require(image.load_address() == 0x00001000U, "load address was not parsed");
    require(image.text_size() == 8U, "text size was not parsed");
    require(image.contains(0x00001000U, 4U), "first text word should be readable");
    require(!image.contains(0x00001008U, 1U), "one-past-end address must be rejected");
    require(image.read_u32(0x00001000U) == 0x12345678U,
            "guest word reads must use bounded little-endian access");

    require_throws([&] {
        (void)image.read_u32(0x00001008U);
    }, "out-of-range guest word reads must be rejected");

    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    require_throws([&] {
        (void)PsxExeImage::parse(truncated);
    }, "truncated text must be rejected");

    std::vector<std::uint8_t> bad_magic = bytes;
    bad_magic[0] = static_cast<std::uint8_t>('X');
    require_throws([&] {
        (void)PsxExeImage::parse(bad_magic);
    }, "bad magic must be rejected");

    std::vector<std::uint8_t> oversized = bytes;
    write_le32(oversized, 0x1cU, 12U);
    require_throws([&] {
        (void)PsxExeImage::parse(oversized);
    }, "text extending beyond the file must be rejected");

    std::vector<std::uint8_t> outside_entry = bytes;
    write_le32(outside_entry, 0x10U, 0x00001008U);
    require_throws([&] {
        (void)PsxExeImage::parse(outside_entry);
    }, "an entry at the text end must be rejected");

    std::vector<std::uint8_t> wrapping = make_exe(
        0xfffffff0U, 0xfffffff0U, std::vector<std::uint8_t>(0x20U, 0U));
    require_throws([&] {
        (void)PsxExeImage::parse(wrapping);
    }, "a text range wrapping the guest address space must be rejected");

    std::vector<std::uint8_t> zero_text = bytes;
    write_le32(zero_text, 0x1cU, 0U);
    require_throws([&] {
        (void)PsxExeImage::parse(zero_text);
    }, "a zero-size text image must be rejected");

    std::vector<std::uint8_t> unaligned_load = bytes;
    write_le32(unaligned_load, 0x18U, 0x00001001U);
    require_throws([&] {
        (void)PsxExeImage::parse(unaligned_load);
    }, "an unaligned load address must be rejected");
}

void test_bounded_raw_image_ingestion() {
    std::vector<std::uint8_t> bytes(8U, 0U);
    write_le32(bytes, 0U, 0x12345678U);
    const PsxExeImage image = PsxExeImage::parse_raw(bytes, 0x00002000U, 0x00002004U);
    require(image.load_address() == 0x00002000U && image.entry_pc() == 0x00002004U,
            "raw image must preserve explicit bounded placement");
    require(image.text_end_exclusive() == 0x00002008U &&
                image.read_u32(0x00002000U) == 0x12345678U,
            "raw image bytes must be available to the region builder");
    require_throws([&] {
        (void)PsxExeImage::parse_raw(bytes, 0xfffffffCU, 0xfffffffCU);
    }, "raw image address wrap must fail closed");
    require_throws([&] {
        (void)PsxExeImage::parse_raw(bytes, 0x00002000U, 0x00002008U);
    }, "raw image entry outside the bounded payload must fail closed");
    require_throws([&] {
        (void)PsxExeImage::parse_raw(std::span<const std::uint8_t>(bytes).first(6U),
                                     0x00002000U, 0x00002000U);
    }, "partial instruction bytes must fail closed");
}

void test_direct_decoder_control_classes() {
    const auto branch = clean_aot::compiler::decode_instruction(
        0x00001000U, encode_regimm(2U, 0x00U, -2));
    require(branch.kind == InstructionKind::ConditionalBranch &&
                branch.has_delay_slot && branch.direct_target == 0x00000ffcU,
            "BLTZ must decode as a direct conditional branch");

    const auto conditional_call = clean_aot::compiler::decode_instruction(
        0x00001000U, encode_regimm(2U, 0x10U, 2));
    require(conditional_call.kind == InstructionKind::ConditionalCall &&
                conditional_call.direct_target == 0x0000100cU,
            "BLTZAL must retain conditional call identity");

    const auto mirrored_regimm = clean_aot::compiler::decode_instruction(
        0x00001000U, encode_regimm(2U, 0x02U, 2));
    require(mirrored_regimm.kind == InstructionKind::ConditionalBranch &&
                mirrored_regimm.has_delay_slot &&
                mirrored_regimm.direct_target == 0x0000100cU,
            "R3000A REGIMM mirror encodings must retain hardware branch behavior");

    const auto return_instruction = clean_aot::compiler::decode_instruction(
        0x00001000U, encode_jr(31U));
    require(return_instruction.kind == InstructionKind::Return,
            "JR $ra must retain return identity");
    const auto indirect_jump = clean_aot::compiler::decode_instruction(
        0x00001000U, encode_jalr(4U, 0U));
    require(indirect_jump.kind == InstructionKind::IndirectJump,
            "JALR with rd=$zero must not be classified as a call");
    const auto indirect_call = clean_aot::compiler::decode_instruction(
        0x00001000U, encode_jalr(4U, 31U));
    require(indirect_call.kind == InstructionKind::IndirectCall,
            "JALR with a link destination must be classified as a call");

    const std::uint32_t blez_ignored_rt = 0x18010000U;
    require(clean_aot::compiler::decode_instruction(0x00001000U, blez_ignored_rt).kind ==
                InstructionKind::ConditionalBranch,
            "R3000A BLEZ must ignore its nominally unused rt field");
    const std::uint32_t beq_distinct_registers =
        0x10000000U | (2U << 21U) | (3U << 16U) | 1U;
    require(clean_aot::compiler::decode_instruction(
                0x00001000U, beq_distinct_registers).kind ==
                InstructionKind::ConditionalBranch,
            "BEQ must permit two distinct source registers");
    const std::uint32_t jr_ignored_fields = encode_jr(31U) | (1U << 16U);
    require(clean_aot::compiler::decode_instruction(0x00001000U, jr_ignored_fields).kind ==
                InstructionKind::Return,
            "R3000A JR must ignore non-function operand fields");

    const auto wrapped_jump = clean_aot::compiler::decode_instruction(
        0xfffffffcU, 0x08000001U);
    require(wrapped_jump.direct_target == 0x00000004U,
            "J target high bits must come from wrapped PC+4");
    const auto wrapped_branch = clean_aot::compiler::decode_instruction(
        0x00000000U, encode_beq(-2));
    require(wrapped_branch.direct_target == 0xfffffffcU,
            "negative branch displacement must wrap in the 32-bit guest space");
}

Region make_trampoline_region() {
    std::vector<std::uint8_t> text(0x24U, 0U);
    write_le32(text, 0x00U, encode_jump(0x00001000U, 0x00001010U));
    write_le32(text, 0x04U, 0x24210001U);
    write_le32(text, 0x10U, encode_jump(0x00001010U, 0x00001020U));
    write_le32(text, 0x14U, 0x24210001U);
    write_le32(text, 0x20U, 0x34021234U);

    const PsxExeImage image = make_image(0x00001000U, 0x00001000U, std::move(text));
    RegionBuildOptions options;
    options.entry_points = {0x00001000U};
    return RegionBuilder::build(image, std::move(options));
}

Region make_generated_fixture_region() {
    std::vector<std::uint8_t> text(0x3cU, 0U);
    write_le32(text,0x00U,encode_jump(0x00001000U,0x00001010U));write_le32(text,0x04U,0x24210001U);
    write_le32(text,0x10U,encode_jump(0x00001010U,0x00001020U));write_le32(text,0x14U,0x24210001U);write_le32(text,0x20U,0x34021234U);
    write_le32(text,0x30U,(0x22U<<26U)|(1U<<21U)|(2U<<16U)|3U);write_le32(text,0x34U,(0x26U<<26U)|(1U<<21U)|(2U<<16U));
    const PsxExeImage image=make_image(0x00001000U,0x00001000U,std::move(text));RegionBuildOptions options;options.entry_points={0x00001000U,0x00001030U};return RegionBuilder::build(image,std::move(options));
}

void test_unnamed_direct_path_preserves_targets() {
    const Region region = make_trampoline_region();
    require(region.blocks.size() == 3U,
            "A, trampoline, and continuation must become three explicit blocks");
    require(region.edges.size() == 3U,
            "the direct path and its final external fallthrough must be explicit");
    require(region.blocks[0].start == 0x00001000U, "first block ordering is not deterministic");
    require(region.blocks[1].start == 0x00001010U,
            "trampoline block was not discovered by its direct target");
    require(region.blocks[2].start == 0x00001020U,
            "continuation block was not discovered by the trampoline target");
    require(region.edges[0].kind == EdgeKind::DirectJump &&
                region.edges[0].target == 0x00001010U,
            "A must have a direct edge to the trampoline");
    require(region.edges[1].kind == EdgeKind::DirectJump &&
                region.edges[1].target == 0x00001020U,
            "the trampoline must have a direct edge to the continuation");
    require(region.edges[0].target_block == region.blocks[1].id,
            "A edge must resolve to the trampoline block ID");
    require(region.edges[1].target_block == region.blocks[2].id,
            "trampoline edge must resolve to the continuation block ID");
    require(region.edges[2].kind == EdgeKind::Fallthrough &&
                region.edges[2].target == 0x00001024U &&
                !region.edges[2].target_block.has_value(),
            "the unnamed continuation must publish its external next PC");
    for (const auto& edge : region.edges) {
        require(edge.target.has_value() && *edge.target != 0U,
                "a resolved direct edge must never use zero as a missing-name sentinel");
    }
}

void test_branch_delay_slot_and_fallthrough() {
    std::vector<std::uint8_t> text(0x10U, 0U);
    write_le32(text, 0x00U, encode_beq(2));
    write_le32(text, 0x04U, 0U);
    write_le32(text, 0x08U, 0U);
    write_le32(text, 0x0cU, 0U);

    const PsxExeImage image = make_image(0x00002000U, 0x00002000U, std::move(text));
    RegionBuildOptions options;
    options.entry_points = {0x00002000U};
    const Region region = RegionBuilder::build(image, std::move(options));

    require(region.edges.size() == 4U,
            "conditional successors and both later fallthroughs must be explicit");
    require(region.edges[0].kind == EdgeKind::BranchTaken &&
                region.edges[0].target == 0x0000200cU,
            "taken edge must use the decoded branch target");
    require(region.edges[1].kind == EdgeKind::BranchFallthrough &&
                region.edges[1].target == 0x00002008U,
            "fallthrough must skip the delay slot");
    require(region.blocks[0].instructions.size() == 2U,
            "branch block must own exactly its branch and delay slot");
    require(region.blocks[0].terminator_index == 0U,
            "branch terminator must precede its delay slot");
    require(region.edges.back().kind == EdgeKind::Fallthrough &&
                region.edges.back().target == 0x00002010U,
            "the final reachable block must publish its external next PC");
}

void test_bounded_straight_line_publishes_external_fallthrough() {
    const PsxExeImage image = make_image(
        0x00002400U, 0x00002400U, std::vector<std::uint8_t>(8U, 0U));
    RegionBuildOptions options;
    options.entry_points = {0x00002400U};
    options.end_exclusive = 0x00002404ULL;
    const Region region = RegionBuilder::build(image, std::move(options));

    require(region.blocks.size() == 1U, "bounded fixture must contain one block");
    require(region.edges.size() == 1U,
            "a straight-line block at the region boundary needs an external edge");
    require(region.edges[0].kind == EdgeKind::Fallthrough &&
                region.edges[0].target == 0x00002404U &&
                !region.edges[0].target_block.has_value(),
            "bounded fallthrough must publish the architectural next PC externally");

    const std::string source = clean_aot::compiler::CppEmitter::emit(region);
    require(source.find("pc = 0x00002404u;") != std::string::npos,
            "emitted boundary fallthrough must write the architectural next PC");
}

void test_image_end_fallthrough_can_publish_zero() {
    const PsxExeImage image = make_image(
        0xfffffffcU, 0xfffffffcU, std::vector<std::uint8_t>(4U, 0U));
    RegionBuildOptions options;
    options.entry_points = {0xfffffffcU};
    const Region region = RegionBuilder::build(image, std::move(options));

    require(region.edges.size() == 1U && region.edges[0].target == 0x00000000U,
            "32-bit sequential PC wrap must be an explicit edge, not a missing target");
    const std::string source = clean_aot::compiler::CppEmitter::emit(region);
    require(source.find("pc = 0x00000000u;") != std::string::npos,
            "a real zero next PC must be emitted explicitly");
}

void test_region_bounds_and_entries_require_instruction_alignment() {
    const PsxExeImage image = make_image(
        0x00002800U, 0x00002800U, std::vector<std::uint8_t>(12U, 0U));

    require_throws([&] {
        RegionBuildOptions options;
        options.entry_points = {0x00002801U};
        (void)RegionBuilder::build(image, std::move(options));
    }, "unaligned region entries must be rejected");

    require_throws([&] {
        RegionBuildOptions options;
        options.entry_points = {0x00002800U};
        options.end_exclusive = 0x00002806ULL;
        (void)RegionBuilder::build(image, std::move(options));
    }, "unaligned region ends must be rejected");
}

void test_direct_call_emitter_advances_to_callee() {
    std::vector<std::uint8_t> text(0x14U, 0U);
    write_le32(text, 0x00U, encode_call(0x00002c00U, 0x00002c10U));
    const PsxExeImage image = make_image(0x00002c00U, 0x00002c00U, std::move(text));
    RegionBuildOptions options;
    options.entry_points = {0x00002c00U};
    const Region region = RegionBuilder::build(image, std::move(options));

    const std::string source = clean_aot::compiler::CppEmitter::emit(region);
    const std::size_t call_case = source.find("case 0x00002C00u:");
    require(call_case != std::string::npos, "direct-call block must be emitted");
    const std::size_t next_case = source.find("    case ", call_case + 1U);
    const std::string call_body = source.substr(call_case, next_case - call_case);
    require(call_body.find("pc = 0x00002C10u;") != std::string::npos,
            "direct call must advance to its callee rather than leave a stale PC");
}

void test_return_and_external_control_edges_are_explicit() {
    std::vector<std::uint8_t> return_text(8U, 0U);
    write_le32(return_text, 0U, encode_jr(31U));
    const PsxExeImage return_image = make_image(
        0x00003400U, 0x00003400U, std::move(return_text));
    RegionBuildOptions return_options;
    return_options.entry_points = {0x00003400U};
    const Region return_region = RegionBuilder::build(
        return_image, std::move(return_options));
    require(return_region.edges.size() == 1U &&
                return_region.edges[0].kind == EdgeKind::Return &&
                !return_region.edges[0].target.has_value(),
            "return must be represented as an explicit dynamic edge");

    std::vector<std::uint8_t> jump_text(8U, 0U);
    write_le32(jump_text, 0U, encode_jump(0x00003420U, 0x00003800U));
    const PsxExeImage jump_image = make_image(
        0x00003420U, 0x00003420U, std::move(jump_text));
    RegionBuildOptions jump_options;
    jump_options.entry_points = {0x00003420U};
    const Region jump_region = RegionBuilder::build(
        jump_image, std::move(jump_options));
    require(jump_region.edges.size() == 1U &&
                jump_region.edges[0].kind == EdgeKind::DirectJump &&
                jump_region.edges[0].target == 0x00003800U &&
                !jump_region.edges[0].target_block.has_value(),
            "direct jumps outside the image must retain exact external targets");
}

void test_delay_slot_boundaries_fail_closed() {
    std::vector<std::uint8_t> text(8U, 0U);
    write_le32(text, 0U, encode_beq(0));
    const PsxExeImage image = make_image(0x00003c00U, 0x00003c00U, std::move(text));

    require_throws([&] {
        RegionBuildOptions options;
        options.entry_points = {0x00003c00U};
        options.end_exclusive = 0x00003c04ULL;
        (void)RegionBuilder::build(image, std::move(options));
    }, "a region boundary must never split a branch from its delay slot");

    require_throws([&] {
        RegionBuildOptions options;
        options.entry_points = {0x00003c00U, 0x00003c04U};
        (void)RegionBuilder::build(image, std::move(options));
    }, "an independently entered delay slot must fail closed in this IR tranche");
}

void test_loop_and_emission_order_are_deterministic() {
    std::vector<std::uint8_t> loop_text(8U, 0U);
    write_le32(loop_text, 0U, encode_jump(0x00004000U, 0x00004000U));
    const PsxExeImage loop_image = make_image(
        0x00004000U, 0x00004000U, std::move(loop_text));
    RegionBuildOptions loop_options;
    loop_options.entry_points = {0x00004000U, 0x00004000U};
    const Region loop = RegionBuilder::build(loop_image, std::move(loop_options));
    require(loop.blocks.size() == 1U && loop.edges.size() == 1U &&
                loop.edges[0].target_block == loop.blocks[0].id,
            "a direct self-loop must resolve without duplicate expansion");

    const Region canonical = make_trampoline_region();
    Region reordered = canonical;
    std::reverse(reordered.blocks.begin(), reordered.blocks.end());
    std::reverse(reordered.edges.begin(), reordered.edges.end());
    require(clean_aot::compiler::CppEmitter::emit(reordered) ==
                clean_aot::compiler::CppEmitter::emit(canonical),
            "emission must be canonical regardless of IR container order");
}

void test_ir_validation_rejects_missing_boundary_edge() {
    const PsxExeImage image = make_image(
        0x00004400U, 0x00004400U, std::vector<std::uint8_t>(4U, 0U));
    RegionBuildOptions options;
    options.entry_points = {0x00004400U};
    Region region = RegionBuilder::build(image, std::move(options));
    region.edges.clear();
    require_throws([&] {
        region.validate();
    }, "IR validation must reject the historical missing-fallthrough shape");
}

void test_adversarial_instruction_streams_are_deterministic() {
    std::uint32_t random_state = 0x6d2b79f5U;
    auto next_random = [&] {
        random_state ^= random_state << 13U;
        random_state ^= random_state >> 17U;
        random_state ^= random_state << 5U;
        return random_state;
    };

    std::size_t accepted = 0U;
    std::size_t rejected = 0U;
    for (std::uint32_t fixture = 0U; fixture < 256U; ++fixture) {
        std::vector<std::uint8_t> text(64U * sizeof(std::uint32_t), 0U);
        for (std::size_t offset = 0U; offset < text.size(); offset += 4U) {
            write_le32(text, offset, next_random());
        }

        const GuestAddress load = 0x00100000U + fixture * 0x00001000U;
        const PsxExeImage image = make_image(load, load, std::move(text));
        RegionBuildOptions options;
        options.entry_points = {load};
        try {
            const Region region = RegionBuilder::build(image, std::move(options));
            const std::string first = clean_aot::compiler::CppEmitter::emit(region);
            const std::string second = clean_aot::compiler::CppEmitter::emit(region);
            require(first == second,
                    "the same adversarial region must emit byte-identical source");
            ++accepted;
        } catch (const clean_aot::compiler::IrError&) {
            // Unsupported branch-in-delay-slot and ambiguous delay-slot shapes
            // are expected to fail closed, never to produce partial IR.
            ++rejected;
        }
    }
    require(accepted != 0U, "adversarial corpus must exercise accepted regions");
    require(rejected != 0U, "adversarial corpus must exercise fail-closed regions");
}

void test_backward_direct_edge_is_inside_the_region() {
    std::vector<std::uint8_t> text(0x10U, 0U);
    write_le32(text, 0x00U, 0U);
    write_le32(text, 0x04U, encode_jump(0x00003004U, 0x00003000U));
    write_le32(text, 0x08U, 0U);
    write_le32(text, 0x0cU, 0U);

    const PsxExeImage image = make_image(0x00003000U, 0x00003004U, std::move(text));
    RegionBuildOptions options;
    options.entry_points = {0x00003004U};
    const Region region = RegionBuilder::build(image, std::move(options));

    require(region.start == 0x00003000U,
            "region bounds must cover reachable addresses below the entry point");
    require(region.blocks.size() == 2U,
            "backward direct target must be materialized as a second basic block");
    require(region.edges.size() == 2U,
            "backward path must retain its delay-slot fallthrough and direct edge");
    require(region.edges[1].kind == EdgeKind::DirectJump &&
                region.edges[1].target == 0x00003000U,
            "backward direct edge must retain its target address");
}

void test_emitter_contains_compilable_graph_shape() {
    const Region region = make_trampoline_region();
    const std::string source = clean_aot::compiler::CppEmitter::emit(region);
    require(source.find("#include <optional>") != std::string::npos,
            "emitter must include the type used for unresolved edges");
    require(source.find("0x00001010u") != std::string::npos,
            "emitter must preserve the trampoline target");
    require(source.find("0x00001020u") != std::string::npos,
            "emitter must preserve the continuation target");
    require(source.find("pc = 0;") == std::string::npos,
            "emitter must not synthesize a zero PC for an unresolved name");
    require(source.find(
                "extern \"C\" bool clean_aot_region_00001000(GeneratedCpuState& state) noexcept") !=
                std::string::npos,
            "emitter must provide a stable, region-unique dispatch ABI");
}

void test_emitter_contains_cop0_semantics() {
    std::vector<std::uint8_t> text(20U, 0U);
    write_le32(text, 0x00U, 0x408c6000U); // mtc0 t4,status
    write_le32(text, 0x04U, 0x400d7000U); // mfc0 t5,epc
    write_le32(text, 0x08U, 0x42000010U); // rfe
    write_le32(text, 0x0cU, 0x214a0080U); // addi t2,t2,128
    const PsxExeImage image = make_image(0x00004000U, 0x00004000U, std::move(text));
    RegionBuildOptions options;
    options.entry_points = {0x00004000U};
    options.start_inclusive = 0x00004000U;
    options.end_exclusive = 0x00004010U;
    const std::string source = clean_aot::compiler::CppEmitter::emit(
        RegionBuilder::build(image, std::move(options)));
    require(source.find("cpu_ops::mtc0(state,12,12)") != std::string::npos,
            "emitter must lower MTC0 to explicit generated CPU state");
    require(source.find("cpu_ops::mfc0(state,13,14)") != std::string::npos,
            "emitter must lower MFC0 with load-delay semantics");
    require(source.find("cpu_ops::rfe(state)") != std::string::npos,
            "emitter must lower RFE status-stack restoration");
    require(source.find("cpu_ops::add(state,10,state.gpr[10],0x80u)") != std::string::npos,
            "emitter must lower ADDI with overflow semantics");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--emit") {
            std::cerr << "usage: clean_aot_compiler_tests [--emit output.cpp]\n";
            return 2;
        }
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "unable to open emitter output path\n";
            return 2;
        }
        output << clean_aot::compiler::CppEmitter::emit(make_generated_fixture_region());
        if (!output) {
            std::cerr << "unable to write emitter output\n";
            return 2;
        }
        std::cout << "EMITTED " << argv[2] << '\n';
        return 0;
    }

    const std::vector<std::pair<std::string_view, std::function<void()>>> tests = {
        {"checked_exe_ingestion", test_checked_exe_ingestion},
        {"bounded_raw_image_ingestion", test_bounded_raw_image_ingestion},
        {"direct_decoder_control_classes", test_direct_decoder_control_classes},
        {"unnamed_direct_path_preserves_targets", test_unnamed_direct_path_preserves_targets},
        {"branch_delay_slot_and_fallthrough", test_branch_delay_slot_and_fallthrough},
        {"bounded_straight_line_publishes_external_fallthrough",
         test_bounded_straight_line_publishes_external_fallthrough},
        {"image_end_fallthrough_can_publish_zero",
         test_image_end_fallthrough_can_publish_zero},
        {"region_bounds_and_entries_require_instruction_alignment",
         test_region_bounds_and_entries_require_instruction_alignment},
        {"direct_call_emitter_advances_to_callee",
         test_direct_call_emitter_advances_to_callee},
        {"return_and_external_control_edges_are_explicit",
         test_return_and_external_control_edges_are_explicit},
        {"delay_slot_boundaries_fail_closed", test_delay_slot_boundaries_fail_closed},
        {"loop_and_emission_order_are_deterministic",
         test_loop_and_emission_order_are_deterministic},
        {"ir_validation_rejects_missing_boundary_edge",
         test_ir_validation_rejects_missing_boundary_edge},
        {"adversarial_instruction_streams_are_deterministic",
         test_adversarial_instruction_streams_are_deterministic},
        {"backward_direct_edge_is_inside_the_region", test_backward_direct_edge_is_inside_the_region},
        {"emitter_contains_compilable_graph_shape", test_emitter_contains_compilable_graph_shape},
        {"emitter_contains_cop0_semantics", test_emitter_contains_cop0_semantics},
    };

    std::size_t passed = 0U;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "PASS all " << passed << " focused tests\n";
    return 0;
}
