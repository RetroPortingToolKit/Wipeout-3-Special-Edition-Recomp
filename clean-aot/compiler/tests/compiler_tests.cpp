#include "clean_aot/compiler/cpp_emitter.hpp"
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

    bool threw = false;
    try {
        (void)image.read_u32(0x00001008U);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "out-of-range guest word reads must be rejected");

    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    threw = false;
    try {
        (void)PsxExeImage::parse(truncated);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "truncated text must be rejected");

    std::vector<std::uint8_t> bad_magic = bytes;
    bad_magic[0] = static_cast<std::uint8_t>('X');
    threw = false;
    try {
        (void)PsxExeImage::parse(bad_magic);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "bad magic must be rejected");

    std::vector<std::uint8_t> oversized = bytes;
    write_le32(oversized, 0x1cU, 12U);
    threw = false;
    try {
        (void)PsxExeImage::parse(oversized);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "text extending beyond the file must be rejected");

    std::vector<std::uint8_t> outside_entry = bytes;
    write_le32(outside_entry, 0x10U, 0x00001008U);
    threw = false;
    try {
        (void)PsxExeImage::parse(outside_entry);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "an entry at the text end must be rejected");

    std::vector<std::uint8_t> wrapping = make_exe(
        0xfffffff0U, 0xfffffff0U, std::vector<std::uint8_t>(0x20U, 0U));
    threw = false;
    try {
        (void)PsxExeImage::parse(wrapping);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "a text range wrapping the guest address space must be rejected");
}

Region make_trampoline_region() {
    std::vector<std::uint8_t> text(0x24U, 0U);
    write_le32(text, 0x00U, encode_jump(0x00001000U, 0x00001010U));
    write_le32(text, 0x04U, 0U);
    write_le32(text, 0x10U, encode_jump(0x00001010U, 0x00001020U));
    write_le32(text, 0x14U, 0U);
    write_le32(text, 0x20U, 0U);

    const PsxExeImage image = make_image(0x00001000U, 0x00001000U, std::move(text));
    RegionBuildOptions options;
    options.entry_points = {0x00001000U};
    return RegionBuilder::build(image, std::move(options));
}

void test_unnamed_direct_path_preserves_targets() {
    const Region region = make_trampoline_region();
    require(region.blocks.size() == 3U,
            "A, trampoline, and continuation must become three explicit blocks");
    require(region.edges.size() == 2U, "the direct path must have two explicit edges");
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

    require(region.edges.size() == 3U,
            "conditional branch successors and the later block fallthrough must be explicit");
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
    require(source.find("state.pc = 0;") == std::string::npos,
            "emitter must not synthesize a zero PC for an unresolved name");
    require(source.find("step(CpuState& state)") != std::string::npos,
            "emitter must provide a small callable fixture stepper");
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
        output << clean_aot::compiler::CppEmitter::emit(make_trampoline_region());
        if (!output) {
            std::cerr << "unable to write emitter output\n";
            return 2;
        }
        std::cout << "EMITTED " << argv[2] << '\n';
        return 0;
    }

    const std::vector<std::pair<std::string_view, std::function<void()>>> tests = {
        {"checked_exe_ingestion", test_checked_exe_ingestion},
        {"unnamed_direct_path_preserves_targets", test_unnamed_direct_path_preserves_targets},
        {"branch_delay_slot_and_fallthrough", test_branch_delay_slot_and_fallthrough},
        {"backward_direct_edge_is_inside_the_region", test_backward_direct_edge_is_inside_the_region},
        {"emitter_contains_compilable_graph_shape", test_emitter_contains_compilable_graph_shape},
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
