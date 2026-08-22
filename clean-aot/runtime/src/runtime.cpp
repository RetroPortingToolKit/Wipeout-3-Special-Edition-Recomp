#include "clean_aot/runtime/runtime.hpp"

#include "cdrom.hpp"
#include "clean_aot/gpu/gpu.hpp"
#include "clean_aot/spu/spu.hpp"
#include "clean_aot_soc.hpp"
#include "gte.hpp"
#include "mdec.hpp"
#include "sio.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clean_aot::runtime {
namespace {

constexpr std::uint32_t kMemoryControlBegin = 0x1f80'1000U;
constexpr std::uint32_t kMemoryControlEnd = 0x1f80'1024U;
constexpr std::uint32_t kSio0Begin = 0x1f80'1040U;
constexpr std::uint32_t kSio0End = 0x1f80'1050U;
constexpr std::uint32_t kSio1Begin = 0x1f80'1050U;
constexpr std::uint32_t kSio1End = 0x1f80'1060U;
constexpr std::uint32_t kRamSize = 0x1f80'1060U;
constexpr std::uint32_t kInterruptStatus = 0x1f80'1070U;
constexpr std::uint32_t kInterruptMask = 0x1f80'1074U;
constexpr std::uint32_t kDmaBegin = 0x1f80'1080U;
constexpr std::uint32_t kDmaEnd = 0x1f80'1100U;
constexpr std::uint32_t kTimerBegin = 0x1f80'1100U;
constexpr std::uint32_t kTimerEnd = 0x1f80'1130U;
constexpr std::uint32_t kCdromBegin = 0x1f80'1800U;
constexpr std::uint32_t kCdromEnd = 0x1f80'1804U;
constexpr std::uint32_t kGpuBegin = 0x1f80'1810U;
constexpr std::uint32_t kGpuEnd = 0x1f80'1818U;
constexpr std::uint32_t kMdecBegin = 0x1f80'1820U;
constexpr std::uint32_t kMdecEnd = 0x1f80'1828U;
constexpr std::uint32_t kSpuBegin = 0x1f80'1c00U;
constexpr std::uint32_t kSpuEnd = 0x1f80'1e00U;
constexpr std::uint32_t kExpansionTwoBegin = 0x1f80'2000U;
constexpr std::uint32_t kExpansionTwoEnd = 0x1f80'2080U;
constexpr std::uint32_t kCacheControl = 0xfffe'0130U;

[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> input) {
    static constexpr std::array<std::uint32_t, 64> constants = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
    std::array<std::uint32_t, 8> hash = {0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                                         0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    const std::uint64_t bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
    const std::size_t padded = ((input.size() + 9U + 63U) / 64U) * 64U;
    std::vector<std::uint8_t> bytes(padded, 0U);
    std::copy(input.begin(), input.end(), bytes.begin());
    bytes[input.size()] = 0x80U;
    for (std::size_t index = 0; index < 8U; ++index) {
        bytes[padded - 1U - index] = static_cast<std::uint8_t>(bit_length >> (index * 8U));
    }
    auto rotate = [](std::uint32_t value, unsigned amount) { return (value >> amount) | (value << (32U - amount)); };
    for (std::size_t offset = 0; offset < padded; offset += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16U; ++i) {
            const std::size_t p = offset + i * 4U;
            words[i] = (static_cast<std::uint32_t>(bytes[p]) << 24U) | (static_cast<std::uint32_t>(bytes[p+1U]) << 16U) |
                       (static_cast<std::uint32_t>(bytes[p+2U]) << 8U) | bytes[p+3U];
        }
        for (std::size_t i = 16U; i < 64U; ++i) {
            const auto s0 = rotate(words[i-15U],7U) ^ rotate(words[i-15U],18U) ^ (words[i-15U] >> 3U);
            const auto s1 = rotate(words[i-2U],17U) ^ rotate(words[i-2U],19U) ^ (words[i-2U] >> 10U);
            words[i] = words[i-16U] + s0 + words[i-7U] + s1;
        }
        auto a=hash[0]; auto b=hash[1]; auto c=hash[2]; auto d=hash[3]; auto e=hash[4]; auto f=hash[5]; auto g=hash[6]; auto h=hash[7];
        for (std::size_t i = 0; i < 64U; ++i) {
            const auto s1=rotate(e,6U)^rotate(e,11U)^rotate(e,25U); const auto ch=(e&f)^((~e)&g);
            const auto t1=h+s1+ch+constants[i]+words[i]; const auto s0=rotate(a,2U)^rotate(a,13U)^rotate(a,22U);
            const auto maj=(a&b)^(a&c)^(b&c); const auto t2=s0+maj;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d; hash[4]+=e; hash[5]+=f; hash[6]+=g; hash[7]+=h;
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string output(64U, '0');
    for (std::size_t i=0;i<hash.size();++i) for (std::size_t j=0;j<8U;++j)
        output[i*8U+j]=digits[(hash[i] >> ((7U-j)*4U)) & 0xfU];
    return output;
}

[[nodiscard]] bool parse_variant_identity(std::string_view text,
                                           memory::VariantIdentity& identity) noexcept {
    if(text.size()!=64U)return false;
    auto nibble=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(std::size_t i=0;i<identity.digest.size();++i){const int high=nibble(text[i*2U]),low=nibble(text[i*2U+1U]);if(high<0||low<0)return false;identity.digest[i]=static_cast<std::uint8_t>((high<<4)|low);}return identity.valid();
}

[[nodiscard]] bool hash_bus_range(memory::Bus& bus,std::uint32_t start,
                                  std::uint32_t byte_count,std::string& digest){
    if(byte_count==0U||static_cast<std::uint64_t>(start)+byte_count>(std::uint64_t{1}<<32U))return false;
    std::vector<std::uint8_t> bytes(byte_count);for(std::uint32_t i=0;i<byte_count;++i){const auto value=bus.read8(start+i);if(!value.ok())return false;bytes[i]=value.value;}digest=sha256_hex(bytes);return true;
}

[[nodiscard]] constexpr std::uint32_t access_width_bytes(
    memory::AccessWidth width) noexcept {
    return memory::width_bytes(width);
}

[[nodiscard]] constexpr std::uint32_t width_mask(
    memory::AccessWidth width) noexcept {
    switch (width) {
    case memory::AccessWidth::Byte:
        return 0xffU;
    case memory::AccessWidth::Halfword:
        return 0xffffU;
    case memory::AccessWidth::Word:
        return 0xffff'ffffU;
    }
    return 0U;
}

[[nodiscard]] constexpr std::uint32_t lane_read(std::uint32_t word,
                                                 std::uint32_t address,
                                                 memory::AccessWidth width) noexcept {
    const std::uint32_t shift = (address & 3U) * 8U;
    return (word >> shift) & width_mask(width);
}

[[nodiscard]] constexpr std::uint32_t lane_merge(
    std::uint32_t previous,
    std::uint32_t address,
    memory::AccessWidth width,
    std::uint32_t value) noexcept {
    const std::uint32_t shift = (address & 3U) * 8U;
    const std::uint32_t mask = width_mask(width) << shift;
    return (previous & ~mask) | ((value << shift) & mask);
}

[[nodiscard]] bool add_rational_parts(
    std::uint64_t denominator,
    std::uint64_t right_whole,
    std::uint64_t right_remainder,
    std::uint64_t& left_whole,
    std::uint64_t& left_remainder) noexcept {
    if (denominator == 0U || left_remainder >= denominator ||
        right_remainder >= denominator ||
        right_whole > std::numeric_limits<std::uint64_t>::max() - left_whole) {
        return false;
    }

    std::uint64_t sum_whole = left_whole + right_whole;
    const std::uint64_t distance = denominator - right_remainder;
    std::uint64_t sum_remainder = 0U;
    if (left_remainder >= distance) {
        if (sum_whole == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        ++sum_whole;
        sum_remainder = left_remainder - distance;
    } else {
        sum_remainder = left_remainder + right_remainder;
    }
    left_whole = sum_whole;
    left_remainder = sum_remainder;
    return true;
}

struct RationalPreview final {
    std::uint64_t amount = 0U;
    std::uint64_t remainder = 0U;
    std::uint64_t total = 0U;
};

[[nodiscard]] bool preview_rational(
    std::uint64_t units,
    std::uint64_t rate_numerator,
    std::uint64_t rate_denominator,
    std::uint64_t current_remainder,
    std::uint64_t current_total,
    RationalPreview& result) noexcept {
    if (rate_denominator == 0U || current_remainder >= rate_denominator) {
        return false;
    }

    std::uint64_t accumulated_whole = 0U;
    std::uint64_t accumulated_remainder = current_remainder;
    std::uint64_t term_whole = rate_numerator / rate_denominator;
    std::uint64_t term_remainder = rate_numerator % rate_denominator;
    std::uint64_t remaining = units;
    while (remaining != 0U) {
        if ((remaining & 1U) != 0U &&
            !add_rational_parts(rate_denominator, term_whole, term_remainder,
                                accumulated_whole,
                                accumulated_remainder)) {
            return false;
        }
        remaining >>= 1U;
        if (remaining != 0U) {
            const std::uint64_t prior_whole = term_whole;
            const std::uint64_t prior_remainder = term_remainder;
            if (!add_rational_parts(rate_denominator, prior_whole,
                                    prior_remainder, term_whole,
                                    term_remainder)) {
                return false;
            }
        }
    }
    if (accumulated_whole >
        std::numeric_limits<std::uint64_t>::max() - current_total) {
        return false;
    }
    result = RationalPreview{accumulated_whole, accumulated_remainder,
                             current_total + accumulated_whole};
    return true;
}

[[nodiscard]] AccessFault map_status(Status status) noexcept {
    switch (status) {
    case Status::Ok:
        return AccessFault::None;
    case Status::TimeReversal:
        return AccessFault::TimeReversal;
    case Status::ArithmeticOverflow:
        return AccessFault::ArithmeticOverflow;
    case Status::DeviceRejected:
        return AccessFault::DeviceRejected;
    case Status::ReentrantAccess:
        return AccessFault::ReentrantAccess;
    case Status::MissedDeadline:
        return AccessFault::MissedDeadline;
    case Status::InvalidConfiguration:
    case Status::ExecutableRejected:
        return AccessFault::BusError;
    }
    return AccessFault::BusError;
}

[[nodiscard]] AccessFault map_bus_status(memory::AccessStatus status) noexcept {
    switch (status) {
    case memory::AccessStatus::Ok:
        return AccessFault::None;
    case memory::AccessStatus::InvalidWidth:
    case memory::AccessStatus::AlignmentFault:
    case memory::AccessStatus::AddressOverflow:
        return AccessFault::AddressError;
    case memory::AccessStatus::UnsupportedSegment:
        return AccessFault::TlbMiss;
    case memory::AccessStatus::InvalidConfiguration:
    case memory::AccessStatus::InvalidOrigin:
    case memory::AccessStatus::OriginNotSupported:
    case memory::AccessStatus::Unmapped:
    case memory::AccessStatus::ReadOnly:
    case memory::AccessStatus::MmioUnavailable:
    case memory::AccessStatus::MmioRejected:
    case memory::AccessStatus::GenerationExhausted:
        return AccessFault::BusError;
    }
    return AccessFault::BusError;
}

[[nodiscard]] bool interval_contains(std::uint32_t address,
                                     memory::AccessWidth width,
                                     std::uint32_t begin,
                                     std::uint32_t end) noexcept {
    const std::uint32_t bytes = access_width_bytes(width);
    return bytes != 0U && address >= begin && address < end &&
           bytes <= end - address;
}

[[nodiscard]] std::optional<std::uint32_t> physical_address(
    std::uint32_t address) noexcept {
    const memory::Translation translation = memory::translate_address(address);
    return translation.ok()
               ? std::optional<std::uint32_t>{translation.physical_address}
               : std::nullopt;
}

[[nodiscard]] bool is_cache_control(std::uint32_t address,
                                    memory::AccessWidth width) noexcept {
    const std::uint32_t bytes = access_width_bytes(width);
    return bytes != 0U && address >= kCacheControl &&
           address - kCacheControl <= 4U - bytes;
}

[[nodiscard]] constexpr bool is_cached_cpu_address(
    std::uint32_t address) noexcept {
    // KUSEG and KSEG0 use the R3000A data cache. KSEG1 is the uncached alias;
    // KSEG2 is not directly mapped by this runtime.
    return address < 0xa000'0000U;
}

[[nodiscard]] bool is_device_access(std::uint32_t address,
                                    memory::AccessWidth width) noexcept {
    if (is_cache_control(address, width)) {
        return true;
    }
    const std::optional<std::uint32_t> physical = physical_address(address);
    if (!physical.has_value()) {
        return false;
    }
    const std::uint32_t bytes = access_width_bytes(width);
    const bool expansion_one = *physical >= 0x1f00'0000U &&
                               *physical < 0x1f80'0000U &&
                               bytes <= 0x1f80'0000U - *physical;
    const bool mmio = *physical >= memory::kMmioPhysicalBase &&
                      *physical < memory::kBiosPhysicalBase &&
                      bytes <= memory::kBiosPhysicalBase - *physical;
    return bytes != 0U && (expansion_one || mmio);
}

[[nodiscard]] bool is_expansion_one(std::uint32_t address,
                                    memory::AccessWidth width) noexcept {
    const auto physical = physical_address(address);
    const std::uint32_t bytes = access_width_bytes(width);
    return physical.has_value() && bytes != 0U && *physical >= 0x1f00'0000U &&
           *physical < 0x1f80'0000U && bytes <= 0x1f80'0000U - *physical;
}

[[nodiscard]] sio::MmioWidth sio_width(memory::AccessWidth width) noexcept {
    return static_cast<sio::MmioWidth>(static_cast<std::uint8_t>(width));
}

[[nodiscard]] spu::MmioWidth spu_width(memory::AccessWidth width) noexcept {
    return static_cast<spu::MmioWidth>(static_cast<std::uint8_t>(width));
}

} // namespace

struct RetirementClock::Impl final {
    machine::Machine::CpuMultiplier multiplier{0U, 0U};
    std::uint64_t rate_numerator = 0U;
    std::uint64_t rate_denominator = 0U;
    std::uint64_t retirement_total = 0U;
    std::uint64_t native_total = 0U;
    std::uint64_t remainder = 0U;
    bool configured = false;
};

RetirementClock::RetirementClock(
    std::uint64_t multiplier_numerator,
    std::uint64_t multiplier_denominator) noexcept
    : impl_(new (std::nothrow) Impl{}) {
    if (impl_ == nullptr) {
        return;
    }
    machine::Machine::CpuMultiplier canonical{};
    if (!machine::Machine::canonicalize_cpu_multiplier(
            multiplier_numerator, multiplier_denominator, canonical)) {
        return;
    }
    impl_->multiplier = canonical;
    impl_->rate_numerator = canonical.denominator;
    impl_->rate_denominator = canonical.numerator;
    impl_->configured = true;
}

RetirementClock::~RetirementClock() = default;
RetirementClock::RetirementClock(RetirementClock&&) noexcept = default;
RetirementClock& RetirementClock::operator=(RetirementClock&&) noexcept = default;

bool RetirementClock::configured() const noexcept {
    return impl_ != nullptr && impl_->configured;
}

machine::Machine::CpuMultiplier RetirementClock::multiplier() const noexcept {
    return impl_ != nullptr ? impl_->multiplier
                            : machine::Machine::CpuMultiplier{0U, 0U};
}

ClockAdvance RetirementClock::advance_to(
    std::uint64_t retirement_cycle) noexcept {
    ClockAdvance result{};
    if (!configured()) {
        return result;
    }
    result.phase_denominator = impl_->rate_denominator;
    result.retirement_total = impl_->retirement_total;
    result.native_total = impl_->native_total;
    result.phase = impl_->remainder;
    if (retirement_cycle < impl_->retirement_total) {
        result.status = Status::TimeReversal;
        return result;
    }

    const std::uint64_t delta = retirement_cycle - impl_->retirement_total;
    RationalPreview preview{};
    if (!preview_rational(delta, impl_->rate_numerator,
                          impl_->rate_denominator, impl_->remainder,
                          impl_->native_total, preview)) {
        result.status = Status::ArithmeticOverflow;
        return result;
    }

    impl_->retirement_total = retirement_cycle;
    impl_->native_total = preview.total;
    impl_->remainder = preview.remainder;
    result.status = Status::Ok;
    result.retirement_delta = delta;
    result.native_delta = preview.amount;
    result.retirement_total = impl_->retirement_total;
    result.native_total = impl_->native_total;
    result.phase = impl_->remainder;
    return result;
}

ClockDeadline RetirementClock::deadline_for_native_cycle(
    std::uint64_t native_cycle) const noexcept {
    ClockDeadline result{};
    result.native_cycle = native_cycle;
    if (!configured()) {
        return result;
    }
    if (native_cycle <= impl_->native_total) {
        result.status = Status::Ok;
        result.retirement_cycle = impl_->retirement_total;
        return result;
    }

    const std::uint64_t maximum_delta =
        std::numeric_limits<std::uint64_t>::max() - impl_->retirement_total;
    if (maximum_delta == 0U) {
        result.status = Status::ArithmeticOverflow;
        return result;
    }
    auto reaches = [&](std::uint64_t delta) noexcept {
        RationalPreview preview{};
        if (!preview_rational(delta, impl_->rate_numerator,
                              impl_->rate_denominator, impl_->remainder,
                              impl_->native_total, preview)) {
            /* Overflow is above every representable native target. */
            return true;
        }
        return preview.total >= native_cycle;
    };

    std::uint64_t high = 1U;
    while (high < maximum_delta && !reaches(high)) {
        high = high > maximum_delta / 2U ? maximum_delta : high * 2U;
    }
    if (!reaches(high)) {
        result.status = Status::ArithmeticOverflow;
        return result;
    }

    std::uint64_t low = 0U;
    while (low + 1U < high) {
        const std::uint64_t middle = low + ((high - low) / 2U);
        if (reaches(middle)) {
            high = middle;
        } else {
            low = middle;
        }
    }
    result.status = Status::Ok;
    result.retirement_cycle = impl_->retirement_total + high;
    return result;
}

void RetirementClock::reset() noexcept {
    if (impl_ != nullptr) {
        impl_->retirement_total = 0U;
        impl_->native_total = 0U;
        impl_->remainder = 0U;
    }
}

std::uint64_t RetirementClock::retirement_total() const noexcept {
    return impl_ != nullptr ? impl_->retirement_total : 0U;
}

std::uint64_t RetirementClock::native_total() const noexcept {
    return impl_ != nullptr ? impl_->native_total : 0U;
}

std::uint64_t RetirementClock::phase() const noexcept {
    return impl_ != nullptr ? impl_->remainder : 0U;
}

struct Runtime::Impl final {
    static constexpr std::size_t kMaximumCopiedEpochs = 64U;
    struct CopiedEpoch final {
        GeneratedDispatcher dispatcher{};
        memory::CodeRegionStamp stamp{};
        memory::VariantIdentity variant{};
        std::string destination_sha256{};
        bool bound = false;
    };
    GeneratedDispatcher generated_dispatcher{};
    GeneratedDispatcher bios_dispatcher{};
    std::array<CopiedEpoch, kMaximumCopiedEpochs> copied_epochs{};
    GeneratedCpuState generated_cpu{};
    GeneratedCpuState bios_cpu{};
    std::string bios_sha256{};
    bool bios_bound = false;
    bool executable_loaded = false;
    std::size_t copied_epoch_count = 0U;
    GeneratedCpuState* active_generated_cpu = nullptr;
    std::uint64_t generated_memory_reads = 0U;
    std::uint64_t generated_memory_writes = 0U;
    std::uint64_t generated_cache_isolated_writes = 0U;
    std::uint64_t generated_device_reads = 0U;
    std::uint64_t generated_device_writes = 0U;
    std::uint32_t last_bios_instruction_count = 0U;

    [[nodiscard]] static bool generated_store_is_cache_isolated(
        const Impl& self, std::uint32_t address) noexcept {
        return self.active_generated_cpu != nullptr &&
               (cpu_ops::effective_status(*self.active_generated_cpu) &
                (1U << 16U)) != 0U &&
               is_cached_cpu_address(address);
    }

    static bool generated_write32(void* context, std::uint32_t address,
                                  std::uint32_t value) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self == nullptr) return false;
        AccessFault fault=AccessFault::None;if(self->active_generated_cpu==nullptr||!self->sync_for_access({self->active_generated_cpu->issue_cycle,self->active_generated_cpu->instruction_pc,self->active_generated_cpu->in_delay_slot},address,memory::AccessWidth::Word,fault))return false;
        ++self->generated_memory_writes;
        if (is_device_access(address, memory::AccessWidth::Word) ||
            address == kCacheControl) ++self->generated_device_writes;
        if (address == kCacheControl) {
            self->cache_control = value;
            return true;
        }
        if (generated_store_is_cache_isolated(*self, address)) {
            ++self->generated_cache_isolated_writes;
            return true;
        }
        return self->bus.write32(address, value).ok();
    }
    static bool generated_read8(void* c,std::uint32_t a,std::uint8_t& v) noexcept{auto*s=static_cast<Impl*>(c);if(!s||!s->active_generated_cpu)return false;AccessFault f=AccessFault::None;if(!s->sync_for_access({s->active_generated_cpu->issue_cycle,s->active_generated_cpu->instruction_pc,s->active_generated_cpu->in_delay_slot},a,memory::AccessWidth::Byte,f))return false;++s->generated_memory_reads;if(is_device_access(a,memory::AccessWidth::Byte)||is_expansion_one(a,memory::AccessWidth::Byte))++s->generated_device_reads;if(is_expansion_one(a,memory::AccessWidth::Byte)){v=0xffU;return true;}auto r=s->bus.read8(a);v=r.value;return r.ok();}
    static bool generated_read16(void* c,std::uint32_t a,std::uint16_t& v) noexcept{auto*s=static_cast<Impl*>(c);if(!s||!s->active_generated_cpu)return false;AccessFault f=AccessFault::None;if(!s->sync_for_access({s->active_generated_cpu->issue_cycle,s->active_generated_cpu->instruction_pc,s->active_generated_cpu->in_delay_slot},a,memory::AccessWidth::Halfword,f))return false;++s->generated_memory_reads;if(is_device_access(a,memory::AccessWidth::Halfword)||is_expansion_one(a,memory::AccessWidth::Halfword))++s->generated_device_reads;if(is_expansion_one(a,memory::AccessWidth::Halfword)){v=0xffffU;return true;}auto r=s->bus.read16(a);v=r.value;return r.ok();}
    static bool generated_read32(void* c,std::uint32_t a,std::uint32_t& v) noexcept{auto*s=static_cast<Impl*>(c);if(!s||!s->active_generated_cpu)return false;AccessFault f=AccessFault::None;if(!s->sync_for_access({s->active_generated_cpu->issue_cycle,s->active_generated_cpu->instruction_pc,s->active_generated_cpu->in_delay_slot},a,memory::AccessWidth::Word,f))return false;++s->generated_memory_reads;if(is_device_access(a,memory::AccessWidth::Word)||is_expansion_one(a,memory::AccessWidth::Word))++s->generated_device_reads;if(is_expansion_one(a,memory::AccessWidth::Word)){v=0xffff'ffffU;return true;}auto r=s->bus.read32(a);v=r.value;return r.ok();}
    static bool generated_write8(void* c,std::uint32_t a,std::uint8_t v) noexcept{auto*s=static_cast<Impl*>(c);if(!s||!s->active_generated_cpu)return false;AccessFault f=AccessFault::None;if(!s->sync_for_access({s->active_generated_cpu->issue_cycle,s->active_generated_cpu->instruction_pc,s->active_generated_cpu->in_delay_slot},a,memory::AccessWidth::Byte,f))return false;++s->generated_memory_writes;if(is_device_access(a,memory::AccessWidth::Byte))++s->generated_device_writes;if(generated_store_is_cache_isolated(*s,a)){++s->generated_cache_isolated_writes;return true;}return s->bus.write8(a,v).ok();}
    static bool generated_write16(void* c,std::uint32_t a,std::uint16_t v) noexcept{auto*s=static_cast<Impl*>(c);if(!s||!s->active_generated_cpu)return false;AccessFault f=AccessFault::None;if(!s->sync_for_access({s->active_generated_cpu->issue_cycle,s->active_generated_cpu->instruction_pc,s->active_generated_cpu->in_delay_slot},a,memory::AccessWidth::Halfword,f))return false;++s->generated_memory_writes;if(is_device_access(a,memory::AccessWidth::Halfword))++s->generated_device_writes;if(generated_store_is_cache_isolated(*s,a)){++s->generated_cache_isolated_writes;return true;}return s->bus.write16(a,v).ok();}
    static std::uint32_t generated_cop2_read_data(void*c,std::uint8_t r)noexcept{auto*s=static_cast<Impl*>(c);return s?s->gte.read_data(r):0U;}static std::uint32_t generated_cop2_read_control(void*c,std::uint8_t r)noexcept{auto*s=static_cast<Impl*>(c);return s?s->gte.read_control(r):0U;}static void generated_cop2_write_data(void*c,std::uint8_t r,std::uint32_t v)noexcept{auto*s=static_cast<Impl*>(c);if(s)s->gte.write_data(r,v);}static void generated_cop2_write_control(void*c,std::uint8_t r,std::uint32_t v)noexcept{auto*s=static_cast<Impl*>(c);if(s)s->gte.write_control(r,v);}static std::uint8_t generated_cop2_execute(void*c,std::uint32_t raw,bool&recognized)noexcept{auto*s=static_cast<Impl*>(c);if(!s){recognized=false;return 0U;}const auto receipt=s->gte.execute(raw);recognized=receipt.recognized;return receipt.cycles;}
    static void bind_generated_cop2(Impl&self,GeneratedCpuState&cpu)noexcept{cpu.cop2_context=&self;cpu.cop2_read_data=&generated_cop2_read_data;cpu.cop2_read_control=&generated_cop2_read_control;cpu.cop2_write_data=&generated_cop2_write_data;cpu.cop2_write_control=&generated_cop2_write_control;cpu.cop2_execute=&generated_cop2_execute;}

    class CdScheduler final : public cdrom::MachineScheduler {
    public:
        static constexpr std::size_t kCapacity = cdrom::Cdrom::EventCapacity;

        void schedule_cdrom_event(
            cdrom::Cycle cycle,
            cdrom::ScheduledEvent event) noexcept override {
            for (std::size_t index = 0U; index < count_; ++index) {
                if (events_[index].cycle == cycle &&
                    events_[index].kind == event) {
                    return;
                }
            }
            if (count_ == events_.size()) {
                overflowed_ = true;
                return;
            }
            events_[count_++] = Event{cycle, event};
        }

        void cancel_cdrom_event(
            cdrom::ScheduledEvent event) noexcept override {
            std::size_t write = 0U;
            for (std::size_t read = 0U; read < count_; ++read) {
                if (events_[read].kind != event) {
                    events_[write++] = events_[read];
                }
            }
            count_ = write;
        }

        void discard_through(cdrom::Cycle cycle) noexcept {
            std::size_t write = 0U;
            for (std::size_t read = 0U; read < count_; ++read) {
                if (events_[read].cycle > cycle) {
                    events_[write++] = events_[read];
                }
            }
            count_ = write;
        }

        [[nodiscard]] std::optional<cdrom::Cycle> next() const noexcept {
            if (count_ == 0U) {
                return std::nullopt;
            }
            cdrom::Cycle value = events_[0].cycle;
            for (std::size_t index = 1U; index < count_; ++index) {
                value = std::min(value, events_[index].cycle);
            }
            return value;
        }

        [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

        void reset() noexcept {
            count_ = 0U;
            overflowed_ = false;
        }

    private:
        struct Event final {
            cdrom::Cycle cycle = 0U;
            cdrom::ScheduledEvent kind = cdrom::ScheduledEvent::CommandStart;
        };

        std::array<Event, kCapacity> events_{};
        std::size_t count_ = 0U;
        bool overflowed_ = false;
    };

    class CddaBridge final : public cdrom::CddaSink {
    public:
        explicit CddaBridge(Impl* owner) noexcept : owner_(owner) {}

        void receive_cdda_samples(
            cdrom::Lba,
            std::span<const cdrom::StereoSample> samples) noexcept override {
            if (owner_ == nullptr || samples.size() > converted_.size()) {
                if (owner_ != nullptr) {
                    owner_->terminal_status = Status::DeviceRejected;
                }
                return;
            }
            for (std::size_t index = 0U; index < samples.size(); ++index) {
                converted_[index] =
                    spu::StereoSample{samples[index].left, samples[index].right};
            }
            const spu::PushResult result = owner_->spu.push_cdda(
                std::span<const spu::StereoSample>(converted_.data(),
                                                   samples.size()));
            if (result.overflow || result.dropped != 0U ||
                result.accepted != samples.size()) {
                owner_->terminal_status = Status::DeviceRejected;
            }
        }

    private:
        Impl* owner_ = nullptr;
        std::array<spu::StereoSample, cdrom::CddaSamplesPerSector> converted_{};
    };

    class Router final : public memory::MmioRouter {
    public:
        explicit Router(Impl* owner) noexcept : owner_(owner) {}

        [[nodiscard]] memory::MmioReadResponse read(
            const memory::MmioRequest& request) noexcept override {
            return owner_ != nullptr ? owner_->mmio_read(request)
                                     : memory::MmioReadResponse{};
        }

        [[nodiscard]] memory::MmioWriteResponse write(
            const memory::MmioRequest& request,
            std::uint32_t value) noexcept override {
            return owner_ != nullptr ? owner_->mmio_write(request, value)
                                     : memory::MmioWriteResponse{};
        }

    private:
        Impl* owner_ = nullptr;
    };

    RetirementClock clock;
    machine::Machine frame_clock;
    soc::SystemOnChip soc{};
    gte::Gte gte{};
    gpu::FrontEnd gpu{};
    mdec::Device mdec{};
    sio::Sio0 sio0;
    spu::Device spu{};
    CdScheduler cd_scheduler{};
    CddaBridge cdda_bridge;
    cdrom::Cdrom cdrom{};
    Router router;
    memory::Bus bus;

    std::array<std::uint32_t, 9U> memory_control{};
    std::uint32_t ram_size = 0U;
    std::uint32_t cache_control = 0U;
    std::array<std::uint8_t, kExpansionTwoEnd - kExpansionTwoBegin>
        expansion_two{};
    std::uint32_t sio1_data = 0xffU;
    std::uint32_t sio1_status = 0x0000'0005U;
    std::uint16_t sio1_mode = 0U;
    std::uint16_t sio1_control = 0U;
    std::uint16_t sio1_misc = 0U;
    std::uint16_t sio1_baud = 0U;
    std::uint64_t active_native_cycle = 0U;
    std::uint8_t interrupt_lines = 0U;
    bool configured = false;
    bool in_public_access = false;
    Status terminal_status = Status::Ok;
    static constexpr std::size_t kAudioCapacity = 16U * 1024U;
    std::array<StereoSample, kAudioCapacity> audio{};
    std::size_t audio_read = 0U;
    std::size_t audio_write = 0U;
    std::size_t audio_count = 0U;
    std::uint64_t produced_audio = 0U;
    bool spu_irq_enabled = false;

    Impl(Configuration configuration,
         machine::Machine::CpuMultiplier canonical,
         bool multiplier_valid)
        : clock(canonical.numerator, canonical.denominator),
          frame_clock(configuration.initial_vblank_rate, canonical),
          sio0(sio::Sio0::Callbacks{&Impl::sio_irq_callback, this}),
          cdda_bridge(this),
          router(this),
          bus(memory::BusConfig{configuration.bios, &router}) {
        configured = multiplier_valid && clock.configured() && bus.configured();
        try {
            bios_sha256 = sha256_hex(configuration.bios);
        } catch (...) {
            configured = false;
        }
        cdrom.attach_scheduler(&cd_scheduler);
        cdrom.attach_cdda_sink(&cdda_bridge);
        if (configuration.disc_source != nullptr) {
            cdrom.attach_source(configuration.disc_source);
        }
        if (configuration.disc_layout != nullptr &&
            !cdrom.set_disc_layout(*configuration.disc_layout)) {
            configured = false;
        }
        spu.set_irq_callback(&Impl::spu_irq_callback, this);
        install_dma_callbacks();
        reset_registers();
        sample_device_irqs();
    }

    void reset_registers() noexcept {
        memory_control.fill(0U);
        memory_control[0] = 0x1f00'0000U;
        memory_control[1] = 0x1f80'2000U;
        ram_size = 0U;
        bus.set_extended_ram_mirrors(true);
        cache_control = 0U;
        expansion_two.fill(0U);
        sio1_data = 0xffU;
        sio1_status = 0x0000'0005U;
        sio1_mode = 0U;
        sio1_control = 0U;
        sio1_misc = 0U;
        sio1_baud = 0U;
        active_native_cycle = 0U;
        interrupt_lines = 0U;
        in_public_access = false;
        terminal_status = Status::Ok;
        audio.fill(StereoSample{});
        audio_read = 0U;
        audio_write = 0U;
        audio_count = 0U;
        produced_audio = 0U;
        spu_irq_enabled = false;
    }

    static void sio_irq_callback(void* context, bool level) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self != nullptr && level) {
            self->soc.interrupts().raise(soc::InterruptSource::controller);
            self->update_interrupt_lines();
        }
    }

    static void spu_irq_callback(void* context) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self != nullptr) {
            self->soc.interrupts().raise(soc::InterruptSource::sound);
            self->update_interrupt_lines();
        }
    }

