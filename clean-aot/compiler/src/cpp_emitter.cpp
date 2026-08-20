#include "clean_aot/compiler/cpp_emitter.hpp"

#include <array>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>

namespace clean_aot::compiler {
namespace {

[[nodiscard]] const char* edge_kind_name(EdgeKind kind) noexcept {
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
    }
    return "Fallthrough";
}

void write_address(std::ostringstream& output, GuestAddress address) {
    output << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
           << address << "u" << std::nouppercase << std::dec << std::setfill(' ');
}

} // namespace

std::string CppEmitter::emit(const Region& region) {
    region.validate();

    std::map<BlockId, std::vector<const Edge*>> outgoing;
    for (const BasicBlock& block : region.blocks) {
        outgoing.emplace(block.id, std::vector<const Edge*>{});
    }
    for (const Edge& edge : region.edges) {
        outgoing.at(edge.source).push_back(&edge);
    }

    std::ostringstream output;
    output << "#include <array>\n"
           << "#include <cstdint>\n"
           << "#include <optional>\n\n"
           << "namespace clean_aot_generated {\n\n"
           << "enum class EdgeKind : std::uint8_t {\n"
           << "    Fallthrough, DirectJump, DirectCall, BranchTaken,\n"
           << "    BranchFallthrough, IndirectJump, IndirectCall\n"
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

    for (const Edge& edge : region.edges) {
        output << "    {";
        const BasicBlock* source = region.block_by_id(edge.source);
        if (source == nullptr) {
            throw IrError("emitter found an edge with no source block");
        }
        write_address(output, source->start);
        output << ", ";
        if (edge.target.has_value()) {
            write_address(output, *edge.target);
        } else {
            output << "std::nullopt";
        }
        output << ", EdgeKind::" << edge_kind_name(edge.kind) << "},\n";
    }
    output << "}};\n\n"
           << "// Returns true when a resolved edge advanced the guest PC.\n"
           << "// An unresolved indirect edge leaves the caller's PC untouched.\n"
           << "inline bool step(CpuState& state) {\n"
           << "    switch (state.pc) {\n";

    for (const BasicBlock& block : region.blocks) {
        output << "    case ";
        write_address(output, block.start);
        output << ": {\n";
        const auto& edges = outgoing.at(block.id);
        if (edges.size() == 1U && edges.front()->target.has_value()) {
            output << "        state.pc = ";
            write_address(output, *edges.front()->target);
            output << ";\n        return true;\n";
        } else if (edges.size() == 2U && edges[0]->target.has_value() &&
                   edges[1]->target.has_value() &&
                   edges[0]->kind == EdgeKind::BranchTaken &&
                   edges[1]->kind == EdgeKind::BranchFallthrough) {
            output << "        state.pc = state.branch_taken ? ";
            write_address(output, *edges[0]->target);
            output << " : ";
            write_address(output, *edges[1]->target);
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
