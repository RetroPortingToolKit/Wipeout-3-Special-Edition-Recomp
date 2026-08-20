#ifndef CLEAN_AOT_GTE_HPP
#define CLEAN_AOT_GTE_HPP

#include <array>
#include <cstdint>

namespace clean_aot::gte {

enum class Command : std::uint8_t {
    Unknown = 0xffU,
    Rtps = 0x01U,
    Nclip = 0x06U,
    Op = 0x0cU,
    Dpcs = 0x10U,
    Intpl = 0x11U,
    Mvmva = 0x12U,
    Ncds = 0x13U,
    Cdp = 0x14U,
    Ncdt = 0x16U,
    Nccs = 0x1bU,
    Cc = 0x1cU,
    Ncs = 0x1eU,
    Nct = 0x20U,
    Sqr = 0x28U,
    Dcpl = 0x29U,
    Dpct = 0x2aU,
    Avsz3 = 0x2dU,
    Avsz4 = 0x2eU,
    Rtpt = 0x30U,
    Gpf = 0x3dU,
    Gpl = 0x3eU,
    Ncct = 0x3fU,
};

struct DecodedInstruction final {
    std::uint32_t raw = 0U;
    Command command = Command::Unknown;
    bool shift_fraction = false;
    bool limit_negative = false;
    std::uint8_t matrix_selector = 0U;
    std::uint8_t vector_selector = 0U;
    std::uint8_t translation_selector = 0U;
};

struct ExecutionReceipt final {
    bool recognized = false;
    Command command = Command::Unknown;
    std::uint8_t cycles = 0U;
    std::uint32_t flag = 0U;
    std::uint64_t state_digest = 0U;
};

struct Snapshot final {
    std::array<std::array<std::int16_t, 3U>, 3U> vectors{};
    std::uint32_t rgbc = 0U;
    std::uint16_t otz = 0U;
    std::int16_t ir0 = 0;
    std::array<std::int16_t, 3U> ir{};
    std::array<std::array<std::int16_t, 2U>, 3U> sxy{};
    std::array<std::uint16_t, 4U> sz{};
    std::array<std::uint32_t, 4U> rgb{};
    std::array<std::int64_t, 4U> mac{};
    std::uint32_t lzcs = 0U;
    std::uint32_t lzcr = 0U;
    std::array<std::array<std::int16_t, 3U>, 3U> rotation{};
    std::array<std::int32_t, 3U> translation{};
    std::array<std::array<std::int16_t, 3U>, 3U> light{};
    std::array<std::int32_t, 3U> background{};
    std::array<std::array<std::int16_t, 3U>, 3U> color{};
    std::array<std::int32_t, 3U> far_color{};
    std::int32_t ofx = 0;
    std::int32_t ofy = 0;
    std::uint16_t h = 0U;
    std::int16_t dqa = 0;
    std::int32_t dqb = 0;
    std::int16_t zsf3 = 0;
    std::int16_t zsf4 = 0;
    std::uint32_t flag = 0U;
};

class Gte final {
public:
    static constexpr std::uint32_t FlagSummaryMask = 0x7f87e000U;

    Gte() noexcept;

    void reset() noexcept;

    static DecodedInstruction decode(std::uint32_t instruction) noexcept;
    static std::uint8_t cycles(Command command) noexcept;
    static const char* command_name(Command command) noexcept;

    ExecutionReceipt execute(std::uint32_t instruction) noexcept;
    ExecutionReceipt execute(const DecodedInstruction& instruction) noexcept;

    std::uint32_t read_data(std::uint8_t index) const noexcept;
    void write_data(std::uint8_t index, std::uint32_t value) noexcept;
    std::uint32_t read_control(std::uint8_t index) const noexcept;
    void write_control(std::uint8_t index, std::uint32_t value) noexcept;

    Snapshot snapshot() const noexcept;
    std::uint64_t state_digest() const noexcept;

private:
    using S16 = std::int16_t;
    using U16 = std::uint16_t;
    using S32 = std::int32_t;
    using S64 = std::int64_t;

    static constexpr S64 MacPositiveLimit = S64{1} << 43U;
    static constexpr S64 MacNegativeLimit = -MacPositiveLimit;