    void update_interrupt_lines() noexcept {
        /* The PS1 peripheral interrupt controller drives COP0 hardware IP2. */
        interrupt_lines = soc.interrupts().cpu_line() ? 1U : 0U;
    }

    void sample_device_irqs() noexcept {
        /* Channel 3 DRQ is the live CD data-FIFO level, not a latched event. */
        (void)soc.dma().set_request(3U, cdrom.data_count() >= 4U);
        if (soc.dma().irq_line()) {
            soc.interrupts().raise(soc::InterruptSource::dma);
        }
        soc.counters().resample_irq_lines();
        if (cdrom.irq_line()) {
            soc.interrupts().raise(soc::InterruptSource::disc);
        }
        if (gpu.control().irq1_pending) {
            soc.interrupts().raise(soc::InterruptSource::graphics);
        }
        if (sio0.irq_pending()) {
            soc.interrupts().raise(soc::InterruptSource::controller);
        }
        if (spu.irq_pending()) {
            soc.interrupts().raise(soc::InterruptSource::sound);
        }
        update_interrupt_lines();
    }

    [[nodiscard]] Status advance_devices(const ClockAdvance& advance) noexcept {
        if (!advance.ok()) {
            return advance.status;
        }
        active_native_cycle = advance.native_total;
        if (advance.native_delta != 0U) {
            constexpr std::uint64_t kNativeCyclesPerSpuSample =
                machine::Machine::CpuHz / machine::Machine::SpuHz;
            static_assert(kNativeCyclesPerSpuSample == 768U);
            const std::uint64_t target_samples =
                advance.native_total / kNativeCyclesPerSpuSample;
            if (target_samples < produced_audio) {
                return Status::DeviceRejected;
            }
            const std::uint64_t samples_due = target_samples - produced_audio;
            if (samples_due > static_cast<std::uint64_t>(kAudioCapacity -
                                                         audio_count)) {
                return Status::DeviceRejected;
            }
            soc::CounterAdvanceReport counter_report{};
            if (!soc::succeeded(soc.counters().advance_cpu(
                    soc::CycleDelta::whole(advance.native_delta),
                    &counter_report))) {
                return Status::DeviceRejected;
            }
            cdrom.advance_to(active_native_cycle);
            cd_scheduler.discard_through(active_native_cycle);
            if (cd_scheduler.overflowed()) {
                return Status::DeviceRejected;
            }
            if (!succeeded(terminal_status)) {
                return terminal_status;
            }
            sample_device_irqs();
            if (!sio0.advance_to(active_native_cycle)) {
                return Status::DeviceRejected;
            }
            const soc::DmaRunBudget dma_budget{advance.native_delta,
                                               advance.native_delta};
            const soc::DmaRunReport dma_report = soc.dma().run(dma_budget);
            if (!soc::succeeded(dma_report.status) &&
                dma_report.status != soc::SocStatus::idle) {
                return Status::DeviceRejected;
            }
            std::uint64_t remaining_samples = samples_due;
            std::array<spu::StereoSample, spu::MaxRenderSamplesPerCall> rendered{};
            while (remaining_samples != 0U) {
                const std::size_t chunk = static_cast<std::size_t>(std::min(
                    remaining_samples,
                    static_cast<std::uint64_t>(rendered.size())));
                const spu::RenderResult render =
                    spu.render(std::span<spu::StereoSample>(rendered.data(), chunk));
                if (!render.ok() || render.rendered_samples != chunk) {
                    return Status::DeviceRejected;
                }
                for (std::size_t index = 0U; index < chunk; ++index) {
                    audio[audio_write] =
                        StereoSample{rendered[index].left, rendered[index].right};
                    audio_write = (audio_write + 1U) % audio.size();
                }
                audio_count += chunk;
                produced_audio += chunk;
                remaining_samples -= chunk;
            }
        }
        sample_device_irqs();
        return Status::Ok;
    }

