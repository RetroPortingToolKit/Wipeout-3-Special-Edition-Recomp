#include "clean_aot/compiler/region_builder.hpp"

#include "clean_aot/compiler/mips_decoder.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace clean_aot::compiler {
namespace {

using InstructionMap = std::map<GuestAddress, Instruction>;

[[nodiscard]] bool valid_instruction_address(const PsxExeImage& image,
                                             GuestAddress address,
                                             std::uint64_t region_end) noexcept {
    return static_cast<std::uint64_t>(address) >=
               static_cast<std::uint64_t>(image.load_address()) &&
           static_cast<std::uint64_t>(address) + sizeof(std::uint32_t) <= region_end &&
           image.contains(address, sizeof(std::uint32_t));
}

[[nodiscard]] GuestAddress checked_plus(GuestAddress address, std::uint32_t amount) {
    const std::uint64_t result = static_cast<std::uint64_t>(address) + amount;
    if (result > 0xffffffffULL) {
        throw IrError("guest address arithmetic overflowed the 32-bit address space");
    }
    return static_cast<GuestAddress>(result);
}

void add_edge(Region& region,
              BlockId source,
              EdgeKind kind,
              std::optional<GuestAddress> target) {
    Edge edge;
    edge.source = source;
    edge.kind = kind;
    edge.target = target;
    if (target.has_value()) {
        const BasicBlock* block = region.block_at(*target);
        if (block != nullptr) {
            edge.target_block = block->id;
        }
    }
    region.edges.push_back(edge);
}

} // namespace

