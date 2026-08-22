#include "clean_aot/runtime/generated_cpu.hpp"
#include <cstdint>
extern "C" bool clean_aot_region_00001000(clean_aot::runtime::GeneratedCpuState&) noexcept;
namespace { struct Memory{std::uint8_t bytes[8]{0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};};
bool read32(void* p,std::uint32_t a,std::uint32_t&v)noexcept{if(a<0x1000U||a>0x1004U)return false;auto&m=*static_cast<Memory*>(p);auto i=a-0x1000U;v=std::uint32_t(m.bytes[i])|(std::uint32_t(m.bytes[i+1])<<8U)|(std::uint32_t(m.bytes[i+2])<<16U)|(std::uint32_t(m.bytes[i+3])<<24U);return true;} }
int main(){clean_aot::runtime::GeneratedCpuState state{};state.pc=0x1000U;
 if(!clean_aot_region_00001000(state)||state.pc!=0x1010U||state.gpr[1]!=1U)return 1;
 if(!clean_aot_region_00001000(state)||state.pc!=0x1020U||state.gpr[1]!=2U)return 2;
 if(!clean_aot_region_00001000(state)||state.pc!=0x1030U||state.gpr[2]!=0x1234U)return 3;
 Memory memory;state.pc=0x1030U;state.gpr[1]=0x1001U;state.gpr[2]=0xaabbccddU;state.memory_context=&memory;state.read32=read32;
 if(!clean_aot_region_00001000(state)||state.pc!=0x103cU||state.gpr[2]!=0x55443322U)return 4;
 return clean_aot_region_00001000(state)?5:0;}