    struct NativeDeadlineCandidate final {
        bool pending = false;
        DeadlineSource source = DeadlineSource::None;
        std::uint64_t cycle = 0U;
    };

    static void choose_deadline(NativeDeadlineCandidate& current,
                                DeadlineSource source,
                                std::uint64_t cycle) noexcept {
        if (!current.pending || cycle < current.cycle ||
            (cycle == current.cycle && source < current.source)) {
            current = NativeDeadlineCandidate{true, source, cycle};
        }
    }

    [[nodiscard]] std::optional<std::uint64_t>
    next_counter_interrupt_cycle() const noexcept {
        constexpr std::uint16_t kIrqTarget = 0x0010U;
        constexpr std::uint16_t kIrqOverflow = 0x0020U;
        constexpr std::uint64_t kModulus = 0x1'0000ULL;
        std::optional<std::uint64_t> result{};
        for (std::size_t index = 0U; index < soc::RootCounters::kCount;
             ++index) {
            const soc::CounterSnapshot state = soc.counters().snapshot(index);
            if (!state.running || !state.irq_armed ||
                (state.mode & (kIrqTarget | kIrqOverflow)) == 0U) {
                continue;
            }

            const std::uint8_t selector =
                static_cast<std::uint8_t>((state.mode >> 8U) & 3U);
            bool system_clock = false;
            bool divided_by_eight = false;
            if (index == 0U || index == 1U) {
                system_clock = selector == 0U || selector == 2U;
            } else {
                system_clock = selector < 2U;
                divided_by_eight = selector >= 2U;
            }
            if (!system_clock && !divided_by_eight) {
                continue;
            }

            const std::uint64_t count = state.count;
            const std::uint64_t to_target =
                state.target > state.count
                    ? static_cast<std::uint64_t>(state.target) - count
                    : (kModulus - count) + state.target;
            const std::uint64_t to_overflow = kModulus - count;
            std::uint64_t ticks = kModulus;
            if ((state.mode & kIrqTarget) != 0U) {
                ticks = std::min(ticks, to_target);
            }
            if ((state.mode & kIrqOverflow) != 0U) {
                ticks = std::min(ticks, to_overflow);
            }

            std::uint64_t native_delta = ticks;
            if (divided_by_eight) {
                if (state.phase_denominator == 0U ||
                    state.phase_numerator >= state.phase_denominator) {
                    return std::nullopt;
                }
                const std::uint64_t required_numerator =
                    ticks * state.phase_denominator - state.phase_numerator;
                const std::uint64_t scaled = required_numerator * 8U;
                native_delta =
                    scaled / state.phase_denominator +
                    ((scaled % state.phase_denominator) != 0U ? 1U : 0U);
            }
            if (native_delta > std::numeric_limits<std::uint64_t>::max() -
                                   clock.native_total()) {
                continue;
            }
            const std::uint64_t cycle = clock.native_total() + native_delta;
            result = result.has_value() ? std::min(*result, cycle) : cycle;
        }
        return result;
    }

