#include "clean_aot/compiler/ir.hpp"

#include "clean_aot/compiler/mips_decoder.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <string>

namespace clean_aot::compiler {

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

    std::set<BlockId> ids;
    std::set<GuestAddress> starts;
    for (const BasicBlock& block : blocks) {
        if (!ids.insert(block.id).second) {
            throw IrError("basic block IDs must be unique");
        }
        if (!starts.insert(block.start).second) {
            throw IrError("basic block starts must be unique");
        }
        if (block.instructions.empty()) {
            throw IrError("basic blocks cannot be empty");
        }
        if (static_cast<std::uint64_t>(block.start) < static_cast<std::uint64_t>(start) ||
            static_cast<std::uint64_t>(block.start) >= end_exclusive) {
            throw IrError("basic block starts must lie inside the region");
        }
        if (block.terminator_index.has_value()) {
            const std::size_t index = *block.terminator_index;
            if (index >= block.instructions.size() ||
                !is_control_transfer(block.instructions[index].kind)) {
                throw IrError("terminator index does not identify a control transfer");
            }
            if (index + 1U >= block.instructions.size() ||
                block.instructions[index + 1U].address !=
                    block.instructions[index].address + 4U) {
                throw IrError("a control transfer must be followed by its delay slot");
            }
        }
        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            const Instruction& instruction = block.instructions[index];
            if (instruction.address != block.start + static_cast<GuestAddress>(index * 4U)) {
                throw IrError("basic block instructions must be contiguous");
            }
            if (index == block.instructions.size() - 1U &&
                is_control_transfer(instruction.kind)) {
                throw IrError("control transfer is missing its delay slot");
            }
        }
    }

    for (const Edge& edge : edges) {
        if (!ids.contains(edge.source)) {
            throw IrError("edge source does not identify a basic block");
        }

        const bool direct_kind = edge.kind == EdgeKind::Fallthrough ||
                                 edge.kind == EdgeKind::DirectJump ||
                                 edge.kind == EdgeKind::DirectCall ||
                                 edge.kind == EdgeKind::BranchTaken ||
                                 edge.kind == EdgeKind::BranchFallthrough;
        if (direct_kind && !edge.target.has_value()) {
            throw IrError("direct edge is missing its guest target");
        }
        if (edge.target_block.has_value()) {
            if (!ids.contains(*edge.target_block)) {
                throw IrError("edge target block does not exist");
            }
            const BasicBlock* target_block = block_by_id(*edge.target_block);
            if (target_block == nullptr || !edge.target.has_value() ||
                target_block->start != *edge.target) {
                throw IrError("edge target block does not match its guest target");
            }
        }
        if (edge.target.has_value() &&
            static_cast<std::uint64_t>(*edge.target) >= start &&
            static_cast<std::uint64_t>(*edge.target) < end_exclusive &&
            edge.kind != EdgeKind::IndirectJump && edge.kind != EdgeKind::IndirectCall &&
            !block_at(*edge.target)) {
            throw IrError("in-region direct edge target is not a basic block start");
        }
    }
}

} // namespace clean_aot::compiler
