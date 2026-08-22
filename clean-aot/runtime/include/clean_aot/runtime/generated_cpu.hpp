#pragma once
#include <array>
#include <cstdint>
#include <limits>
namespace clean_aot::runtime {
enum class CpuFault : std::uint8_t { None, Overflow, ReservedInstruction, UnknownTarget };
struct GeneratedCpuState final {
    std::array<std::uint32_t,32> gpr{}; std::uint32_t hi=0,lo=0,pc=0,next_pc=4;
    bool branch_taken=false; CpuFault fault=CpuFault::None;
    void write(std::uint8_t r,std::uint32_t v) noexcept { if(r) gpr[r]=v; gpr[0]=0; }
};
namespace cpu_ops {
inline std::int32_t s(std::uint32_t v) noexcept { return static_cast<std::int32_t>(v); }
inline void addu(GeneratedCpuState& c,std::uint8_t d,std::uint8_t a,std::uint8_t b) noexcept{c.write(d,c.gpr[a]+c.gpr[b]);}
inline void subu(GeneratedCpuState& c,std::uint8_t d,std::uint8_t a,std::uint8_t b) noexcept{c.write(d,c.gpr[a]-c.gpr[b]);}
inline void logic(GeneratedCpuState& c,std::uint8_t d,std::uint32_t v) noexcept{c.write(d,v);}
inline void slt(GeneratedCpuState& c,std::uint8_t d,std::uint8_t a,std::uint8_t b,bool uns) noexcept{c.write(d,uns?c.gpr[a]<c.gpr[b]:s(c.gpr[a])<s(c.gpr[b]));}
inline bool add_checked(std::uint32_t a,std::uint32_t b,std::uint32_t& out) noexcept{out=a+b;return ((~(a^b)&(a^out))>>31U)==0U;}
inline void add(GeneratedCpuState& c,std::uint8_t d,std::uint32_t a,std::uint32_t b) noexcept{std::uint32_t v; if(add_checked(a,b,v))c.write(d,v);else c.fault=CpuFault::Overflow;}
inline void sub(GeneratedCpuState& c,std::uint8_t d,std::uint32_t a,std::uint32_t b) noexcept{std::uint32_t v; if(add_checked(a,~b+1U,v))c.write(d,v);else c.fault=CpuFault::Overflow;}
inline std::uint32_t sar(std::uint32_t v,unsigned n) noexcept{return n?((v>>n)|(s(v)<0?(~0U<<(32U-n)):0U)):v;}
inline void mult(GeneratedCpuState& c,std::uint32_t a,std::uint32_t b,bool uns) noexcept{if(uns){auto v=std::uint64_t(a)*b;c.lo=std::uint32_t(v);c.hi=std::uint32_t(v>>32U);}else{auto v=std::int64_t(s(a))*s(b);auto u=static_cast<std::uint64_t>(v);c.lo=std::uint32_t(u);c.hi=std::uint32_t(u>>32U);}}
inline void div(GeneratedCpuState& c,std::uint32_t a,std::uint32_t b,bool uns) noexcept{if(!b){c.hi=a;c.lo=uns?~0U:(s(a)<0?1U:~0U);return;}if(!uns&&a==0x80000000U&&b==~0U){c.lo=a;c.hi=0;return;}if(uns){c.lo=a/b;c.hi=a%b;}else{c.lo=std::uint32_t(s(a)/s(b));c.hi=std::uint32_t(s(a)%s(b));}}
}
}