    [[nodiscard]] NativeDeadlineCandidate earliest_native_deadline() const noexcept {
        NativeDeadlineCandidate result{};
        if (const auto counter = next_counter_interrupt_cycle();
            counter.has_value()) {
            choose_deadline(result, DeadlineSource::RootCounter, *counter);
        }
        if (const auto disc = cd_scheduler.next(); disc.has_value()) {
            choose_deadline(result, DeadlineSource::Cdrom, *disc);
        }
        const std::uint64_t sio_cycle = sio0.next_event_cycle();
        if (sio_cycle != std::numeric_limits<std::uint64_t>::max()) {
            choose_deadline(result, DeadlineSource::Sio0, sio_cycle);
        }
        if (spu_irq_enabled && produced_audio !=
                                   std::numeric_limits<std::uint64_t>::max() / 768U) {
            const std::uint64_t sample_cycle = (produced_audio + 1U) * 768U;
            choose_deadline(result, DeadlineSource::Spu, sample_cycle);
        }
        for (std::size_t channel = 0U;
             channel < soc::DmaController::kChannelCount; ++channel) {
            if (soc.dma().channel_status(channel).active) {
                if (clock.native_total() !=
                    std::numeric_limits<std::uint64_t>::max()) {
                    choose_deadline(result, DeadlineSource::Dma,
                                    clock.native_total() + 1U);
                }
                break;
            }
        }
        return result;
    }

    [[nodiscard]] DeviceDeadline next_deadline() const noexcept {
        DeviceDeadline result{};
        if (!configured) {
            return result;
        }
        if (!succeeded(terminal_status)) {
            result.status = terminal_status;
            return result;
        }
        result.status = Status::Ok;
        const NativeDeadlineCandidate candidate = earliest_native_deadline();
        if (!candidate.pending) {
            return result;
        }
        const ClockDeadline raw =
            clock.deadline_for_native_cycle(candidate.cycle);
        if (!raw.ok()) {
            result.status = raw.status;
            return result;
        }
        result.pending = true;
        result.source = candidate.source;
        result.native_cycle = candidate.cycle;
        result.raw_cpu_cycle = raw.retirement_cycle;
        return result;
    }

    [[nodiscard]] ClockAdvance advance_to(std::uint64_t cycle) noexcept {
        if (!succeeded(terminal_status)) {
            ClockAdvance failed{};
            failed.status = terminal_status;
            failed.retirement_total = clock.retirement_total();
            failed.native_total = clock.native_total();
            failed.phase = clock.phase();
            failed.phase_denominator = clock.multiplier().numerator;
            return failed;
        }
        const DeviceDeadline deadline = next_deadline();
        if (!deadline.ok()) {
            ClockAdvance failed{};
            failed.status = deadline.status;
            failed.retirement_total = clock.retirement_total();
            failed.native_total = clock.native_total();
            return failed;
        }
        if (deadline.pending && cycle > deadline.raw_cpu_cycle) {
            ClockAdvance failed{};
            failed.status = Status::MissedDeadline;
            failed.retirement_total = clock.retirement_total();
            failed.native_total = clock.native_total();
            failed.phase = clock.phase();
            failed.phase_denominator = clock.multiplier().numerator;
            return failed;
        }
        ClockAdvance advance = clock.advance_to(cycle);
        if (!advance.ok()) {
            return advance;
        }
        const Status device_status = advance_devices(advance);
        if (!succeeded(device_status)) {
            advance.status = device_status;
            terminal_status = device_status;
        }
        return advance;
    }

    [[nodiscard]] bool sync_for_access(AccessContext context,
                                       std::uint32_t address,
                                       memory::AccessWidth width,
                                       AccessFault& fault) noexcept {
        if (!is_device_access(address, width)) {
            return true;
        }
        const ClockAdvance advance = advance_to(context.raw_cpu_cycle);
        fault = map_status(advance.status);
        return advance.ok();
    }

    [[nodiscard]] memory::MmioReadResponse handled_read(
        std::uint32_t value) noexcept {
        return {memory::MmioStatus::Handled, value};
    }

    [[nodiscard]] memory::MmioReadResponse rejected_read() noexcept {
        return {memory::MmioStatus::Rejected, 0U};
    }

    [[nodiscard]] memory::MmioWriteResponse handled_write() noexcept {
        return {memory::MmioStatus::Handled};
    }

    [[nodiscard]] memory::MmioWriteResponse rejected_write() noexcept {
        return {memory::MmioStatus::Rejected};
    }

    [[nodiscard]] memory::MmioReadResponse mmio_read(
        const memory::MmioRequest& request) noexcept {
        const std::uint32_t address = request.physical_address;
        const memory::AccessWidth width = request.width;

        if (interval_contains(address, width, kMemoryControlBegin,
                              kMemoryControlEnd)) {
            const std::size_t index =
                static_cast<std::size_t>((address - kMemoryControlBegin) / 4U);
            return handled_read(lane_read(memory_control[index], address, width));
        }
        if (interval_contains(address, width, kSio0Begin, kSio0End)) {
            const sio::MmioReadResult result = sio0.read(
                address - kSio0Begin, sio_width(width), active_native_cycle);
            sample_device_irqs();
            return result.accepted ? handled_read(result.value) : rejected_read();
        }
        if (interval_contains(address, width, kSio1Begin, kSio1End)) {
            return read_sio1(address, width);
        }
        if (interval_contains(address, width, kRamSize, kRamSize + 4U)) {
            return handled_read(lane_read(ram_size, address, width));
        }
        if (interval_contains(address, width, kInterruptStatus,
                              kInterruptStatus + 4U)) {
            return handled_read(lane_read(soc.interrupts().status(), address,
                                          width));
        }
        if (interval_contains(address, width, kInterruptMask,
                              kInterruptMask + 4U)) {
            return handled_read(
                lane_read(soc.interrupts().mask(), address, width));
        }
        if (interval_contains(address, width, kDmaBegin, kDmaEnd)) {
            return read_dma(address, width);
        }
        if (interval_contains(address, width, kTimerBegin, kTimerEnd)) {
            return read_timer(address, width);
        }
        if (interval_contains(address, width, kCdromBegin, kCdromEnd)) {
            const std::uint8_t offset =
                static_cast<std::uint8_t>(address - kCdromBegin);
            std::uint32_t value = 0U;
            switch (width) {
            case memory::AccessWidth::Byte:
                value = cdrom.read_register(offset);
                break;
            case memory::AccessWidth::Halfword:
                value = cdrom.read_register16(offset);
                break;
            case memory::AccessWidth::Word:
                value = cdrom.read_register32(offset);
                break;
            }
            sample_device_irqs();
            return handled_read(value);
        }
        if (interval_contains(address, width, kGpuBegin, kGpuEnd)) {
            if (width != memory::AccessWidth::Word) {
                return rejected_read();
            }
            if (address == kGpuBegin) {
                const gpu::ReadResult result = gpu.read_gpuread();
                return result.ready ? handled_read(result.word) : rejected_read();
            }
            if (address == kGpuBegin + 4U) {
                return handled_read(gpu.status());
            }
            return rejected_read();
        }
        if (interval_contains(address, width, kMdecBegin, kMdecEnd)) {
            if (width != memory::AccessWidth::Word) {
                return rejected_read();
            }
            if (address == kMdecBegin) {
                std::uint32_t value = 0U;
                const mdec::TransferResult result = mdec.read_data_word(value);
                return result.fault == mdec::Fault::None
                           ? handled_read(value)
                           : rejected_read();
            }
            if (address == kMdecBegin + 4U) {
                return handled_read(mdec.status());
            }
            return rejected_read();
        }
        if (interval_contains(address, width, kSpuBegin, kSpuEnd)) {
            const spu::MmioReadResult result =
                spu.read_mmio(address - kSpuBegin, spu_width(width));
            sample_device_irqs();
            return result.ok() ? handled_read(result.value) : rejected_read();
        }
        if (interval_contains(address, width, kExpansionTwoBegin,
                              kExpansionTwoEnd)) {
            std::uint32_t value = 0U;
            const std::size_t offset =
                static_cast<std::size_t>(address - kExpansionTwoBegin);
            for (std::uint32_t lane = 0U; lane < access_width_bytes(width); ++lane) {
                value |= static_cast<std::uint32_t>(expansion_two[offset + lane])
                         << (lane * 8U);
            }
            return handled_read(value);
        }
        return {memory::MmioStatus::Unmapped, 0U};
    }

