#include "clean_aot/compiler/cpp_emitter.hpp"
#include "clean_aot/compiler/mips_decoder.hpp"

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

void write_semantic(std::ostringstream& out, const Instruction& instruction) {
    const auto raw=instruction.raw, op=raw>>26U, rs=(raw>>21U)&31U, rt=(raw>>16U)&31U,
               rd=(raw>>11U)&31U, fn=raw&63U; const auto imm=raw&0xffffU;
    if(raw==0U)return;
    if(op==0U&&fn==0x21U)out<<"        cpu_ops::addu(state,"<<rd<<","<<rs<<","<<rt<<");\n";
    else if(op==0U&&fn==0x23U)out<<"        cpu_ops::subu(state,"<<rd<<","<<rs<<","<<rt<<");\n";
    else if(op==0U&&fn==0x25U)out<<"        cpu_ops::bit_or(state,"<<rd<<","<<rs<<","<<rt<<");\n";
    else if(op==0x08U)out<<"        cpu_ops::add(state,"<<rt<<",state.gpr["<<rs<<"],0x"<<std::hex<<static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(imm)))<<"u);\n"<<std::dec;
    else if(op==0x09U)out<<"        state.write("<<rt<<", state.gpr["<<rs<<"] + 0x"<<std::hex<<static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(imm)))<<"u);\n"<<std::dec;
    else if(op==0x0dU)out<<"        state.write("<<rt<<", state.gpr["<<rs<<"] | 0x"<<std::hex<<imm<<"u);\n"<<std::dec;
    else if(op==0x0fU)out<<"        state.write("<<rt<<", 0x"<<std::hex<<(imm<<16U)<<"u);\n"<<std::dec;
    else if(op==0x20U||op==0x24U)out<<"        cpu_ops::lb(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u),"<<(op==0x24U?"true":"false")<<");\n"<<std::dec;
    else if(op==0x21U||op==0x25U)out<<"        cpu_ops::lh(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u),"<<(op==0x25U?"true":"false")<<");\n"<<std::dec;
    else if(op==0x22U)out<<"        cpu_ops::lwl(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u));\n"<<std::dec;
    else if(op==0x23U)out<<"        cpu_ops::lw(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u));\n"<<std::dec;
    else if(op==0x26U)out<<"        cpu_ops::lwr(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u));\n"<<std::dec;
    else if(op==0x28U)out<<"        cpu_ops::sb(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u));\n"<<std::dec;
    else if(op==0x29U)out<<"        cpu_ops::sh(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u));\n"<<std::dec;
    else if(op==0x2aU)out<<"        cpu_ops::swl(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u));\n"<<std::dec;
    else if(op==0x2bU)out<<"        cpu_ops::sw(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u));\n"<<std::dec;
    else if(op==0x2eU)out<<"        cpu_ops::swr(state,"<<rs<<","<<rt<<",static_cast<std::int16_t>(0x"<<std::hex<<imm<<"u));\n"<<std::dec;
    else if(op==0x10U&&rs==0U)out<<"        cpu_ops::mfc0(state,"<<rt<<","<<rd<<");\n";
    else if(op==0x10U&&rs==4U)out<<"        cpu_ops::mtc0(state,"<<rt<<","<<rd<<");\n";
    else if(raw==0x42000010U)out<<"        cpu_ops::rfe(state);\n";
    else if(!instruction.has_delay_slot)out<<"        cpu_ops::raise_exception(state, CpuException::ReservedInstruction);\n";
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
           << "#include <optional>\n#include <clean_aot/runtime/generated_cpu.hpp>\n\n"
           << "namespace clean_aot_generated {\nusing namespace clean_aot::runtime;\n\n"
           << "enum class EdgeKind : std::uint8_t {\n"
           << "    Fallthrough, DirectJump, DirectCall, BranchTaken,\n"
           << "    BranchFallthrough, IndirectJump, IndirectCall, Return\n"
           << "};\n\n"
           << "struct Edge final {\n"
           << "    std::uint32_t source;\n"
           << "    std::optional<std::uint32_t> target;\n"
           << "    EdgeKind kind;\n"
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
           << "extern \"C\" bool clean_aot_region_"
           << std::hex << std::setw(8) << std::setfill('0') << region.start
           << std::dec << std::setfill(' ')
           << "(GeneratedCpuState& state) noexcept {\n"
           << "    switch (state.pc) {\n";

    for (const BasicBlock* block : ordered_blocks) {
        output << "    case ";
        write_address(output, block->start);
        output << ": {\n";
        output << "        state.fault = CpuFault::None;\n";
        for (std::size_t instruction_index = 0; instruction_index < block->instructions.size(); ++instruction_index) {
            const Instruction& instruction = block->instructions[instruction_index];
            const bool delay = block->terminator_index.has_value() && instruction_index == *block->terminator_index + 1U;
            output << "        cpu_ops::begin(state, ";
            write_address(output, instruction.address);
            output << ", " << (delay ? "true" : "false") << ", ";
            if (delay) write_address(output, block->instructions[*block->terminator_index].address);
            else output << "0u";
            output << ");\n";
            if (!is_control_transfer(instruction.kind)) write_semantic(output, instruction);
            output << "        cpu_ops::end(state);\n";
            output << "        if (state.fault != CpuFault::None) return false;\n";
            output << "        if (state.exception_raised) return true;\n";
        }
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
            const std::uint32_t raw = block->instructions[*block->terminator_index].raw;
            const std::uint32_t opcode = raw >> 26U;
            const std::uint32_t rs = (raw >> 21U) & 31U;
            const std::uint32_t rt = (raw >> 16U) & 31U;
            output << "        state.branch_taken = ";
            if (opcode == 0x04U) output << "state.gpr["<<rs<<"] == state.gpr["<<rt<<"]";
            else if (opcode == 0x05U) output << "state.gpr["<<rs<<"] != state.gpr["<<rt<<"]";
            else if (opcode == 0x06U) output << "cpu_ops::s(state.gpr["<<rs<<"]) <= 0";
            else if (opcode == 0x07U) output << "cpu_ops::s(state.gpr["<<rs<<"]) > 0";
            else if (opcode == 0x01U && (((raw >> 16U) & 1U) == 0U)) output << "cpu_ops::s(state.gpr["<<rs<<"]) < 0";
            else if (opcode == 0x01U) output << "cpu_ops::s(state.gpr["<<rs<<"]) >= 0";
            else output << "false";
            output << ";\n        state.pc = state.branch_taken ? ";
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