    std::array<std::array<S16, 3U>, 3U> vectors_{};
    std::uint32_t rgbc_ = 0U;
    U16 otz_ = 0U;
    S16 ir0_ = 0;
    std::array<S16, 3U> ir_{};
    std::array<std::array<S16, 2U>, 3U> sxy_{};
    std::array<U16, 4U> sz_{};
    std::array<std::uint32_t, 4U> rgb_{};
    std::uint32_t res1_ = 0U;
    std::array<S64, 4U> mac_{};
    std::uint32_t lzcs_ = 0U;
    std::uint32_t lzcr_ = 0U;

    std::array<std::array<S16, 3U>, 3U> rotation_{};
    std::array<S32, 3U> translation_{};
    std::array<std::array<S16, 3U>, 3U> light_{};
    std::array<S32, 3U> background_{};
    std::array<std::array<S16, 3U>, 3U> color_{};
    std::array<S32, 3U> far_color_{};
    S32 ofx_ = 0;
    S32 ofy_ = 0;
    U16 h_ = 0U;
    S16 dqa_ = 0;
    S32 dqb_ = 0;
    S16 zsf3_ = 0;
    S16 zsf4_ = 0;
    std::uint32_t flag_ = 0U;

    static Command decode_command(std::uint32_t opcode) noexcept;
    static S64 arithmetic_shift_right(S64 value, unsigned shift) noexcept;
    static S64 clamp(S64 value, S64 minimum, S64 maximum) noexcept;
    static S32 sign_extend_16(U16 value) noexcept;
    static S16 low_s16(std::uint32_t value) noexcept;
    static U16 low_u16(std::uint32_t value) noexcept;
    static std::uint32_t pack_s16(S16 low, S16 high) noexcept;
    static unsigned leading_count(std::uint32_t value) noexcept;
    static std::uint32_t color_pack(std::uint32_t red,
                                    std::uint32_t green,
                                    std::uint32_t blue,
                                    std::uint32_t code) noexcept;

    void clear_flags() noexcept;
    void set_flag(std::uint32_t bit) noexcept;
    std::uint32_t visible_flag() const noexcept;
    void record_mac(unsigned index, S64 value, S64 overflow_probe) noexcept;
    void set_ir(unsigned index,
                S64 value,
                bool limit_negative,
                S64 flag_probe) noexcept;
    void set_ir0(S64 value) noexcept;
    void set_sz3(S64 value) noexcept;
    void push_sxy(S64 x, S64 y) noexcept;
    void push_rgb(S64 red, S64 green, S64 blue) noexcept;

    const std::array<S16, 3U>& selected_vector(std::uint8_t selector) const noexcept;
    const std::array<std::array<S16, 3U>, 3U>& selected_matrix(
        std::uint8_t selector) const noexcept;
    S64 selected_translation(std::uint8_t selector, unsigned row) const noexcept;
    void make_reserved_matrix(std::array<std::array<S16, 3U>, 3U>& matrix) const noexcept;

    void execute_rtps(const DecodedInstruction& instruction,
                      unsigned vector_index) noexcept;
    void execute_rtpt(const DecodedInstruction& instruction) noexcept;
    void execute_mvmva(const DecodedInstruction& instruction) noexcept;
    void execute_nclip() noexcept;
    void execute_avsz(unsigned count) noexcept;
    void execute_op(const DecodedInstruction& instruction) noexcept;
    void execute_sqr(const DecodedInstruction& instruction) noexcept;
    void execute_normal_color(const DecodedInstruction& instruction,
                              unsigned vector_index,
                              bool with_color,
                              bool with_depth) noexcept;
    void execute_color_color(const DecodedInstruction& instruction,
                             bool with_depth) noexcept;
    void execute_depth_cue(const DecodedInstruction& instruction,
                           const std::array<S64, 3U>& base) noexcept;
    void execute_dpct(const DecodedInstruction& instruction) noexcept;
    void execute_gpf(const DecodedInstruction& instruction, bool with_base) noexcept;

    void normal_stage(const DecodedInstruction& instruction,
                      unsigned vector_index) noexcept;
    void color_matrix_stage(const DecodedInstruction& instruction) noexcept;
    std::array<S64, 3U> color_modulation() noexcept;
    void finish_color(const DecodedInstruction& instruction,
                      const std::array<S64, 3U>& base) noexcept;
    static std::uint32_t reciprocal_scale(U16 h,
                                          U16 sz,
                                          std::uint32_t& flags) noexcept;
};

} // namespace clean_aot::gte

#endif // CLEAN_AOT_GTE_HPP