    [[nodiscard]] memory::MmioWriteResponse mmio_write(
        const memory::MmioRequest& request,
        std::uint32_t value) noexcept {
        const std::uint32_t address = request.physical_address;
        const memory::AccessWidth width = request.width;

        if (interval_contains(address, width, kMemoryControlBegin,
                              kMemoryControlEnd)) {
            const std::size_t index =
                static_cast<std::size_t>((address - kMemoryControlBegin) / 4U);
            memory_control[index] = lane_merge(memory_control[index], address,
                                               width, value);
            return handled_write();
        }
        if (interval_contains(address, width, kSio0Begin, kSio0End)) {
            const sio::MmioWriteResult result = sio0.write(
                address - kSio0Begin, sio_width(width), value,
                active_native_cycle);
            sample_device_irqs();
            return result.accepted ? handled_write() : rejected_write();
        }
        if (interval_contains(address, width, kSio1Begin, kSio1End)) {
            return write_sio1(address, width, value);
        }
        if (interval_contains(address, width, kRamSize, kRamSize + 4U)) {
            ram_size = lane_merge(ram_size, address, width, value);
            const std::uint32_t bank_geometry = (ram_size >> 9U) & 7U;
            if (bank_geometry == 4U) {
                bus.set_extended_ram_mirrors(false);
            } else if (bank_geometry == 5U) {
                bus.set_extended_ram_mirrors(true);
            }
            return handled_write();
        }
        if (interval_contains(address, width, kInterruptStatus,
                              kInterruptStatus + 4U)) {
            const std::uint32_t written = lane_merge(
                0xffff'ffffU, address, width, value);
            soc.interrupts().write_status(written);
            sample_device_irqs();
            return handled_write();
        }
        if (interval_contains(address, width, kInterruptMask,
                              kInterruptMask + 4U)) {
            const std::uint32_t merged = lane_merge(
                soc.interrupts().mask(), address, width, value);
            soc.interrupts().write_mask(merged);
            update_interrupt_lines();
            return handled_write();
        }
        if (interval_contains(address, width, kDmaBegin, kDmaEnd)) {
            return write_dma(address, width, value);
        }
        if (interval_contains(address, width, kTimerBegin, kTimerEnd)) {
            return write_timer(address, width, value);
        }
        if (interval_contains(address, width, kCdromBegin, kCdromEnd)) {
            if (width != memory::AccessWidth::Byte) {
                return rejected_write();
            }
            cdrom.write_register(
                static_cast<std::uint8_t>(address - kCdromBegin),
                static_cast<std::uint8_t>(value));
            sample_device_irqs();
            return handled_write();
        }
        if (interval_contains(address, width, kGpuBegin, kGpuEnd)) {
            if (width != memory::AccessWidth::Word) {
                return rejected_write();
            }
            gpu::WriteResult result{};
            if (address == kGpuBegin) {
                result = gpu.write_gp0(value);
            } else if (address == kGpuBegin + 4U) {
                result = gpu.write_gp1(value);
            } else {
                return rejected_write();
            }
            sample_device_irqs();
            return result.accepted ? handled_write() : rejected_write();
        }
        if (interval_contains(address, width, kMdecBegin, kMdecEnd)) {
            if (width != memory::AccessWidth::Word) {
                return rejected_write();
            }
            const mdec::TransferResult result =
                address == kMdecBegin ? mdec.write_command_word(value)
                                      : mdec.write_control_word(value);
            return result.fault == mdec::Fault::None ? handled_write()
                                                     : rejected_write();
        }
        if (interval_contains(address, width, kSpuBegin, kSpuEnd)) {
            const spu::MmioWriteResult result =
                spu.write_mmio(address - kSpuBegin, value, spu_width(width));
            if (result.ok() && address - kSpuBegin == spu::Device::Control &&
                width == memory::AccessWidth::Halfword) {
                spu_irq_enabled =
                    (static_cast<std::uint16_t>(value) &
                     spu::Device::ControlIrqEnable) != 0U;
            }
            sample_device_irqs();
            return result.ok() ? handled_write() : rejected_write();
        }
        if (interval_contains(address, width, kExpansionTwoBegin,
                              kExpansionTwoEnd)) {
            const std::size_t offset =
                static_cast<std::size_t>(address - kExpansionTwoBegin);
            for (std::uint32_t lane = 0U; lane < access_width_bytes(width); ++lane) {
                expansion_two[offset + lane] =
                    static_cast<std::uint8_t>(value >> (lane * 8U));
            }
            return handled_write();
        }
        return {memory::MmioStatus::Unmapped};
    }

    [[nodiscard]] memory::MmioReadResponse read_sio1(
        std::uint32_t address,
        memory::AccessWidth width) noexcept {
        const std::uint32_t offset = address - kSio1Begin;
        if (offset < 4U) {
            return handled_read(lane_read(sio1_data, address, width));
        }
        if (offset >= 4U && offset < 8U) {
            return handled_read(lane_read(sio1_status, address, width));
        }
        if (width != memory::AccessWidth::Halfword) {
            return rejected_read();
        }
        switch (offset) {
        case 8U:
            return handled_read(sio1_mode);
        case 10U:
            return handled_read(sio1_control);
        case 12U:
            return handled_read(sio1_misc);
        case 14U:
            return handled_read(sio1_baud);
        default:
            return rejected_read();
        }
    }

    [[nodiscard]] memory::MmioWriteResponse write_sio1(
        std::uint32_t address,
        memory::AccessWidth width,
        std::uint32_t value) noexcept {
        const std::uint32_t offset = address - kSio1Begin;
        if (offset < 4U) {
            sio1_data = lane_merge(sio1_data, address, width, value);
            return handled_write();
        }
        if (offset >= 4U && offset < 8U) {
            return rejected_write();
        }
        if (width != memory::AccessWidth::Halfword) {
            return rejected_write();
        }
        switch (offset) {
        case 8U:
            sio1_mode = static_cast<std::uint16_t>(value);
            return handled_write();
        case 10U:
            sio1_control = static_cast<std::uint16_t>(value);
            return handled_write();
        case 12U:
            sio1_misc = static_cast<std::uint16_t>(value);
            return handled_write();
        case 14U:
            sio1_baud = static_cast<std::uint16_t>(value);
            return handled_write();
        default:
            return rejected_write();
        }
    }

    [[nodiscard]] memory::MmioReadResponse read_dma(
        std::uint32_t address,
        memory::AccessWidth width) noexcept {
        if (width != memory::AccessWidth::Word) {
            return rejected_read();
        }
        const std::uint32_t offset = address - kDmaBegin;
        if (offset < 0x70U) {
            const std::size_t channel = static_cast<std::size_t>(offset / 0x10U);
            const std::uint32_t reg = offset & 0x0fU;
            const soc::DmaChannelRegisters registers = soc.dma().registers(channel);
            switch (reg) {
            case 0U:
                return handled_read(registers.base_address);
            case 4U:
                return handled_read(registers.block_control);
            case 8U:
                return handled_read(registers.channel_control);
            default:
                return rejected_read();
            }
        }
        if (offset == 0x70U) {
            return handled_read(soc.dma().read_priority_control());
        }
        if (offset == 0x74U) {
            return handled_read(soc.dma().read_interrupt_control());
        }
        return rejected_read();
    }

    [[nodiscard]] memory::MmioWriteResponse write_dma(
        std::uint32_t address,
        memory::AccessWidth width,
        std::uint32_t value) noexcept {
        if (width != memory::AccessWidth::Word) {
            return rejected_write();
        }
        const std::uint32_t offset = address - kDmaBegin;
        soc::SocStatus status = soc::SocStatus::invalid_argument;
        if (offset < 0x70U) {
            const std::size_t channel = static_cast<std::size_t>(offset / 0x10U);
            switch (offset & 0x0fU) {
            case 0U:
                status = soc.dma().write_base_address(channel, value);
                break;
            case 4U:
                status = soc.dma().write_block_control(channel, value);
                break;
            case 8U:
                status = soc.dma().write_channel_control(channel, value);
                break;
            default:
                return rejected_write();
            }
        } else if (offset == 0x70U) {
            soc.dma().write_priority_control(value);
            status = soc::SocStatus::ok;
        } else if (offset == 0x74U) {
            soc.dma().write_interrupt_control(value);
            status = soc::SocStatus::ok;
        } else {
            return rejected_write();
        }
        sample_device_irqs();
        return soc::succeeded(status) ? handled_write() : rejected_write();
    }

    [[nodiscard]] memory::MmioReadResponse read_timer(
        std::uint32_t address,
        memory::AccessWidth width) noexcept {
        if (width != memory::AccessWidth::Halfword &&
            width != memory::AccessWidth::Word) {
            return rejected_read();
        }
        const std::uint32_t offset = address - kTimerBegin;
        const std::size_t index = static_cast<std::size_t>(offset / 0x10U);
        if (index >= soc::RootCounters::kCount) {
            return rejected_read();
        }
        switch (offset & 0x0fU) {
        case 0U:
            return handled_read(soc.counters().read_count(index));
        case 4U:
            return handled_read(soc.counters().read_mode(index));
        case 8U:
            return handled_read(soc.counters().read_target(index));
        default:
            return rejected_read();
        }
    }

    [[nodiscard]] memory::MmioWriteResponse write_timer(
        std::uint32_t address,
        memory::AccessWidth width,
        std::uint32_t value) noexcept {
        if (width != memory::AccessWidth::Halfword &&
            width != memory::AccessWidth::Word) {
            return rejected_write();
        }
        const std::uint32_t offset = address - kTimerBegin;
        const std::size_t index = static_cast<std::size_t>(offset / 0x10U);
        if (index >= soc::RootCounters::kCount) {
            return rejected_write();
        }
        soc::SocStatus status = soc::SocStatus::invalid_argument;
        const std::uint16_t half = static_cast<std::uint16_t>(value);
        switch (offset & 0x0fU) {
        case 0U:
            status = soc.counters().write_count(index, half);
            break;
        case 4U:
            status = soc.counters().write_mode(index, half);
            break;
        case 8U:
            status = soc.counters().write_target(index, half);
            break;
        default:
            return rejected_write();
        }
        sample_device_irqs();
        return soc::succeeded(status) ? handled_write() : rejected_write();
    }

    static soc::DmaCallbackStatus dma_bus_read(
        void* context,
        std::uint32_t address,
        std::uint32_t* value) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self == nullptr || value == nullptr) {
            return soc::DmaCallbackStatus::malformed;
        }
        const auto result = self->bus.read32(address, memory::AccessOrigin::Dma);
        if (!result.ok()) {
            return soc::DmaCallbackStatus::rejected;
        }
        *value = result.value;
        return soc::DmaCallbackStatus::ok;
    }

    static soc::DmaCallbackStatus dma_bus_write(
        void* context,
        std::uint32_t address,
        std::uint32_t value) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self == nullptr) {
            return soc::DmaCallbackStatus::malformed;
        }
        return self->bus.write32(address, value, memory::AccessOrigin::Dma).ok()
                   ? soc::DmaCallbackStatus::ok
                   : soc::DmaCallbackStatus::rejected;
    }

    static soc::DmaCallbackStatus dma_device_read(
        void* context,
        std::uint32_t* value) noexcept {
        auto* endpoint = static_cast<std::pair<Impl*, std::size_t>*>(context);
        if (endpoint == nullptr || endpoint->first == nullptr || value == nullptr) {
            return soc::DmaCallbackStatus::malformed;
        }
        Impl& self = *endpoint->first;
        switch (endpoint->second) {
        case 1U: {
            const mdec::TransferResult result =
                self.mdec.dma_read_words(std::span<std::uint32_t>(value, 1U));
            return result.fault == mdec::Fault::None && result.words == 1U
                       ? soc::DmaCallbackStatus::ok
                       : soc::DmaCallbackStatus::rejected;
        }
        case 2U: {
            const gpu::ReadResult result = self.gpu.read_dma_word();
            if (!result.ready) {
                return soc::DmaCallbackStatus::not_ready;
            }
            *value = result.word;
            return soc::DmaCallbackStatus::ok;
        }
        case 3U:
            if (self.cdrom.data_count() < 4U) {
                return soc::DmaCallbackStatus::rejected;
            }
            *value = static_cast<std::uint32_t>(self.cdrom.pop_data());
            *value |= static_cast<std::uint32_t>(self.cdrom.pop_data()) << 8U;
            *value |= static_cast<std::uint32_t>(self.cdrom.pop_data()) << 16U;
            *value |= static_cast<std::uint32_t>(self.cdrom.pop_data()) << 24U;
            return soc::DmaCallbackStatus::ok;
        case 4U: {
            std::array<std::uint16_t, 2U> words{};
            const spu::DmaResult result = self.spu.dma_read(words);
            if (!result.ok() || result.transferred_halfwords != words.size()) {
                return soc::DmaCallbackStatus::rejected;
            }
            *value = static_cast<std::uint32_t>(words[0]) |
                     (static_cast<std::uint32_t>(words[1]) << 16U);
            return soc::DmaCallbackStatus::ok;
        }
        default:
            return soc::DmaCallbackStatus::rejected;
        }
    }

    static soc::DmaCallbackStatus dma_device_write(
        void* context,
        std::uint32_t value) noexcept {
        auto* endpoint = static_cast<std::pair<Impl*, std::size_t>*>(context);
        if (endpoint == nullptr || endpoint->first == nullptr) {
            return soc::DmaCallbackStatus::malformed;
        }
        Impl& self = *endpoint->first;
        switch (endpoint->second) {
        case 0U: {
            const mdec::TransferResult result = self.mdec.dma_write_words(
                std::span<const std::uint32_t>(&value, 1U));
            return result.fault == mdec::Fault::None && result.words == 1U
                       ? soc::DmaCallbackStatus::ok
                       : soc::DmaCallbackStatus::rejected;
        }
        case 2U:
            return self.gpu.write_dma_word(value).accepted
                       ? soc::DmaCallbackStatus::ok
                       : soc::DmaCallbackStatus::not_ready;
        case 4U: {
            const std::array<std::uint16_t, 2U> words{
                static_cast<std::uint16_t>(value),
                static_cast<std::uint16_t>(value >> 16U)};
            const spu::DmaResult result = self.spu.dma_write(words);
            return result.ok() && result.transferred_halfwords == words.size()
                       ? soc::DmaCallbackStatus::ok
                       : soc::DmaCallbackStatus::rejected;
        }
        default:
            return soc::DmaCallbackStatus::rejected;
        }
    }

    std::array<std::pair<Impl*, std::size_t>, 7U> dma_endpoints{};

    void install_dma_callbacks() noexcept {
        for (std::size_t channel = 0U; channel < dma_endpoints.size(); ++channel) {
            dma_endpoints[channel] = {this, channel};
            const soc::DmaBusCallbacks bus_callbacks{
                this, &Impl::dma_bus_read, &Impl::dma_bus_write};
            soc::DmaDeviceCallbacks device_callbacks{};
            if (channel <= 4U && channel != 3U) {
                device_callbacks = soc::DmaDeviceCallbacks{
                    &dma_endpoints[channel], &Impl::dma_device_read,
                    &Impl::dma_device_write};
            } else if (channel == 3U) {
                device_callbacks = soc::DmaDeviceCallbacks{
                    &dma_endpoints[channel], &Impl::dma_device_read, nullptr};
            }
            (void)soc.dma().set_callbacks(channel, bus_callbacks,
                                          device_callbacks);
        }
    }
};

