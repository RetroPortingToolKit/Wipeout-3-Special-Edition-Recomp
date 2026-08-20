#include "clean_aot/compiler/ir.hpp"

#include "clean_aot/compiler/mips_decoder.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace clean_aot::compiler {
namespace {

constexpr std::uint64_t kGuestAddressSpaceSize =
    static_cast<std::uint64_t>(std::numeric_limits<GuestAddress>::max()) + 1ULL;

[[nodiscard]] GuestAddress next_pc(GuestAddress address, std::uint32_t amount) noexcept {
    return static_cast<GuestAddress>(address + amount);
}

[[nodiscard]] bool is_direct_edge(EdgeKind kind) noexcept {
    switch (kind) {
    case EdgeKind::Fallthrough:
    case EdgeKind::DirectJump:
    case EdgeKind::DirectCall:
    case EdgeKind::BranchTaken:
    case EdgeKind::BranchFallthrough:
        return true;
    case EdgeKind::IndirectJump:
    case EdgeKind::IndirectCall:
    case EdgeKind::Return:
        return false;
    }
    return false;
}

} // namespace

const BasicBlock* Region::block_at(GuestAddress address) const noexcept {
    for (const BasicBlock& block : blocks) {
        if (block.start == address) {
            return &block;
        }
    }
    return nullptr;
}

const BasicBlock* Region::block_by_id(BlockId id) const noexcept {
    for (const BasicBlock& block : blocks) {
        if (block.id == id) {
            return &block;
        }
    }
    return nullptr;
}

