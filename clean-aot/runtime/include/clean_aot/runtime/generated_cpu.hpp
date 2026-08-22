#pragma once
#include <array>
#include <cstdint>
#include <limits>
namespace clean_aot::runtime {
enum class CpuFault : std::uint8_t { None, Overflow, ReservedInstruction, UnknownTarget, MemoryAccess };
struct GeneratedCpuState final {
    using Read8 = bool (*)(void*,std::uint32_t,std::uint8_t&) noexcept;
    using Read16 = bool (*)(void*,std::uint32_t,std::uint16_t&) noexcept;
    using Read32 = bool (*)(void*,std::uint32_t,std::uint32_t&) noexcept;
    using Write8 = bool (*)(void*,std::uint32_t,std::uint8_t) noexcept;
    using Write16 = bool (*)(void*,std::uint32_t,std::uint16_t) noexcept;
    using Write32 = bool (*)(void*,std::uint32_t,std::uint32_t) noexcept;
    std::array<std::uint32_t,32> gpr{}; std::uint32_t hi=0,lo=0,pc=0,next_pc=4;
    bool branch_taken=false; CpuFault fault=CpuFault::None;
    bool load_valid=false;std::uint8_t load_reg=0;std::uint32_t load_value=0;
    bool commit_load_valid=false;std::uint8_t commit_load_reg=0;std::uint32_t commit_load_value=0;
    void* memory_context=nullptr;Read8 read8=nullptr;Read16 read16=nullptr;Read32 read32=nullptr;
    Write8 write8=nullptr;Write16 write16=nullptr;Write32 write32=nullptr;
    void write(std::uint8_t r,std::uint32_t v) noexcept { if(r) gpr[r]=v; gpr[0]=0; }
};
namespace cpu_ops {
inline void begin(GeneratedCpuState& c) noexcept{c.commit_load_valid=c.load_valid;c.commit_load_reg=c.load_reg;c.commit_load_value=c.load_value;c.load_valid=false;}
inline void end(GeneratedCpuState& c) noexcept{if(c.commit_load_valid)c.write(c.commit_load_reg,c.commit_load_value);c.commit_load_valid=false;}
inline void queue_load(GeneratedCpuState& c,std::uint8_t r,std::uint32_t v) noexcept{if(r){c.load_valid=true;c.load_reg=r;c.load_value=v;}}
inline std::int32_t s(std::uint32_t v) noexcept { return static_cast<std::int32_t>(v); }
inline void addu(GeneratedCpuState& c,std::uint8_t d,std::uint8_t a,std::uint8_t b) noexcept{c.write(d,c.gpr[a]+c.gpr[b]);}
inline void subu(GeneratedCpuState& c,std::uint8_t d,std::uint8_t a,std::uint8_t b) noexcept{c.write(d,c.gpr[a]-c.gpr[b]);}
inline void bit_or(GeneratedCpuState& c,std::uint8_t d,std::uint8_t a,std::uint8_t b) noexcept{c.write(d,c.gpr[a]|c.gpr[b]);}
inline void logic(GeneratedCpuState& c,std::uint8_t d,std::uint32_t v) noexcept{c.write(d,v);}
inline void slt(GeneratedCpuState& c,std::uint8_t d,std::uint8_t a,std::uint8_t b,bool uns) noexcept{c.write(d,uns?c.gpr[a]<c.gpr[b]:s(c.gpr[a])<s(c.gpr[b]));}
inline bool add_checked(std::uint32_t a,std::uint32_t b,std::uint32_t& out) noexcept{out=a+b;return ((~(a^b)&(a^out))>>31U)==0U;}
inline void add(GeneratedCpuState& c,std::uint8_t d,std::uint32_t a,std::uint32_t b) noexcept{std::uint32_t v; if(add_checked(a,b,v))c.write(d,v);else c.fault=CpuFault::Overflow;}
inline void sub(GeneratedCpuState& c,std::uint8_t d,std::uint32_t a,std::uint32_t b) noexcept{std::uint32_t v; if(add_checked(a,~b+1U,v))c.write(d,v);else c.fault=CpuFault::Overflow;}
inline std::uint32_t sar(std::uint32_t v,unsigned n) noexcept{return n?((v>>n)|(s(v)<0?(~0U<<(32U-n)):0U)):v;}
inline void mult(GeneratedCpuState& c,std::uint32_t a,std::uint32_t b,bool uns) noexcept{if(uns){auto v=std::uint64_t(a)*b;c.lo=std::uint32_t(v);c.hi=std::uint32_t(v>>32U);}else{auto v=std::int64_t(s(a))*s(b);auto u=static_cast<std::uint64_t>(v);c.lo=std::uint32_t(u);c.hi=std::uint32_t(u>>32U);}}
inline void div(GeneratedCpuState& c,std::uint32_t a,std::uint32_t b,bool uns) noexcept{if(!b){c.hi=a;c.lo=uns?~0U:(s(a)<0?1U:~0U);return;}if(!uns&&a==0x80000000U&&b==~0U){c.lo=a;c.hi=0;return;}if(uns){c.lo=a/b;c.hi=a%b;}else{c.lo=std::uint32_t(s(a)/s(b));c.hi=std::uint32_t(s(a)%s(b));}}
inline void sw(GeneratedCpuState& c,std::uint8_t base,std::uint8_t source,std::int16_t offset) noexcept{const auto address=c.gpr[base]+static_cast<std::uint32_t>(static_cast<std::int32_t>(offset));if(c.write32==nullptr||!c.write32(c.memory_context,address,c.gpr[source]))c.fault=CpuFault::MemoryAccess;}
inline std::uint32_t address(const GeneratedCpuState& c,std::uint8_t b,std::int16_t o) noexcept{return c.gpr[b]+static_cast<std::uint32_t>(static_cast<std::int32_t>(o));}
inline void lw(GeneratedCpuState& c,std::uint8_t b,std::uint8_t r,std::int16_t o) noexcept{std::uint32_t v=0;auto a=address(c,b,o);if((a&3U)||c.read32==nullptr||!c.read32(c.memory_context,a,v))c.fault=CpuFault::MemoryAccess;else queue_load(c,r,v);}
inline void lh(GeneratedCpuState& c,std::uint8_t b,std::uint8_t r,std::int16_t o,bool uns) noexcept{std::uint16_t v=0;auto a=address(c,b,o);if((a&1U)||c.read16==nullptr||!c.read16(c.memory_context,a,v))c.fault=CpuFault::MemoryAccess;else queue_load(c,r,uns?v:static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(v))));}
inline void lb(GeneratedCpuState& c,std::uint8_t b,std::uint8_t r,std::int16_t o,bool uns) noexcept{std::uint8_t v=0;auto a=address(c,b,o);if(c.read8==nullptr||!c.read8(c.memory_context,a,v))c.fault=CpuFault::MemoryAccess;else queue_load(c,r,uns?v:static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(v))));}
inline void sh(GeneratedCpuState& c,std::uint8_t b,std::uint8_t r,std::int16_t o) noexcept{auto a=address(c,b,o);if((a&1U)||c.write16==nullptr||!c.write16(c.memory_context,a,static_cast<std::uint16_t>(c.gpr[r])))c.fault=CpuFault::MemoryAccess;}
inline void sb(GeneratedCpuState& c,std::uint8_t b,std::uint8_t r,std::int16_t o) noexcept{auto a=address(c,b,o);if(c.write8==nullptr||!c.write8(c.memory_context,a,static_cast<std::uint8_t>(c.gpr[r])))c.fault=CpuFault::MemoryAccess;}
}
}