namespace {

class AccessGuard final {
public:
    explicit AccessGuard(bool& active) noexcept : active_(active) {
        acquired_ = !active_;
        if (acquired_) {
            active_ = true;
        }
    }
    ~AccessGuard() {
        if (acquired_) {
            active_ = false;
        }
    }
    [[nodiscard]] bool acquired() const noexcept { return acquired_; }

private:
    bool& active_;
    bool acquired_ = false;
};

template <typename ImplType>
[[nodiscard]] ReadResult cache_read(ImplType& impl,
                                    std::uint32_t address,
                                    memory::AccessWidth width) noexcept {
    return {AccessFault::None,
            lane_read(impl.cache_control, address - kCacheControl, width), 0U};
}

template <typename ImplType>
[[nodiscard]] WriteResult cache_write(ImplType& impl,
                                      std::uint32_t address,
                                      memory::AccessWidth width,
                                      std::uint32_t value) noexcept {
    impl.cache_control = lane_merge(impl.cache_control,
                                    address - kCacheControl, width, value);
    return {AccessFault::None, 0U};
}

} // namespace

Runtime::Runtime(Configuration configuration) {
    machine::Machine::CpuMultiplier canonical{1U, 1U};
    const bool multiplier_valid =
        machine::Machine::canonicalize_cpu_multiplier(
            configuration.cpu_multiplier_numerator,
            configuration.cpu_multiplier_denominator, canonical);
    impl_ = std::make_unique<Impl>(configuration, canonical, multiplier_valid);
}

Runtime::~Runtime() = default;
Runtime::Runtime(Runtime&&) noexcept = default;
Runtime& Runtime::operator=(Runtime&&) noexcept = default;

bool Runtime::configured() const noexcept {
    return impl_ != nullptr && impl_->configured;
}

bool Runtime::operational() const noexcept {
    return configured() && succeeded(impl_->terminal_status);
}

Status Runtime::status() const noexcept {
    if (!configured()) {
        return Status::InvalidConfiguration;
    }
    return impl_->terminal_status;
}

Status Runtime::reset() noexcept {
    if (!configured()) {
        return Status::InvalidConfiguration;
    }
    if (impl_->in_public_access) {
        return Status::ReentrantAccess;
    }
    const memory::ResetStatus bus_status = impl_->bus.reset_mutable_state();
    if (bus_status != memory::ResetStatus::Ok) {
        return bus_status == memory::ResetStatus::IdentityExhausted
                   ? Status::ArithmeticOverflow
                   : Status::InvalidConfiguration;
    }
    impl_->clock.reset();
    impl_->frame_clock.reset();
    impl_->soc.reset();
    impl_->gte.reset();
    impl_->gpu.power_on_reset();
    impl_->mdec.reset();
    impl_->sio0.reset();
    impl_->spu.reset();
    impl_->spu.set_irq_callback(&Impl::spu_irq_callback, impl_.get());
    impl_->cd_scheduler.reset();
    impl_->cdrom.reset();
    impl_->install_dma_callbacks();
    impl_->reset_registers();
    impl_->generated_dispatcher = GeneratedDispatcher{};
    impl_->bios_dispatcher = GeneratedDispatcher{};
    impl_->bios_cpu = GeneratedCpuState{};
    impl_->bios_cpu.pc = 0xbfc0'0000U;
    impl_->bios_cpu.next_pc = 0xbfc0'0004U;
    impl_->bios_cpu.memory_context = impl_.get();
    impl_->bios_cpu.read8=&Impl::generated_read8;impl_->bios_cpu.read16=&Impl::generated_read16;impl_->bios_cpu.read32=&Impl::generated_read32;
    impl_->bios_cpu.write8=&Impl::generated_write8;impl_->bios_cpu.write16=&Impl::generated_write16;
    impl_->bios_cpu.write32 = &Impl::generated_write32;
    Impl::bind_generated_cop2(*impl_,impl_->bios_cpu);
    impl_->bios_bound = false;
    impl_->copied_epochs = {};
    impl_->copied_epoch_count = 0U;
    impl_->generated_memory_reads = 0U;
    impl_->generated_memory_writes = 0U;
    impl_->generated_cache_isolated_writes = 0U;
    impl_->generated_device_reads = 0U;
    impl_->generated_device_writes = 0U;
    impl_->last_bios_instruction_count = 0U;
    impl_->generated_cpu = GeneratedCpuState{};
    impl_->executable_loaded = false;
    impl_->sample_device_irqs();
    return Status::Ok;
}

DispatchStatus Runtime::bind_authenticated_bios(
    std::string_view selected_bios_identity_sha256,
    const GeneratedExecutableBinding& binding) noexcept {
    constexpr std::uint32_t reset_pc = 0xbfc0'0000U;
    constexpr std::uint64_t bios_end = 0xbfc8'0000ULL;
    if (!operational() || impl_->bios_bound || impl_->executable_loaded ||
        binding.executable_sha256 != impl_->bios_sha256 ||
        binding.load_start != reset_pc || binding.load_end_exclusive != bios_end) {
        return binding.executable_sha256 != impl_->bios_sha256
                   ? DispatchStatus::ExecutableHashMismatch
                   : DispatchStatus::LoadRejected;
    }
    const auto root = std::lower_bound(binding.dispatch_roots.begin(),
                                       binding.dispatch_roots.end(), reset_pc);
    if (root == binding.dispatch_roots.end() || *root != reset_pc) {
        return DispatchStatus::UnknownCode;
    }
    const DispatchStatus status = impl_->bios_dispatcher.bind(
        selected_bios_identity_sha256, binding);
    if (status != DispatchStatus::Bound) {
        return status;
    }
    impl_->bios_cpu = GeneratedCpuState{};
    impl_->bios_cpu.pc = reset_pc;
    impl_->bios_cpu.next_pc = reset_pc + 4U;
    impl_->bios_cpu.memory_context = impl_.get();
    impl_->bios_cpu.read8=&Impl::generated_read8;impl_->bios_cpu.read16=&Impl::generated_read16;impl_->bios_cpu.read32=&Impl::generated_read32;
    impl_->bios_cpu.write8=&Impl::generated_write8;impl_->bios_cpu.write16=&Impl::generated_write16;
    impl_->bios_cpu.write32 = &Impl::generated_write32;
    Impl::bind_generated_cop2(*impl_,impl_->bios_cpu);
    impl_->bios_bound = true;
    impl_->generated_memory_reads = 0U;
    impl_->generated_memory_writes = 0U;
    impl_->generated_cache_isolated_writes = 0U;
    impl_->generated_device_reads = 0U;
    impl_->generated_device_writes = 0U;
    return DispatchStatus::Bound;
}