void Region::validate() const {
    if (blocks.empty()) {
        throw IrError("region must contain at least one basic block");
    }
    if (start >= end_exclusive) {
        throw IrError("region bounds are inverted");
    }
    if (end_exclusive > kGuestAddressSpaceSize) {
        throw IrError("region end exceeds the guest address space");
    }
    if ((start % sizeof(std::uint32_t)) != 0U ||
        (end_exclusive % sizeof(std::uint32_t)) != 0U) {
        throw IrError("region bounds must be instruction aligned");
    }
    if (entry_points.empty()) {
        throw IrError("region must retain at least one entry point");
    }

    std::map<BlockId, const BasicBlock*> blocks_by_id;
    std::map<GuestAddress, const BasicBlock*> blocks_by_start;
    std::set<GuestAddress> instruction_addresses;
    std::map<BlockId, std::vector<const Edge*>> outgoing;
    for (const BasicBlock& block : blocks) {
        if (!blocks_by_id.emplace(block.id, &block).second) {
            throw IrError("basic block IDs must be unique");
        }
        if (!blocks_by_start.emplace(block.start, &block).second) {
            throw IrError("basic block starts must be unique");
        }
        if (block.instructions.empty()) {
            throw IrError("basic blocks cannot be empty");
        }
        if ((block.start % sizeof(std::uint32_t)) != 0U) {
            throw IrError("basic block starts must be instruction aligned");
        }
        if (static_cast<std::uint64_t>(block.start) < static_cast<std::uint64_t>(start) ||
            static_cast<std::uint64_t>(block.start) >= end_exclusive) {
            throw IrError("basic block starts must lie inside the region");
        }
        if (block.instructions.front().address != block.start) {
            throw IrError("a basic block must begin at its declared start");
        }
        if (block.terminator_index.has_value()) {
            const std::size_t index = *block.terminator_index;
            if (index >= block.instructions.size() ||
                !is_control_transfer(block.instructions[index].kind)) {
                throw IrError("terminator index does not identify a control transfer");
            }
            if (!block.instructions[index].has_delay_slot) {
                throw IrError("terminator does not claim its architectural delay slot");
            }
            if (index + 2U != block.instructions.size()) {
                throw IrError("a delay slot must be the final instruction in its block");
            }
            if (is_control_transfer(block.instructions[index + 1U].kind) ||
                block.instructions[index + 1U].has_delay_slot) {
                throw IrError("a control transfer in a delay slot is unsupported");
            }
        }
        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            const Instruction& instruction = block.instructions[index];
            const std::uint64_t maximum_index =
                (static_cast<std::uint64_t>(std::numeric_limits<GuestAddress>::max()) -
                 static_cast<std::uint64_t>(block.start)) /
                sizeof(std::uint32_t);
            if (static_cast<std::uint64_t>(index) > maximum_index) {
                throw IrError("basic block instruction addresses wrap the guest space");
            }
            const std::uint64_t expected = static_cast<std::uint64_t>(block.start) +
                                           static_cast<std::uint64_t>(index) *
                                               sizeof(std::uint32_t);
            if (static_cast<std::uint64_t>(instruction.address) != expected) {
                throw IrError("basic block instructions must be contiguous");
            }
            if (expected < static_cast<std::uint64_t>(start) ||
                expected + sizeof(std::uint32_t) > end_exclusive) {
                throw IrError("basic block instruction lies outside the region");
            }
            if (!instruction_addresses.insert(instruction.address).second) {
                throw IrError("guest instructions cannot belong to multiple blocks");
            }

            const bool control_transfer = is_control_transfer(instruction.kind);
            if (control_transfer != instruction.has_delay_slot) {
                throw IrError("control-transfer and delay-slot metadata disagree");
            }
            if (control_transfer &&
                (!block.terminator_index.has_value() ||
                 *block.terminator_index != index)) {
                throw IrError("every control transfer must be the block terminator");
            }
        }
        outgoing.emplace(block.id, std::vector<const Edge*>{});
    }

    std::set<GuestAddress> entries;
    for (const GuestAddress entry : entry_points) {
        if (!entries.insert(entry).second) {
            throw IrError("region entry points must be unique");
        }
        if ((entry % sizeof(std::uint32_t)) != 0U ||
            !blocks_by_start.contains(entry)) {
            throw IrError("every region entry point must identify an aligned block start");
        }
    }

    for (const Edge& edge : edges) {
        if (!blocks_by_id.contains(edge.source)) {
            throw IrError("edge source does not identify a basic block");
        }

        if (is_direct_edge(edge.kind) && !edge.target.has_value()) {
            throw IrError("direct edge is missing its guest target");
        }
        if (!is_direct_edge(edge.kind) && edge.target.has_value()) {
            throw IrError("dynamic edge must not claim a static guest target");
        }
        if (edge.target.has_value() &&
            (*edge.target % sizeof(std::uint32_t)) != 0U) {
            throw IrError("direct edge targets must be instruction aligned");
        }

        const auto resolved_iterator = edge.target.has_value()
                                           ? blocks_by_start.find(*edge.target)
                                           : blocks_by_start.end();
        const BasicBlock* resolved_block =
            resolved_iterator == blocks_by_start.end() ? nullptr
                                                       : resolved_iterator->second;
        if (edge.target_block.has_value()) {
            const auto target_iterator = blocks_by_id.find(*edge.target_block);
            if (target_iterator == blocks_by_id.end()) {
                throw IrError("edge target block does not exist");
            }
            const BasicBlock* target_block = target_iterator->second;
            if (target_block == nullptr || !edge.target.has_value() ||
                target_block->start != *edge.target) {
                throw IrError("edge target block does not match its guest target");
            }
        } else if (resolved_block != nullptr) {
            throw IrError("an internal direct edge must resolve its target block");
        }
        if (edge.target.has_value() &&
            static_cast<std::uint64_t>(*edge.target) >= start &&
            static_cast<std::uint64_t>(*edge.target) < end_exclusive &&
            resolved_block == nullptr) {
            throw IrError("in-region direct edge target is not a basic block start");
        }
        outgoing.at(edge.source).push_back(&edge);
    }

    auto require_edge = [](const std::vector<const Edge*>& block_edges,
                           EdgeKind kind,
                           std::optional<GuestAddress> target) {
        const std::size_t count = static_cast<std::size_t>(std::count_if(
            block_edges.begin(), block_edges.end(), [&](const Edge* edge) {
                return edge->kind == kind && edge->target == target;
            }));
        if (count != 1U) {
            throw IrError("basic block is missing an exact architectural edge");
        }
    };

    for (const BasicBlock& block : blocks) {
        const std::vector<const Edge*>& block_edges = outgoing.at(block.id);
        if (!block.terminator_index.has_value()) {
            if (block_edges.size() != 1U) {
                throw IrError("straight-line block must have one explicit fallthrough");
            }
            require_edge(block_edges,
                         EdgeKind::Fallthrough,
                         next_pc(block.instructions.back().address, 4U));
            continue;
        }

        const Instruction& terminator = block.instructions[*block.terminator_index];
        switch (terminator.kind) {
        case InstructionKind::DirectJump:
            if (block_edges.size() != 1U || !terminator.direct_target.has_value()) {
                throw IrError("direct jump has an invalid edge shape");
            }
            require_edge(block_edges, EdgeKind::DirectJump, terminator.direct_target);
            break;
        case InstructionKind::DirectCall:
            if (block_edges.size() != 2U || !terminator.direct_target.has_value()) {
                throw IrError("direct call has an invalid edge shape");
            }
            require_edge(block_edges, EdgeKind::DirectCall, terminator.direct_target);
            require_edge(block_edges,
                         EdgeKind::Fallthrough,
                         next_pc(terminator.address, 8U));
            break;
        case InstructionKind::ConditionalBranch:
        case InstructionKind::ConditionalCall:
            if (block_edges.size() != 2U || !terminator.direct_target.has_value()) {
                throw IrError("conditional transfer has an invalid edge shape");
            }
            require_edge(block_edges, EdgeKind::BranchTaken, terminator.direct_target);
            require_edge(block_edges,
                         EdgeKind::BranchFallthrough,
                         next_pc(terminator.address, 8U));
            break;
        case InstructionKind::IndirectJump:
            if (block_edges.size() != 1U) {
                throw IrError("indirect jump has an invalid edge shape");
            }
            require_edge(block_edges, EdgeKind::IndirectJump, std::nullopt);
            break;
        case InstructionKind::IndirectCall:
            if (block_edges.size() != 2U) {
                throw IrError("indirect call has an invalid edge shape");
            }
            require_edge(block_edges, EdgeKind::IndirectCall, std::nullopt);
            require_edge(block_edges,
                         EdgeKind::Fallthrough,
                         next_pc(terminator.address, 8U));
            break;
        case InstructionKind::Return:
            if (block_edges.size() != 1U) {
                throw IrError("return has an invalid edge shape");
            }
            require_edge(block_edges, EdgeKind::Return, std::nullopt);
            break;
        case InstructionKind::Other:
        case InstructionKind::Nop:
            throw IrError("terminator index names a non-control instruction");
        }
    }
}

} // namespace clean_aot::compiler