Region RegionBuilder::build(const PsxExeImage& image, RegionBuildOptions options) {
    const std::uint64_t region_end = options.end_exclusive.value_or(image.text_end_exclusive());
    const std::uint64_t image_begin = static_cast<std::uint64_t>(image.load_address());
    if (region_end <= image_begin || region_end > image.text_end_exclusive()) {
        throw IrError("region end must lie inside the PS-X EXE text image");
    }
    if (options.entry_points.empty()) {
        throw IrError("region requires at least one entry point");
    }

    std::sort(options.entry_points.begin(), options.entry_points.end());
    options.entry_points.erase(
        std::unique(options.entry_points.begin(), options.entry_points.end()),
        options.entry_points.end());
    for (const GuestAddress entry : options.entry_points) {
        if (!valid_instruction_address(image, entry, region_end)) {
            throw IrError("region entry point is outside the bounded text range");
        }
    }

    std::set<GuestAddress> leaders(options.entry_points.begin(), options.entry_points.end());
    std::set<GuestAddress> pending(options.entry_points.begin(), options.entry_points.end());
    std::set<GuestAddress> expanded_starts;
    std::set<GuestAddress> delay_slots;
    InstructionMap instructions;

    auto queue_target = [&](GuestAddress target) {
        if (static_cast<std::uint64_t>(target) >= image_begin &&
            valid_instruction_address(image, target, region_end)) {
            leaders.insert(target);
            pending.insert(target);
        }
    };

    while (!pending.empty()) {
        const GuestAddress start = *pending.begin();
        pending.erase(pending.begin());
        if (!expanded_starts.insert(start).second) {
            continue;
        }
        std::set<GuestAddress> path;
        GuestAddress pc = start;

        while (valid_instruction_address(image, pc, region_end)) {
            if (!path.insert(pc).second) {
                break;
            }

            auto [it, inserted] = instructions.try_emplace(
                pc, decode_instruction(pc, image.read_u32(pc)));
            const Instruction& instruction = it->second;
            if (instruction.has_delay_slot) {
                const GuestAddress delay_address = checked_plus(pc, 4U);
                if (!valid_instruction_address(image, delay_address, region_end)) {
                    throw IrError("control transfer has no bounded delay-slot instruction");
                }
                delay_slots.insert(delay_address);
                auto [delay_it, delay_inserted] = instructions.try_emplace(
                    delay_address, decode_instruction(delay_address, image.read_u32(delay_address)));
                if (is_control_transfer(delay_it->second.kind)) {
                    throw IrError("control transfer in a delay slot is not supported by this tranche");
                }
                (void)inserted;
                (void)delay_inserted;

                if (instruction.direct_target.has_value()) {
                    queue_target(*instruction.direct_target);
                }
                if (instruction.kind == InstructionKind::ConditionalBranch ||
                    instruction.kind == InstructionKind::DirectCall ||
                    instruction.kind == InstructionKind::IndirectCall) {
                    const GuestAddress fallthrough = checked_plus(pc, 8U);
                    queue_target(fallthrough);
                }
                break;
            }

            const GuestAddress next = checked_plus(pc, 4U);
            if (!valid_instruction_address(image, next, region_end)) {
                break;
            }
            pc = next;
        }
    }

    for (const GuestAddress leader : leaders) {
        if (delay_slots.contains(leader)) {
            throw IrError("region entry or direct edge lands in a delay slot");
        }
    }

    Region region;
    region.start = image.load_address();
    region.end_exclusive = region_end;
    region.entry_points = options.entry_points;

    std::set<GuestAddress> block_starts;
    for (const GuestAddress leader : leaders) {
        if (instructions.contains(leader)) {
            block_starts.insert(leader);
        }
    }

    std::map<GuestAddress, BlockId> block_ids;
    BlockId next_id = 0;
    for (const GuestAddress block_start : block_starts) {
        block_ids.emplace(block_start, next_id++);
    }

    std::set<GuestAddress> covered;
    for (const GuestAddress block_start : block_starts) {
        BasicBlock block;
        block.id = block_ids.at(block_start);
        block.start = block_start;
        GuestAddress pc = block_start;

        while (instructions.contains(pc) && !covered.contains(pc)) {
            if (pc != block_start && block_starts.contains(pc)) {
                break;
            }

            const Instruction& instruction = instructions.at(pc);
            covered.insert(pc);
            block.instructions.push_back(instruction);
            if (instruction.has_delay_slot) {
                const GuestAddress delay_address = checked_plus(pc, 4U);
                if (!instructions.contains(delay_address) || covered.contains(delay_address)) {
                    throw IrError("delay-slot ownership is ambiguous");
                }
                block.terminator_index = block.instructions.size() - 1U;
                block.instructions.push_back(instructions.at(delay_address));
                covered.insert(delay_address);
                break;
            }

            const GuestAddress next = checked_plus(pc, 4U);
            if (!instructions.contains(next) || block_starts.contains(next)) {
                break;
            }
            pc = next;
        }

        if (block.instructions.empty()) {
            throw IrError("unable to materialize a non-empty basic block");
        }
        region.blocks.push_back(std::move(block));
    }

    if (covered.size() != instructions.size()) {
        throw IrError("reachable instructions were not assigned to a basic block");
    }

    for (const BasicBlock& block : region.blocks) {
        if (block.terminator_index.has_value()) {
            const Instruction& terminator = block.instructions[*block.terminator_index];
            switch (terminator.kind) {
            case InstructionKind::DirectJump:
                add_edge(region, block.id, EdgeKind::DirectJump, terminator.direct_target);
                break;
            case InstructionKind::DirectCall:
                add_edge(region, block.id, EdgeKind::DirectCall, terminator.direct_target);
                add_edge(region,
                         block.id,
                         EdgeKind::Fallthrough,
                         checked_plus(terminator.address, 8U));
                break;
            case InstructionKind::ConditionalBranch:
                add_edge(region,
                         block.id,
                         EdgeKind::BranchTaken,
                         terminator.direct_target);
                add_edge(region,
                         block.id,
                         EdgeKind::BranchFallthrough,
                         checked_plus(terminator.address, 8U));
                break;
            case InstructionKind::IndirectCall:
                add_edge(region, block.id, EdgeKind::IndirectCall, std::nullopt);
                add_edge(region,
                         block.id,
                         EdgeKind::Fallthrough,
                         checked_plus(terminator.address, 8U));
                break;
            case InstructionKind::IndirectJump:
                add_edge(region, block.id, EdgeKind::IndirectJump, std::nullopt);
                break;
            case InstructionKind::Return:
                break;
            case InstructionKind::Other:
            case InstructionKind::Nop:
                throw IrError("terminator index names a non-control instruction");
            }
        } else {
            const GuestAddress next = checked_plus(
                block.instructions.back().address, 4U);
            const auto instruction_it = instructions.find(next);
            if (instruction_it != instructions.end()) {
                add_edge(region, block.id, EdgeKind::Fallthrough, next);
            }
        }
    }

    for (Edge& edge : region.edges) {
        if (edge.target.has_value()) {
            const auto block_it = block_ids.find(*edge.target);
            if (block_it != block_ids.end()) {
                edge.target_block = block_it->second;
            }
        }
    }

    region.validate();
    return region;
}

} // namespace clean_aot::compiler
