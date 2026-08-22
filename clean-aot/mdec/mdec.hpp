#ifndef CLEAN_AOT_MDEC_MDEC_HPP
#define CLEAN_AOT_MDEC_MDEC_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace clean_aot::mdec {

enum class DmaWidth : std::uint8_t {
    Byte = 1,
    Halfword = 2,
    Word = 4,
};

enum class OutputDepth : std::uint8_t {
    FourBit = 0,
    EightBit = 1,
    TwentyFourBit = 2,
    FifteenBit = 3,
};

enum class Fault : std::uint8_t {
    None = 0,
    CommandBusy,
    InputFifoFull,
    OutputFifoEmpty,
    DmaNotRequested,
    UnsupportedDmaWidth,
    DmaAlignment,
    TruncatedStream,
    MalformedStream,
    OutputFifoFull,
    ArithmeticOverflow,
    InvalidSnapshot,
};

struct TransferResult {
    std::size_t words = 0;
    std::uint64_t cycles = 0;
    Fault fault = Fault::None;
    bool complete = false;
};

// A snapshot is intentionally a fixed-size value. It contains no pointers,
// allocations, host handles, or references to the device that created it.
struct Snapshot {
    static constexpr std::size_t kStorageBytes = 8192;
    std::array<std::byte, kStorageBytes> storage{};
};

class Device final {
public:
    // The documented maximum MDEC DMA slice is 0x20 words, matching the
    // device-side input buffer boundary exposed to the DMA controller.
    static constexpr std::size_t kInputCapacityWords = 32;
    static constexpr std::size_t kOutputCapacityWords = 256;

    Device() noexcept;

    void reset() noexcept;

    // MDEC0 writes are 32-bit bus writes. The same entry accepts the command
    // word while idle and packed little-endian parameter words while busy.
    TransferResult write_command_word(std::uint32_t word) noexcept;
    TransferResult write_control_word(std::uint32_t word) noexcept;

    TransferResult read_data_word(std::uint32_t& word) noexcept;

    // DMA0/DMA1 are 32-bit word streams. Other widths are rejected rather
    // than silently inventing byte-lane behavior for this device.
    TransferResult dma_write_words(std::span<const std::uint32_t> words) noexcept;
    TransferResult dma_read_words(std::span<std::uint32_t> words) noexcept;

    TransferResult dma_write_bytes(std::span<const std::byte> bytes,
                                   DmaWidth width) noexcept;
    TransferResult dma_read_bytes(std::span<std::byte> bytes,
                                  DmaWidth width) noexcept;

    [[nodiscard]] std::uint32_t status() const noexcept;
    [[nodiscard]] Fault fault() const noexcept;
    [[nodiscard]] bool command_busy() const noexcept;
    [[nodiscard]] bool dma_in_enabled() const noexcept;
    [[nodiscard]] bool dma_out_enabled() const noexcept;
    [[nodiscard]] std::size_t input_bytes_available() const noexcept;
    [[nodiscard]] std::size_t output_words_available() const noexcept;
    [[nodiscard]] std::uint16_t remaining_parameter_words_minus_one() const noexcept;
    [[nodiscard]] std::uint64_t cycles() const noexcept;
    [[nodiscard]] std::uint64_t digest() const noexcept;

    [[nodiscard]] std::uint8_t quantization_luma(std::size_t index) const noexcept;
    [[nodiscard]] std::uint8_t quantization_chroma(std::size_t index) const noexcept;
    [[nodiscard]] std::int16_t scale_table(std::size_t index) const noexcept;

    bool save_snapshot(Snapshot& snapshot) const noexcept;
    bool load_snapshot(const Snapshot& snapshot) noexcept;

private:
    enum class Command : std::uint8_t {
        Idle = 0,
        Decode = 1,
        Quantization = 2,
        Scale = 3,
    };

    enum class BlockPhase : std::uint8_t {
        NeedHeader = 0,
        NeedAc = 1,
    };

    struct State {
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::array<std::uint8_t, kInputCapacityWords * 4> input{};
        std::uint16_t input_head = 0;
        std::uint16_t input_tail = 0;
        std::uint16_t input_count = 0;
        std::array<std::uint32_t, kOutputCapacityWords> output{};
        std::array<std::uint8_t, kOutputCapacityWords> output_block{};
        std::uint16_t output_head = 0;
        std::uint16_t output_tail = 0;
        std::uint16_t output_count = 0;
        std::array<std::uint8_t, 64> quant_luma{};
        std::array<std::uint8_t, 64> quant_chroma{};
        std::array<std::int16_t, 64> scale{};
        std::array<std::array<std::int32_t, 64>, 6> blocks{};
        std::array<std::int32_t, 64> work_block{};
        Command command = Command::Idle;
        BlockPhase block_phase = BlockPhase::NeedHeader;
        OutputDepth depth = OutputDepth::FourBit;
        Fault latched_fault = Fault::None;
        std::uint32_t command_word = 0;
        // remaining_bytes tracks bytes not yet consumed by the decoder;
        // receive_remaining_bytes separately tracks parameter bytes not yet
        // accepted at MDEC0. Keeping both is required while output
        // backpressure stalls processing after DMA0 delivered its final word.
        std::uint32_t remaining_bytes = 0;
        std::uint32_t receive_remaining_bytes = 0;
        std::uint32_t expected_bytes = 0;
        std::uint16_t idle_status_low = 0xFFFFU;
        std::uint8_t decode_block = 0;
        std::uint8_t coefficient_position = 0;
        std::uint8_t q_scale = 0;
        std::uint8_t table_index = 0;
        bool output_signed = false;
        bool output_bit15 = false;
        bool color_quantization = false;
        bool input_dma_enabled = false;
        bool output_dma_enabled = false;
        bool macroblock_started = false;
        bool input_last_word_received = false;
        std::uint64_t cycles = 0;
    };

    void latch_fault(Fault fault) noexcept;
    bool add_cycles(std::uint64_t cycles) noexcept;
    bool push_input_word(std::uint32_t word) noexcept;
    bool peek_input_halfword(std::uint16_t& value) const noexcept;
    bool consume_input_byte(std::uint8_t& value) noexcept;
    bool consume_input_halfword(std::uint16_t& value) noexcept;
    void pump() noexcept;
    bool process_quantization() noexcept;
    bool process_scale() noexcept;
    bool process_decode() noexcept;
    bool process_decode_step() noexcept;
    bool finish_block() noexcept;
    bool emit_macroblock() noexcept;
    bool emit_monochrome() noexcept;
    bool emit_color() noexcept;
    bool append_output_bytes(const std::uint8_t* bytes,
                             std::size_t size,
                             std::uint8_t block) noexcept;
    void finish_command() noexcept;
    [[nodiscard]] bool snapshot_valid(const State& candidate) const noexcept;
    [[nodiscard]] std::uint8_t current_block() const noexcept;
    [[nodiscard]] std::uint16_t status_remaining() const noexcept;

    State state_{};
};

} // namespace clean_aot::mdec

#endif // CLEAN_AOT_MDEC_MDEC_HPP