DispatchStatus Runtime::dispatch_bios() noexcept {
    if (!operational() || !impl_->bios_bound || impl_->executable_loaded) {
        return DispatchStatus::LoadRejected;
    }
    GeneratedDispatcher* dispatcher=&impl_->bios_dispatcher;
    Impl::CopiedEpoch* selected_epoch=nullptr;
    for(std::size_t index=0U;index<impl_->copied_epoch_count;++index){
        auto& epoch=impl_->copied_epochs[index];
        if(epoch.bound&&impl_->bios_cpu.pc>=epoch.stamp.virtual_start&&static_cast<std::uint64_t>(impl_->bios_cpu.pc)<static_cast<std::uint64_t>(epoch.stamp.virtual_start)+epoch.stamp.byte_count){
            if(selected_epoch==nullptr||epoch.stamp.byte_count<selected_epoch->stamp.byte_count)selected_epoch=&epoch;
        }
    }
    if(selected_epoch!=nullptr){
        if(impl_->bus.validate_code_region(selected_epoch->stamp,selected_epoch->variant)!=memory::CodeRegionValidity::Current){
            std::string current_hash;
            try{if(!hash_bus_range(impl_->bus,selected_epoch->stamp.virtual_start,selected_epoch->stamp.byte_count,current_hash))return DispatchStatus::EpochMismatch;}catch(...){return DispatchStatus::EpochMismatch;}
            if(current_hash!=selected_epoch->destination_sha256)return DispatchStatus::EpochMismatch;
            const auto refreshed=impl_->bus.capture_code_region(selected_epoch->stamp.virtual_start,selected_epoch->stamp.byte_count,selected_epoch->variant);
            if(!refreshed.ok())return DispatchStatus::EpochMismatch;
            selected_epoch->stamp=refreshed.stamp;
        }
        dispatcher=&selected_epoch->dispatcher;
    }
    impl_->bios_cpu.issue_cycle=impl_->clock.retirement_total();
    impl_->bios_cpu.cop0_cause=(impl_->bios_cpu.cop0_cause&~0x0000FC00U)|((static_cast<std::uint32_t>(impl_->interrupt_lines)<<10U)&0x0000FC00U);
    impl_->active_generated_cpu=&impl_->bios_cpu;
    const auto result=dispatcher->dispatch(impl_->bios_cpu);
    impl_->last_bios_instruction_count = dispatcher->last_instruction_count();
    impl_->active_generated_cpu=nullptr;
    if(result==DispatchStatus::Executed)(void)impl_->advance_to(impl_->bios_cpu.issue_cycle);
    return result;
}

DispatchStatus Runtime::bind_authenticated_copied_epoch(
    const AuthenticatedCopiedEpochBinding& binding) noexcept {
    if(!operational()||!impl_->bios_bound||impl_->executable_loaded||binding.variant_identity_sha256!=binding.generated.variant_identity_sha256||binding.destination_sha256!=binding.generated.executable_sha256)return DispatchStatus::InvalidBinding;
    memory::VariantIdentity variant{};if(!parse_variant_identity(binding.variant_identity_sha256,variant))return DispatchStatus::IdentityMismatch;
    if(binding.generated.load_end_exclusive<=binding.generated.load_start)return DispatchStatus::InvalidBinding;
    const auto destination_bytes64=binding.generated.load_end_exclusive-binding.generated.load_start;
    if(destination_bytes64>std::numeric_limits<std::uint32_t>::max())return DispatchStatus::InvalidBinding;
    const auto destination_bytes=static_cast<std::uint32_t>(destination_bytes64);std::string source_hash,destination_hash;
    try{if(!hash_bus_range(impl_->bus,binding.source_start,binding.source_byte_count,source_hash)||!hash_bus_range(impl_->bus,binding.generated.load_start,destination_bytes,destination_hash))return DispatchStatus::LoadRejected;}catch(...){return DispatchStatus::LoadRejected;}
    if(source_hash!=binding.source_sha256||destination_hash!=binding.destination_sha256)return DispatchStatus::ExecutableHashMismatch;
    const std::uint64_t candidate_end=binding.generated.load_end_exclusive;
    std::size_t target_index=impl_->copied_epoch_count;
    for(std::size_t index=0U;index<impl_->copied_epoch_count;++index){
        auto& existing=impl_->copied_epochs[index];
        const std::uint64_t existing_end=static_cast<std::uint64_t>(existing.stamp.virtual_start)+existing.stamp.byte_count;
        const bool overlaps=binding.generated.load_start<existing_end&&existing.stamp.virtual_start<candidate_end;
        if(!overlaps)continue;
        if(impl_->bus.validate_code_region(existing.stamp,existing.variant)!=memory::CodeRegionValidity::Current){
            std::string current_hash;
            try{
                if(!hash_bus_range(impl_->bus,existing.stamp.virtual_start,
                                   existing.stamp.byte_count,current_hash)){
                    existing.bound=false;
                    continue;
                }
            }catch(...){
                existing.bound=false;
                continue;
            }
            if(current_hash!=existing.destination_sha256){
                existing.bound=false;
                continue;
            }
            const auto refreshed=impl_->bus.capture_code_region(
                existing.stamp.virtual_start,existing.stamp.byte_count,
                existing.variant);
            if(!refreshed.ok()){
                existing.bound=false;
                continue;
            }
            existing.stamp=refreshed.stamp;
        }
        if(binding.generated.load_start==existing.stamp.virtual_start&&candidate_end==existing_end)return DispatchStatus::InvalidBinding;
    }
    if(target_index==Impl::kMaximumCopiedEpochs)return DispatchStatus::LoadRejected;
    auto& epoch=impl_->copied_epochs[target_index];
    const auto bound=epoch.dispatcher.bind(binding.variant_identity_sha256,binding.generated);if(bound!=DispatchStatus::Bound)return bound;
    const auto capture=impl_->bus.capture_code_region(binding.generated.load_start,destination_bytes,variant);if(!capture.ok())return DispatchStatus::LoadRejected;
    try{epoch.destination_sha256.assign(binding.destination_sha256);}catch(...){return DispatchStatus::LoadRejected;}
    epoch.stamp=capture.stamp;epoch.variant=variant;epoch.bound=true;if(target_index==impl_->copied_epoch_count)++impl_->copied_epoch_count;return DispatchStatus::Bound;
}

std::uint32_t Runtime::bios_pc() const noexcept {
    return impl_ != nullptr ? impl_->bios_cpu.pc : 0U;
}

std::uint32_t Runtime::last_bios_instruction_count() const noexcept {
    return impl_ != nullptr ? impl_->last_bios_instruction_count : 0U;
}

Runtime::GeneratedExecutionSnapshot Runtime::bios_execution_snapshot() const noexcept {
    if (impl_ == nullptr) return {};
    return {impl_->bios_cpu.pc, impl_->bios_cpu.cop0_status,
            impl_->bios_cpu.cop0_cause, impl_->bios_cpu.cop0_epc,
            impl_->bios_cpu.cop0_bad_vaddr, impl_->bios_cpu.instruction_pc,
            impl_->bios_cpu.branch_pc, impl_->bios_cpu.in_delay_slot,
            impl_->generated_memory_reads,
            impl_->generated_memory_writes,
            impl_->generated_cache_isolated_writes,
            impl_->generated_device_reads,
            impl_->generated_device_writes};
}

DispatchStatus Runtime::load_authenticated_executable(
    std::string_view selected_variant_identity_sha256,
    std::span<const std::uint8_t> executable,
    std::uint32_t selected_entry_pc,
    const GeneratedExecutableBinding& binding) noexcept {
    if (!operational() || impl_->executable_loaded ||
        binding.load_end_exclusive <= binding.load_start ||
        binding.load_end_exclusive > (std::uint64_t{1} << 32U) ||
        executable.size() != binding.load_end_exclusive - binding.load_start) {
        return DispatchStatus::LoadRejected;
    }
    const auto start = memory::translate_address(binding.load_start);
    if (!start.ok() || start.physical_address >= memory::kRamBytes ||
        executable.size() > memory::kRamBytes - start.physical_address) {
        return DispatchStatus::LoadRejected;
    }
    try {
        if (sha256_hex(executable) != binding.executable_sha256) {
            return DispatchStatus::ExecutableHashMismatch;
        }
    } catch (...) {
        return DispatchStatus::LoadRejected;
    }
    const auto root = std::lower_bound(binding.dispatch_roots.begin(),
                                       binding.dispatch_roots.end(), selected_entry_pc);
    if (root == binding.dispatch_roots.end() || *root != selected_entry_pc) {
        return DispatchStatus::UnknownCode;
    }
    const DispatchStatus bound = impl_->generated_dispatcher.bind(
        selected_variant_identity_sha256, binding);
    if (bound != DispatchStatus::Bound) {
        return bound;
    }
    for (std::size_t index = 0; index < executable.size(); ++index) {
        if (!impl_->bus.write8(binding.load_start + static_cast<std::uint32_t>(index),
                               executable[index]).ok()) {
            impl_->terminal_status = Status::ExecutableRejected;
            return DispatchStatus::LoadRejected;
        }
    }
    impl_->generated_cpu.pc = selected_entry_pc;
    impl_->generated_cpu.next_pc = selected_entry_pc + 4U;
    impl_->generated_cpu.memory_context = impl_.get();
    impl_->generated_cpu.read8=&Impl::generated_read8;impl_->generated_cpu.read16=&Impl::generated_read16;impl_->generated_cpu.read32=&Impl::generated_read32;
    impl_->generated_cpu.write8=&Impl::generated_write8;impl_->generated_cpu.write16=&Impl::generated_write16;
    impl_->generated_cpu.write32 = &Impl::generated_write32;
    Impl::bind_generated_cop2(*impl_,impl_->generated_cpu);
    impl_->executable_loaded = true;
    return DispatchStatus::Bound;
}

DispatchStatus Runtime::dispatch_selected(bool branch_taken) noexcept {
    if (!operational() || !impl_->executable_loaded) {
        return DispatchStatus::LoadRejected;
    }
    impl_->generated_cpu.branch_taken = branch_taken;
    impl_->generated_cpu.issue_cycle=impl_->clock.retirement_total();
    impl_->generated_cpu.cop0_cause=(impl_->generated_cpu.cop0_cause&~0x0000FC00U)|((static_cast<std::uint32_t>(impl_->interrupt_lines)<<10U)&0x0000FC00U);
    impl_->active_generated_cpu=&impl_->generated_cpu;
    const auto result=impl_->generated_dispatcher.dispatch(impl_->generated_cpu);
    impl_->active_generated_cpu=nullptr;
    if(result==DispatchStatus::Executed)(void)impl_->advance_to(impl_->generated_cpu.issue_cycle);
    return result;
}

DispatchStatus Runtime::bind_authenticated_resident_executable(
    std::string_view selected_variant_identity_sha256,
    std::uint32_t selected_entry_pc,
    const GeneratedExecutableBinding& binding) noexcept {
    if (!operational() || impl_->executable_loaded ||
        binding.load_end_exclusive <= binding.load_start ||
        binding.load_end_exclusive - binding.load_start > memory::kRamBytes) {
        return DispatchStatus::LoadRejected;
    }
    const auto root = std::lower_bound(binding.dispatch_roots.begin(),
        binding.dispatch_roots.end(), selected_entry_pc);
    if (root == binding.dispatch_roots.end() || *root != selected_entry_pc) {
        return DispatchStatus::UnknownCode;
    }
    try {
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(binding.load_end_exclusive - binding.load_start));
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto value = impl_->bus.read8(binding.load_start +
                static_cast<std::uint32_t>(index));
            if (!value.ok()) return DispatchStatus::LoadRejected;
            bytes[index] = value.value;
        }
        if (sha256_hex(bytes) != binding.executable_sha256) {
            return DispatchStatus::ExecutableHashMismatch;
        }
    } catch (...) { return DispatchStatus::LoadRejected; }
    const DispatchStatus bound = impl_->generated_dispatcher.bind(
        selected_variant_identity_sha256, binding);
    if (bound != DispatchStatus::Bound) return bound;
    impl_->generated_cpu = impl_->bios_cpu;
    impl_->generated_cpu.pc = selected_entry_pc;
    impl_->generated_cpu.next_pc = selected_entry_pc + 4U;
    impl_->generated_cpu.memory_context = impl_.get();
    impl_->generated_cpu.read8=&Impl::generated_read8; impl_->generated_cpu.read16=&Impl::generated_read16; impl_->generated_cpu.read32=&Impl::generated_read32;
    impl_->generated_cpu.write8=&Impl::generated_write8; impl_->generated_cpu.write16=&Impl::generated_write16; impl_->generated_cpu.write32=&Impl::generated_write32;
    Impl::bind_generated_cop2(*impl_, impl_->generated_cpu);
    impl_->executable_loaded = true;
    return DispatchStatus::Bound;
}

