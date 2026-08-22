#include "sha256.hpp"

#include <array>
#include <bit>
#include <iomanip>
#include <sstream>

namespace clean_aot::app::detail {
namespace {
constexpr std::array<std::uint32_t, 64U> k{
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};

void block(std::array<std::uint32_t, 8U>& h, const std::uint8_t* input) {
    std::array<std::uint32_t, 64U> w{};
    for (std::size_t i=0U;i<16U;++i) w[i]=(std::uint32_t{input[i*4U]}<<24U)|(std::uint32_t{input[i*4U+1U]}<<16U)|(std::uint32_t{input[i*4U+2U]}<<8U)|input[i*4U+3U];
    for (std::size_t i=16U;i<64U;++i) {
        const auto s0=std::rotr(w[i-15U],7)^std::rotr(w[i-15U],18)^(w[i-15U]>>3U);
        const auto s1=std::rotr(w[i-2U],17)^std::rotr(w[i-2U],19)^(w[i-2U]>>10U);
        w[i]=w[i-16U]+s0+w[i-7U]+s1;
    }
    auto a=h[0];auto b=h[1];auto c=h[2];auto d=h[3];auto e=h[4];auto f=h[5];auto g=h[6];auto q=h[7];
    for (std::size_t i=0U;i<64U;++i) {
        const auto s1=std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);
        const auto choice=(e&f)^((~e)&g); const auto t1=q+s1+choice+k[i]+w[i];
        const auto s0=std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);
        const auto majority=(a&b)^(a&c)^(b&c); const auto t2=s0+majority;
        q=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=q;
}
} // namespace

std::string sha256(std::span<const std::uint8_t> bytes) {
    std::array<std::uint32_t,8U> h{0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    std::size_t offset=0U; while(bytes.size()-offset>=64U){block(h,bytes.data()+offset);offset+=64U;}
    std::array<std::uint8_t,128U> tail{}; const std::size_t remaining=bytes.size()-offset;
    for(std::size_t i=0U;i<remaining;++i) {
        tail[i]=bytes[offset+i];
    }
    tail[remaining]=0x80U;
    const std::size_t padded=remaining<56U?64U:128U; const std::uint64_t bits=static_cast<std::uint64_t>(bytes.size())*8U;
    for(std::size_t i=0U;i<8U;++i)tail[padded-1U-i]=static_cast<std::uint8_t>(bits>>(i*8U));
    block(h,tail.data()); if(padded==128U)block(h,tail.data()+64U);
    std::ostringstream out; out<<std::hex<<std::setfill('0'); for(const auto word:h)out<<std::setw(8)<<word; return out.str();
}
} // namespace clean_aot::app::detail
