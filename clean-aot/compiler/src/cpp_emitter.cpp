#include "clean_aot/compiler/cpp_emitter.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>
#include <vector>

namespace clean_aot::compiler {
namespace {

[[nodiscard]] const char* edge_kind_name(EdgeKind kind) {
    switch (kind) {
    case EdgeKind::Fallthrough:
        return "Fallthrough";
    case EdgeKind::DirectJump:
        return "DirectJump";
    case EdgeKind::DirectCall:
        return "DirectCall";
    case EdgeKind::BranchTaken:
        return "BranchTaken";
    case EdgeKind::BranchFallthrough:
        return "BranchFallthrough";
    case EdgeKind::IndirectJump:
        return "IndirectJump";
    case EdgeKind::IndirectCall:
        return "IndirectCall";
    case EdgeKind::Return:
        return "Return";
    }
    throw IrError("emitter encountered an unknown edge kind");
}

void write_address(std::ostringstream& output, GuestAddress address) {
    output << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
           << address << "u" << std::nouppercase << std::dec << std::setfill(' ');
}

} // namespace

std::string CppEmitter::emit(const Region& region) {
    region.validate();

    std::vector<const BasicBlock*> ordered_blocks;
    std::map<BlockId, const BasicBlock*> blocks_by_id;
    ordered_blocks.reserve(region.blocks.size());
    for (const BasicBlock& block : region.blocks) {
        ordered_blocks.push_back(&block);
        blocks_by_id.emplace(block.id, &block);
    }
    std::sort(ordered_blocks.begin(), ordered_blocks.end(),
              [](const BasicBlock* left, const BasicBlock* right) {
                  if (left->start != right->start) {
                      return left->start < right->start;
                  }
                  return left->id < right->id;
              });

    std::vector<const Edge*> ordered_edges;
    ordered_edges.reserve(region.edges.size());
    for (const Edge& edge : region.edges) {
        ordered_edges.push_back(&edge);
    }
    std::sort(ordered_edges.begin(), ordered_edges.end(),
              [&](const Edge* left, const Edge* right) {
                  const BasicBlock* left_source = blocks_by_id.at(left->source);
                  const BasicBlock* right_source = blocks_by_id.at(right->source);
                  if (left_source->start != right_source->start) {
                      return left_source->start < right_source->start;
                  }
                  if (left->kind != right->kind) {
                      return left->kind < right->kind;
                  }
                  if (left->target != right->target) {
                      return left->target < right->target;
                  }
                  return left->target_block < right->target_block;
              });

    std::map<BlockId, std::vector<const Edge*>> outgoing;
    for (const BasicBlock& block : region.blocks) {
        outgoing.emplace(block.id, std::vector<const Edge*>{});
    }
    for (const Edge* edge : ordered_edges) {
        outgoing.at(edge->source).push_back(edge);
    }

    std::ostringstream output;
    output << "#include <array>\n"
           << "#include <cstdint>\n"
           << "#include <optional>\n\n"
           << "namespace clean_aot_generated {\n\n"
           << "enum class EdgeKind : std::uint8_t {\n"
           << "    Fallthrough, DirectJump, DirectCall, BranchTaken,\n"
           << "    BranchFallthrough, IndirectJump, IndirectCall, Return\n"
           << "};\n\n"
           << "struct Edge final {\n"
           << "    std::uint32_t source;\n"
           << "    std::optional<std::uint32_t> target;\n"
           << "    EdgeKind kind;\n"
           << "};\n\n"
           << "struct CpuState final {\n"
           << "    std::uint32_t pc;\n"
           << "    bool branch_taken;\n"
           << "};\n\n"
           << "inline constexpr std::array<Edge, " << region.edges.size() << "> kEdges = {{\n";

    for (const Edge* edge : ordered_edges) {
        output << "    {";
        const BasicBlock* source = blocks_by_id.at(edge->source);
        write_address(output, source->start);
        output << ", ";
        if (edge->target.has_value()) {
            write_address(output, *edge->target);
        } else {
            output << "std::nullopt";
        }
        output << ", EdgeKind::" << edge_kind_name(edge->kind) << "},\n";
    }
    output << "}};\n\n"
           << "// Returns true when a resolved edge advanced the guest PC.\n"
           << "// An unresolved indirect edge leaves the caller's PC untouched.\n"
           << "inline bool step(CpuState& state) {\n"
           << "    switch (state.pc) {\n";

    for (const BasicBlock* block : ordered_blocks) {
        output << "    case ";
        write_address(output, block->start);
        output << ": {\n";
        const auto& edges = outgoing.at(block->id);
        const auto find_edge = [&](EdgeKind kind) -> const Edge* {
            const auto iterator = std::find_if(
                edges.begin(), edges.end(),
                [kind](const Edge* edge) { return edge->kind == kind; });
            return iterator == edges.end() ? nullptr : *iterator;
        };

        const Edge* resolved = nullptr;
        if (!block->terminator_index.has_value()) {
            resolved = find_edge(EdgeKind::Fallthrough);
        } else {
            const InstructionKind kind =
                block->instructions[*block->terminator_index].kind;
            if (kind == InstructionKind::DirectJump) {
                resolved = find_edge(EdgeKind::DirectJump);
            } else if (kind == InstructionKind::DirectCall) {
                resolved = find_edge(EdgeKind::DirectCall);
            }
        }

        if (resolved != nullptr && resolved->target.has_value()) {
            output << "        state.pc = ";
            write_address(output, *resolved->target);
            output << ";\n        return true;\n";
        } else if (block->terminator_index.has_value() &&
                   (block->instructions[*block->terminator_index].kind ==
                        InstructionKind::ConditionalBranch ||
                    block->instructions[*block->terminator_index].kind ==
                        InstructionKind::ConditionalCall)) {
            const Edge* taken = find_edge(EdgeKind::BranchTaken);
            const Edge* fallthrough = find_edge(EdgeKind::BranchFallthrough);
            if (taken == nullptr || fallthrough == nullptr ||
                !taken->target.has_value() || !fallthrough->target.has_value()) {
                throw IrError("emitter found an invalid conditional edge shape");
            }
            output << "        state.pc = state.branch_taken ? ";
            write_address(output, *taken->target);
            output << " : ";
            write_address(output, *fallthrough->target);
            output << ";\n        return true;\n";
        } else {
            output << "        return false;\n";
        }
        output << "    }\n";
    }

    output << "    default:\n"
           << "        return false;\n"
           << "    }\n"
           << "}\n\n"
           << "} // namespace clean_aot_generated\n";
    return output.str();
}

} // namespace clean_aot::compiler