std::uint32_t Runtime::selected_pc() const noexcept {
    return impl_ != nullptr ? impl_->generated_cpu.pc : 0U;
}

std::uint32_t Runtime::last_selected_instruction_count() const noexcept {
    return impl_ != nullptr ? impl_->generated_dispatcher.last_instruction_count() : 0U;
}

ReadResult Runtime::read8(AccessContext context,
                          std::uint32_t address) noexcept {
    if (!operational()) {
        return {configured() ? map_status(status()) : AccessFault::BusError,
                0U, 0U};
    }
    AccessGuard guard(impl_->in_public_access);
    if (!guard.acquired()) {
        return {AccessFault::ReentrantAccess, 0U, 0U};
    }
    AccessFault fault = AccessFault::None;
    if (!impl_->sync_for_access(context, address, memory::AccessWidth::Byte,
                                fault)) {
        return {fault, 0U, 0U};
    }
    if (is_cache_control(address, memory::AccessWidth::Byte)) {
        return cache_read(*impl_, address, memory::AccessWidth::Byte);
    }
    if (is_expansion_one(address, memory::AccessWidth::Byte)) {
        return {AccessFault::None, 0xffU, 0U};
    }
    const auto result = impl_->bus.read8(address);
    return {map_bus_status(result.status), result.value, 0U};
}

ReadResult Runtime::read16(AccessContext context,
                           std::uint32_t address) noexcept {
    if (!operational()) {
        return {configured() ? map_status(status()) : AccessFault::BusError,
                0U, 0U};
    }
    AccessGuard guard(impl_->in_public_access);
    if (!guard.acquired()) {
        return {AccessFault::ReentrantAccess, 0U, 0U};
    }
    AccessFault fault = AccessFault::None;
    if (!impl_->sync_for_access(context, address, memory::AccessWidth::Halfword,
                                fault)) {
        return {fault, 0U, 0U};
    }
    if (is_cache_control(address, memory::AccessWidth::Halfword)) {
        return cache_read(*impl_, address, memory::AccessWidth::Halfword);
    }
    if (is_expansion_one(address, memory::AccessWidth::Halfword)) {
        return {AccessFault::None, 0xffffU, 0U};
    }
    const auto result = impl_->bus.read16(address);
    return {map_bus_status(result.status), result.value, 0U};
}

ReadResult Runtime::read32(AccessContext context,
                           std::uint32_t address) noexcept {
    if (!operational()) {
        return {configured() ? map_status(status()) : AccessFault::BusError,
                0U, 0U};
    }
    AccessGuard guard(impl_->in_public_access);
    if (!guard.acquired()) {
        return {AccessFault::ReentrantAccess, 0U, 0U};
    }
    AccessFault fault = AccessFault::None;
    if (!impl_->sync_for_access(context, address, memory::AccessWidth::Word,
                                fault)) {
        return {fault, 0U, 0U};
    }
    if (is_cache_control(address, memory::AccessWidth::Word)) {
        return cache_read(*impl_, address, memory::AccessWidth::Word);
    }
    if (is_expansion_one(address, memory::AccessWidth::Word)) {
        return {AccessFault::None, 0xffff'ffffU, 0U};
    }
    const auto result = impl_->bus.read32(address);
    return {map_bus_status(result.status), result.value, 0U};
}

WriteResult Runtime::write8(AccessContext context,
                            std::uint32_t address,
                            std::uint8_t value) noexcept {
    if (!operational()) {
        return {configured() ? map_status(status()) : AccessFault::BusError,
                0U};
    }
    AccessGuard guard(impl_->in_public_access);
    if (!guard.acquired()) {
        return {AccessFault::ReentrantAccess, 0U};
    }
    AccessFault fault = AccessFault::None;
    if (!impl_->sync_for_access(context, address, memory::AccessWidth::Byte,
                                fault)) {
        return {fault, 0U};
    }
    if (is_cache_control(address, memory::AccessWidth::Byte)) {
        return cache_write(*impl_, address, memory::AccessWidth::Byte, value);
    }
    const auto result = impl_->bus.write8(address, value);
    return {map_bus_status(result.status), 0U};
}

WriteResult Runtime::write16(AccessContext context,
                             std::uint32_t address,
                             std::uint16_t value) noexcept {
    if (!operational()) {
        return {configured() ? map_status(status()) : AccessFault::BusError,
                0U};
    }
    AccessGuard guard(impl_->in_public_access);
    if (!guard.acquired()) {
        return {AccessFault::ReentrantAccess, 0U};
    }
    AccessFault fault = AccessFault::None;
    if (!impl_->sync_for_access(context, address, memory::AccessWidth::Halfword,
                                fault)) {
        return {fault, 0U};
    }
    if (is_cache_control(address, memory::AccessWidth::Halfword)) {
        return cache_write(*impl_, address, memory::AccessWidth::Halfword,
                           value);
    }
    const auto result = impl_->bus.write16(address, value);
    return {map_bus_status(result.status), 0U};
}

WriteResult Runtime::write32(AccessContext context,
                             std::uint32_t address,
                             std::uint32_t value) noexcept {
    if (!operational()) {
        return {configured() ? map_status(status()) : AccessFault::BusError,
                0U};
    }
    AccessGuard guard(impl_->in_public_access);
    if (!guard.acquired()) {
        return {AccessFault::ReentrantAccess, 0U};
    }
    AccessFault fault = AccessFault::None;
    if (!impl_->sync_for_access(context, address, memory::AccessWidth::Word,
                                fault)) {
        return {fault, 0U};
    }
    if (is_cache_control(address, memory::AccessWidth::Word)) {
        return cache_write(*impl_, address, memory::AccessWidth::Word, value);
    }
    const auto result = impl_->bus.write32(address, value);
    return {map_bus_status(result.status), 0U};
}

WriteResult Runtime::write_masked32(AccessContext context,
                                    std::uint32_t aligned_address,
                                    std::uint32_t value,
                                    std::uint8_t byte_mask) noexcept {
    if (!operational()) {
        return {configured() ? map_status(status()) : AccessFault::BusError,
                0U};
    }
    AccessGuard guard(impl_->in_public_access);
    if (!guard.acquired()) {
        return {AccessFault::ReentrantAccess, 0U};
    }
    if ((aligned_address & 3U) != 0U || byte_mask == 0U ||
        (byte_mask & 0xf0U) != 0U) {
        return {AccessFault::AddressError, 0U};
    }
    AccessFault fault = AccessFault::None;
    if (!impl_->sync_for_access(context, aligned_address,
                                memory::AccessWidth::Word, fault)) {
        return {fault, 0U};
    }
    if (is_cache_control(aligned_address, memory::AccessWidth::Word)) {
        std::uint32_t merged = impl_->cache_control;
        for (std::uint32_t lane = 0U; lane < 4U; ++lane) {
            if ((byte_mask & (1U << lane)) != 0U) {
                const std::uint32_t lane_mask = 0xffU << (lane * 8U);
                merged = (merged & ~lane_mask) | (value & lane_mask);
            }
        }
        impl_->cache_control = merged;
        return {AccessFault::None, 0U};
    }
    if (is_device_access(aligned_address, memory::AccessWidth::Word)) {
        return {AccessFault::BusError, 0U};
    }
    const auto current = impl_->bus.read32(aligned_address);
    if (!current.ok()) {
        return {map_bus_status(current.status), 0U};
    }
    std::uint32_t merged = current.value;
    for (std::uint32_t lane = 0U; lane < 4U; ++lane) {
        if ((byte_mask & (1U << lane)) != 0U) {
            const std::uint32_t lane_mask = 0xffU << (lane * 8U);
            merged = (merged & ~lane_mask) | (value & lane_mask);
        }
    }
    const auto result = impl_->bus.write32(aligned_address, merged);
    return {map_bus_status(result.status), 0U};
}

ClockAdvance Runtime::advance_to_retirement_cycle(
    std::uint64_t raw_cpu_cycle) noexcept {
    if (!configured()) {
        return {};
    }
    if (!operational()) {
        ClockAdvance result{};
        result.status = status();
        result.retirement_total = current_retirement_cycle();
        result.native_total = current_native_cycle();
        return result;
    }
    if (impl_->in_public_access) {
        ClockAdvance result{};
        result.status = Status::ReentrantAccess;
        return result;
    }
    AccessGuard guard(impl_->in_public_access);
    return impl_->advance_to(raw_cpu_cycle);
}

DeviceDeadline Runtime::next_device_deadline() const noexcept {
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->next_deadline();
}

std::uint64_t Runtime::current_retirement_cycle() const noexcept {
    return impl_ != nullptr ? impl_->clock.retirement_total() : 0U;
}

std::uint64_t Runtime::current_native_cycle() const noexcept {
    return impl_ != nullptr ? impl_->clock.native_total() : 0U;
}

std::uint8_t Runtime::hardware_interrupt_lines() const noexcept {
    return impl_ != nullptr ? impl_->interrupt_lines : 0U;
}

bool Runtime::cpu_interrupt_line() const noexcept {
    return impl_ != nullptr && impl_->soc.interrupts().cpu_line();
}

std::size_t Runtime::queued_audio_samples() const noexcept {
    return impl_ != nullptr ? impl_->audio_count : 0U;
}

std::uint64_t Runtime::produced_audio_samples() const noexcept {
    return impl_ != nullptr ? impl_->produced_audio : 0U;
}

std::size_t Runtime::drain_audio(
    std::span<StereoSample> destination) noexcept {
    if (impl_ == nullptr || impl_->in_public_access) {
        return 0U;
    }
    AccessGuard guard(impl_->in_public_access);
    const std::size_t count = std::min(destination.size(), impl_->audio_count);
    for (std::size_t index = 0U; index < count; ++index) {
        destination[index] = impl_->audio[impl_->audio_read];
        impl_->audio_read = (impl_->audio_read + 1U) % impl_->audio.size();
    }
    impl_->audio_count -= count;
    return count;
}

memory::Bus& Runtime::bus() noexcept { return impl_->bus; }
const memory::Bus& Runtime::bus() const noexcept { return impl_->bus; }
soc::SystemOnChip& Runtime::soc() noexcept { return impl_->soc; }
const soc::SystemOnChip& Runtime::soc() const noexcept { return impl_->soc; }
gte::Gte& Runtime::gte() noexcept { return impl_->gte; }
const gte::Gte& Runtime::gte() const noexcept { return impl_->gte; }
gpu::FrontEnd& Runtime::gpu() noexcept { return impl_->gpu; }
const gpu::FrontEnd& Runtime::gpu() const noexcept { return impl_->gpu; }
mdec::Device& Runtime::mdec() noexcept { return impl_->mdec; }
const mdec::Device& Runtime::mdec() const noexcept { return impl_->mdec; }
sio::Sio0& Runtime::sio0() noexcept { return impl_->sio0; }
const sio::Sio0& Runtime::sio0() const noexcept { return impl_->sio0; }
spu::Device& Runtime::spu() noexcept { return impl_->spu; }
const spu::Device& Runtime::spu() const noexcept { return impl_->spu; }

} // namespace clean_aot::runtime
