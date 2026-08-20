#pragma once

#include "clean_aot/compiler/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace clean_aot::compiler {

class IrError final : public std::runtime_error {
public:
    explicit IrError(const std::string& message)
        : std::runtime_error(message) {}
};

enum class InstructionKind : std::uint8_t {
    Other,
    Nop,
    DirectJump,
    DirectCall,
    ConditionalBranch,
    ConditionalCall,
    IndirectJump,
    IndirectCall,
    Return,
};

struct Instruction final {
    GuestAddress address = 0;
    std::uint32_t raw = 0;
    InstructionKind kind = InstructionKind::Other;
    bool has_delay_slot = false;
    std::optional<GuestAddress> direct_target;
};

enum class EdgeKind : std::uint8_t {
    Fallthrough,
    DirectJump,
    DirectCall,
    BranchTaken,
    BranchFallthrough,
    IndirectJump,
    IndirectCall,
    Return,
};

struct Edge final {
    BlockId source = 0;
    EdgeKind kind = EdgeKind::Fallthrough;
    std::optional<GuestAddress> target;
    std::optional<BlockId> target_block;
};

struct BasicBlock final {
    BlockId id = 0;
    GuestAddress start = 0;
    std::vector<Instruction> instructions;
    // The instruction at this index owns the delay slot, if any. The delay
    // slot is the following instruction in the block.
    std::optional<std::size_t> terminator_index;
};

struct Region final {
    GuestAddress start = 0;
    std::uint64_t end_exclusive = 0;
    std::vector<GuestAddress> entry_points;
    std::vector<BasicBlock> blocks;
    std::vector<Edge> edges;

    [[nodiscard]] const BasicBlock* block_at(GuestAddress address) const noexcept;
    [[nodiscard]] const BasicBlock* block_by_id(BlockId id) const noexcept;

    // Verifies structural invariants used by the emitter and downstream
    // passes. It intentionally does not infer missing blocks or edges.
    void validate() const;
};

} // namespace clean_aot::compiler
