#include "clean_aot/runtime/generated_cpu.hpp"
#include <cstdint>
extern "C" bool clean_aot_region_00001000(clean_aot::runtime::GeneratedCpuState&) noexcept;
int main(){clean_aot::runtime::GeneratedCpuState state{};state.pc=0x1000U;
 if(!clean_aot_region_00001000(state)||state.pc!=0x1010U||state.gpr[1]!=1U)return 1;
 if(!clean_aot_region_00001000(state)||state.pc!=0x1020U||state.gpr[1]!=2U)return 2;
 if(!clean_aot_region_00001000(state)||state.pc!=0x1024U||state.gpr[2]!=0x1234U)return 3;
 return clean_aot_region_00001000(state)?4:0;}
