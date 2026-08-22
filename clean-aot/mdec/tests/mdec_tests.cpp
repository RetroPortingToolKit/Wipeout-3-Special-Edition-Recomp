#include "mdec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using clean_aot::mdec::Device;
using clean_aot::mdec::DmaWidth;
using clean_aot::mdec::Fault;
using clean_aot::mdec::OutputDepth;
using clean_aot::mdec::Snapshot;

constexpr std::uint32_t kStatusBit31 = std::uint32_t{1U} << 31U;
constexpr std::uint32_t kStatusBit15 = std::uint32_t{1U} << 15U;
constexpr std::uint32_t kResetStatus = kStatusBit31 | (4U << 16U);

class TestFailure final : public std::exception {
public:
    explicit TestFailure(std::string message) : message_(std::move(message)) {}

    [[nodiscard]] const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename T, typename U>
void require_equal(const T& actual, const U& expected, const std::string& message)
{
    if (!(actual == expected)) {
        throw TestFailure(message);
    }
}

constexpr std::uint16_t header(std::uint8_t q_scale, std::int32_t dc) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint32_t>(q_scale) << 10U) |
                                      (static_cast<std::uint32_t>(dc) & 0x03FFU));
}

constexpr std::uint16_t run_value(std::uint8_t run, std::int32_t value) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint32_t>(run) << 10U) |
                                      (static_cast<std::uint32_t>(value) & 0x03FFU));
}

std::uint32_t packed_halfwords(std::uint16_t low, std::uint16_t high) noexcept
{
    return static_cast<std::uint32_t>(low) |
           (static_cast<std::uint32_t>(high) << 16U);
}

std::vector<std::uint32_t> pack_halfwords(const std::vector<std::uint16_t>& halfwords)
{
    require((halfwords.size() % 2U) == 0U, "halfword fixture must be word aligned");
    std::vector<std::uint32_t> words;
    words.reserve(halfwords.size() / 2U);
    for (std::size_t index = 0U; index < halfwords.size(); index += 2U) {
        words.push_back(packed_halfwords(halfwords[index], halfwords[index + 1U]));
    }
    return words;
}

std::vector<std::uint16_t> make_block(std::uint8_t q_scale,
                                      std::int32_t dc,
                                      std::span<const std::pair<std::uint8_t, std::int32_t>> ac = {},
                                      bool eob = true)
{
    std::vector<std::uint16_t> halfwords;
    halfwords.push_back(header(q_scale, dc));
    for (const auto& [run, value] : ac) {
        halfwords.push_back(run_value(run, value));
    }
    if (eob) {
        halfwords.push_back(0xFE00U);
    }
    if ((halfwords.size() % 2U) != 0U) {
        halfwords.push_back(0xFE00U);
    }
    return halfwords;
}

std::uint32_t command(std::uint8_t opcode,
                      OutputDepth depth = OutputDepth::EightBit,
                      bool signed_output = false,
                      bool bit15 = false,
                      std::uint16_t parameter_words = 0U) noexcept
{
    return (static_cast<std::uint32_t>(opcode) << 29U) |
           (static_cast<std::uint32_t>(depth) << 27U) |
           (signed_output ? 0x04000000U : 0U) | (bit15 ? 0x02000000U : 0U) |
           static_cast<std::uint32_t>(parameter_words);
}

std::vector<std::uint32_t> make_mono_stream(std::size_t blocks,
                                            std::uint8_t q_scale = 0U,
                                            std::int32_t dc = 16,
                                            bool include_padding = false)
{
    std::vector<std::uint16_t> halfwords;
    for (std::size_t block = 0U; block < blocks; ++block) {
        const auto fixture = make_block(q_scale, dc);
        halfwords.insert(halfwords.end(), fixture.begin(), fixture.end());
    }
    if (include_padding) {
        halfwords.push_back(0xFE00U);
        halfwords.push_back(0xFE00U);
    }
    if ((halfwords.size() % 2U) != 0U) {
        halfwords.push_back(0xFE00U);
    }
    return pack_halfwords(halfwords);
}

std::vector<std::uint32_t> make_color_stream(std::int32_t cr,
                                             std::int32_t cb,
                                             std::int32_t y,
                                             std::uint8_t q_scale = 0U)
{
    std::vector<std::uint16_t> halfwords;
    const std::array<std::int32_t, 6> values = {cr, cb, y, y, y, y};
    for (const std::int32_t value : values) {
        const auto fixture = make_block(q_scale, value);
        halfwords.insert(halfwords.end(), fixture.begin(), fixture.end());
    }
    return pack_halfwords(halfwords);
}

std::vector<std::uint32_t> make_asymmetric_color_stream(
    const std::array<std::int32_t, 4>& luma)
{
    std::vector<std::uint16_t> halfwords;
    const std::array<std::int32_t, 6> values = {
        0, 0, luma[0], luma[1], luma[2], luma[3]};
    for (const std::int32_t value : values) {
        const auto fixture = make_block(0U, value);
        halfwords.insert(halfwords.end(), fixture.begin(), fixture.end());
    }
    return pack_halfwords(halfwords);
}

void send_direct(Device& device,
                 std::uint32_t command_word,
                 std::span<const std::uint32_t> words)
{
    const auto command_result = device.write_command_word(command_word);
    require(command_result.words == 1U && command_result.fault == Fault::None,
            "command write failed");
    for (const std::uint32_t word : words) {
        const auto result = device.write_command_word(word);
        require(result.words == 1U && result.fault == Fault::None, "parameter write failed");
    }
}

void load_quant_tables(Device& device, std::uint8_t luma, std::uint8_t chroma)
{
    std::vector<std::uint32_t> words(32U);
    const std::uint32_t luma_word = static_cast<std::uint32_t>(luma) * 0x01010101U;
    const std::uint32_t chroma_word = static_cast<std::uint32_t>(chroma) * 0x01010101U;
    std::fill_n(words.begin(), 16U, luma_word);
    std::fill(words.begin() + 16, words.end(), chroma_word);
    send_direct(device, command(2U) | 1U, words);
}

std::vector<std::uint32_t> drain(Device& device)
{
    std::vector<std::uint32_t> output;
    while (device.output_words_available() != 0U) {
        std::uint32_t word = 0U;
        const auto result = device.read_data_word(word);
        require(result.words == 1U && result.fault == Fault::None, "data read failed");
        output.push_back(word);
    }
    return output;
}

void test_reset_and_status()
{
    Device device;
    require_equal(device.status(), kResetStatus, "reset status");
    require_equal(device.fault(), Fault::None, "reset fault");
    require_equal(device.output_words_available(), 0U, "reset output FIFO");
    require(!device.command_busy(), "reset must be idle");

    const auto control = device.write_control_word(0x60000000U);
    require(control.complete, "control enable");
    require(device.dma_in_enabled() && device.dma_out_enabled(), "DMA enables");

    const auto no_op = device.write_command_word(command(0U, OutputDepth::FifteenBit, true,
                                                         true, 0x1234U));
    require(no_op.complete, "MDEC zero command");
    require((device.status() & 0x07800000U) == 0x07800000U,
            "format fields are reflected for the no-op command");
    require_equal(device.remaining_parameter_words_minus_one(), 0x1234U,
                  "no-op low status field");

    const auto decode = device.write_command_word(command(1U, OutputDepth::EightBit, false,
                                                           false, 1U));
    require(decode.complete, "decode command acceptance");
    require(device.command_busy(), "decode command busy");
    require((device.status() & 0x30000000U) == 0x30000000U,
            "decode busy and input request status");
    require_equal(device.remaining_parameter_words_minus_one(), 0U,
                  "one remaining parameter word");
    const auto stream = make_mono_stream(1U);
    const auto dma_result = device.dma_write_words(stream);
    require(dma_result.complete, "DMA input request boundary");
    require((device.status() & 0x08000000U) != 0U, "output DMA request status");
    require((device.status() & 0x20000000U) == 0U &&
                (device.status() & 0x08000000U) != 0U,
            "output request and completed-command status");
    (void)drain(device);
    require((device.status() & kStatusBit31) != 0U, "output empty after drain");
}

void test_quant_and_scale_loads()
{
    Device device;
    std::array<std::uint8_t, 128> quant{};
    for (std::size_t index = 0U; index < 64U; ++index) {
        quant[index] = static_cast<std::uint8_t>(index);
        quant[64U + index] = static_cast<std::uint8_t>(255U - index);
    }
    std::vector<std::uint32_t> quant_words;
    quant_words.reserve(32U);
    for (std::size_t index = 0U; index < quant.size(); index += 4U) {
        quant_words.push_back(static_cast<std::uint32_t>(quant[index]) |
                              (static_cast<std::uint32_t>(quant[index + 1U]) << 8U) |
                              (static_cast<std::uint32_t>(quant[index + 2U]) << 16U) |
                              (static_cast<std::uint32_t>(quant[index + 3U]) << 24U));
    }
    send_direct(device, command(2U) | 1U, quant_words);
    require_equal(device.quantization_luma(0U), 0U, "luma table first entry");
    require_equal(device.quantization_luma(63U), 63U, "luma table last entry");
    require_equal(device.quantization_chroma(0U), 255U, "chroma table first entry");
    require_equal(device.quantization_chroma(63U), 192U, "chroma table last entry");
    require_equal(device.remaining_parameter_words_minus_one(), 0xFFFFU,
                  "quant command completion");

    std::vector<std::uint16_t> scale_halfwords;
    scale_halfwords.reserve(64U);
    for (std::size_t index = 0U; index < 64U; ++index) {
        scale_halfwords.push_back(static_cast<std::uint16_t>(0x1000U + index * 8U + 7U));
    }
    const auto scale_words = pack_halfwords(scale_halfwords);
    send_direct(device, command(3U), scale_words);
    require_equal(device.scale_table(0U), static_cast<std::int16_t>(0x1000),
                  "scale low bits are ignored");
    require_equal(device.scale_table(63U), static_cast<std::int16_t>(0x11F8),
                  "scale table final entry");

    scale_halfwords[0] = 0xFEEFU;
    const auto negative_scale_words = pack_halfwords(scale_halfwords);
    send_direct(device, command(3U), negative_scale_words);
    require_equal(static_cast<std::uint16_t>(device.scale_table(0U)), 0xFEE8U,
                  "negative scale sign and low-bit masking");

    std::vector<std::uint16_t> zero_scale(64U, 0U);
    const auto zero_scale_words = pack_halfwords(zero_scale);
    Device zero_idct;
    send_direct(zero_idct, command(3U), zero_scale_words);
    const std::array<std::pair<std::uint8_t, std::int32_t>, 1> edge_ac = {
        std::pair<std::uint8_t, std::int32_t>{0U, -511},
    };
    const auto edge_halfwords = make_block(1U, 511, edge_ac);
    const auto edge_words = pack_halfwords(edge_halfwords);
    send_direct(zero_idct,
                command(1U, OutputDepth::EightBit, true, false,
                        static_cast<std::uint16_t>(edge_words.size())),
                edge_words);
    const auto zero_output = drain(zero_idct);
    require_equal(zero_output.size(), 16U, "zero scale output size");
    for (const std::uint32_t word : zero_output) {
        require_equal(word, 0U, "zero scale removes every coefficient");
    }
}

void test_monochrome_output_and_packing()
{
    const auto stream = make_mono_stream(1U, 0U, 16);
    Device eight_bit;
    send_direct(eight_bit, command(1U, OutputDepth::EightBit, false, false, 1U), stream);
    const auto eight_words = drain(eight_bit);
    require_equal(eight_words.size(), 16U, "8-bit mono output size");
    require_equal(eight_words.front(), 0x84848484U, "8-bit unsigned DC fixture");

    Device four_bit;
    send_direct(four_bit, command(1U, OutputDepth::FourBit, false, false, 1U), stream);
    const auto four_words = drain(four_bit);
    require_equal(four_words.size(), 8U, "4-bit mono output size");
    require_equal(four_words.front(), 0x88888888U, "4-bit nibble packing");

    Device signed_four;
    send_direct(signed_four, command(1U, OutputDepth::FourBit, true, false, 1U), stream);
    const auto signed_words = drain(signed_four);
    require_equal(signed_words.front(), 0x44444444U, "signed 4-bit packing");

    Device signed_eight;
    send_direct(signed_eight, command(1U, OutputDepth::EightBit, true, false, 1U), stream);
    const auto signed_eight_words = drain(signed_eight);
    require_equal(signed_eight_words.front(), 0x04040404U, "signed 8-bit DC fixture");
}

void test_reference_rle_idct_vectors()
{
    const std::array<std::pair<std::uint8_t, std::int32_t>, 1> ac = {
        std::pair<std::uint8_t, std::int32_t>{1U, 64},
    };
    const auto q_zero_halfwords = make_block(0U, 16, ac);
    const auto q_zero_words = pack_halfwords(q_zero_halfwords);
    Device q_zero;
    send_direct(q_zero,
                command(1U, OutputDepth::EightBit, false, false,
                        static_cast<std::uint16_t>(q_zero_words.size())),
                q_zero_words);
    const auto q_zero_output = drain(q_zero);
    constexpr std::array<std::uint32_t, 16> kQZeroGolden = {
        0x6F7B8C99U, 0x998C7B6FU, 0x6F7B8C99U, 0x998C7B6FU,
        0x6F7B8C99U, 0x998C7B6FU, 0x6F7B8C99U, 0x998C7B6FU,
        0x6F7B8C99U, 0x998C7B6FU, 0x6F7B8C99U, 0x998C7B6FU,
        0x6F7B8C99U, 0x998C7B6FU, 0x6F7B8C99U, 0x998C7B6FU,
    };
    require_equal(q_zero_output.size(), kQZeroGolden.size(), "q=0 golden size");
    for (std::size_t index = 0U; index < kQZeroGolden.size(); ++index) {
        require_equal(q_zero_output[index], kQZeroGolden[index], "q=0 natural-order golden");
    }

    const std::array<std::pair<std::uint8_t, std::int32_t>, 1> normal_ac = {
        std::pair<std::uint8_t, std::int32_t>{1U, 128},
    };
    const auto normal_halfwords = make_block(8U, 16, normal_ac);
    const auto normal_words = pack_halfwords(normal_halfwords);
    Device normal;
    send_direct(normal,
                command(1U, OutputDepth::EightBit, false, false,
                        static_cast<std::uint16_t>(normal_words.size())),
                normal_words);
    const auto normal_output = drain(normal);
    constexpr std::array<std::uint32_t, 16> kNormalGolden = {
        0x98989898U, 0x98989898U, 0x95959595U, 0x95959595U,
        0x8E8E8E8EU, 0x8E8E8E8EU, 0x86868686U, 0x86868686U,
        0x7E7E7E7EU, 0x7E7E7E7EU, 0x75757575U, 0x75757575U,
        0x6F6F6F6FU, 0x6F6F6F6FU, 0x6C6C6C6CU, 0x6C6C6C6CU,
    };
    require_equal(normal_output.size(), kNormalGolden.size(), "normal golden size");
    for (std::size_t index = 0U; index < kNormalGolden.size(); ++index) {
        require_equal(normal_output[index], kNormalGolden[index],
                      "normal inverse-zigzag golden");
    }
}

void test_monochrome_uses_luminance_quantization()
{
    const std::array<std::pair<std::uint8_t, std::int32_t>, 2> ac = {
        std::pair<std::uint8_t, std::int32_t>{0U, 7},
        std::pair<std::uint8_t, std::int32_t>{3U, -5},
    };
    const auto words = pack_halfwords(make_block(9U, 11, ac));

    Device low_chroma;
    load_quant_tables(low_chroma, 3U, 1U);
    send_direct(low_chroma,
                command(1U, OutputDepth::EightBit, false, false,
                        static_cast<std::uint16_t>(words.size())),
                words);
    const auto reference = drain(low_chroma);

    Device hostile_chroma;
    load_quant_tables(hostile_chroma, 3U, 251U);
    send_direct(hostile_chroma,
                command(1U, OutputDepth::EightBit, false, false,
                        static_cast<std::uint16_t>(words.size())),
                words);
    require_equal(drain(hostile_chroma), reference,
                  "monochrome Y output is independent of chroma quantization");

    Device chunked;
    load_quant_tables(chunked, 3U, 251U);
    require(chunked.write_command_word(
                command(1U, OutputDepth::EightBit, false, false,
                        static_cast<std::uint16_t>(words.size())))
                .complete,
            "chunked monochrome command");
    require(chunked.write_command_word(words.front()).complete,
            "chunked monochrome prefix");
    Snapshot snapshot{};
    require(chunked.save_snapshot(snapshot), "mid-block quant snapshot");
    Device restored;
    require(restored.load_snapshot(snapshot), "mid-block quant restore");
    for (std::size_t index = 1U; index < words.size(); ++index) {
        require(chunked.write_command_word(words[index]).complete,
                "chunked monochrome suffix");
        require(restored.write_command_word(words[index]).complete,
                "restored monochrome suffix");
    }
    require_equal(drain(chunked), reference, "chunked luminance-quant output");
    require_equal(drain(restored), reference, "restored luminance-quant output");
    require_equal(restored.digest(), chunked.digest(), "quant snapshot replay digest");
}

void test_color_conversion_and_flags()
{
    const auto stream = make_color_stream(511, 511, 511);

    Device progress;
    const auto progress_command = progress.write_command_word(
        command(1U, OutputDepth::TwentyFourBit, false, false, 6U));
    require(progress_command.complete, "color progress command");
    const auto first_block = progress.write_command_word(stream.front());
    require(first_block.complete, "first color block");
    require_equal((progress.status() >> 16U) & 0x07U, 5U,
                  "current block tracks the Cr-to-Cb transition");
    for (std::size_t index = 1U; index < stream.size(); ++index) {
        const auto result = progress.write_command_word(stream[index]);
        require(result.words == 1U && result.fault == Fault::None, "color progress input");
    }
    require_equal(progress.output_words_available(), 192U, "color progress output");
    require_equal((progress.status() >> 16U) & 0x07U, 0U,
                  "output current block is Y1");
    (void)drain(progress);

    Device color24;
    send_direct(color24, command(1U, OutputDepth::TwentyFourBit, false, false, 6U), stream);
    const auto words24 = drain(color24);
    require_equal(words24.size(), 192U, "24-bit color output size");
    const std::uint32_t first24 = words24.front();
    require_equal(first24 & 0xFFU, 0xFFU, "R channel saturates in unsigned 24-bit output");
    require(((first24 >> 8U) & 0xFFU) < 0xFFU,
            "G channel retains the independent chroma conversion");
    require_equal((first24 >> 16U) & 0xFFU, 0xFFU, "B channel saturates in unsigned output");

    Device color15_clear;
    send_direct(color15_clear, command(1U, OutputDepth::FifteenBit, false, false, 6U), stream);
    const auto words15_clear = drain(color15_clear);
    require_equal(words15_clear.size(), 128U, "15-bit color output size");
    require((words15_clear.front() & (kStatusBit31 | kStatusBit15)) == 0U,
            "bit15 clear output");

    Device color15_set;
    send_direct(color15_set, command(1U, OutputDepth::FifteenBit, false, true, 6U), stream);
    const auto words15_set = drain(color15_set);
    require((words15_set.front() & (kStatusBit31 | kStatusBit15)) ==
                (kStatusBit31 | kStatusBit15),
            "bit15 set output");

    Device color15_signed;
    send_direct(color15_signed, command(1U, OutputDepth::FifteenBit, true, false, 6U),
                stream);
    const auto signed15 = drain(color15_signed);
    require((signed15.front() & (kStatusBit31 | kStatusBit15)) == 0U,
            "signed output does not imply bit15");
    require((signed15.front() & 0x0000FFFFU) != (words15_clear.front() & 0x0000FFFFU),
            "signed and unsigned color packing differ");
}

void test_color_macroblock_raster_order()
{
    const auto stream = make_asymmetric_color_stream({-64, -16, 32, 80});
    Device device;
    send_direct(device, command(1U, OutputDepth::TwentyFourBit, false, false,
                                static_cast<std::uint16_t>(stream.size())), stream);
    const auto words = drain(device);
    require_equal(words.size(), 192U, "asymmetric 24-bit raw output size");
    std::array<std::uint8_t, 768> bytes{};
    for (std::size_t index = 0U; index < words.size(); ++index) {
        for (unsigned lane = 0U; lane < 4U; ++lane) {
            bytes[index * 4U + lane] = static_cast<std::uint8_t>(words[index] >> (lane * 8U));
        }
    }
    const auto raw_red = [&bytes](std::size_t block, std::size_t x, std::size_t y) {
        return bytes[(block * 64U + y * 8U + x) * 3U];
    };
    require(raw_red(0U, 0U, 0U) == raw_red(0U, 7U, 7U), "raw Y1 block continuity");
    require(raw_red(0U, 0U, 0U) != raw_red(1U, 0U, 0U),
            "MDEC0 exposes Y1 then Y2 rather than raster scanlines");

    Device via_dma;
    require(via_dma.write_control_word(0x20000000U).complete, "enable output DMA");
    send_direct(via_dma, command(1U, OutputDepth::TwentyFourBit, false, false,
                                static_cast<std::uint16_t>(stream.size())), stream);
    std::vector<std::uint32_t> dma_words(192U);
    require(via_dma.dma_read_words(dma_words).complete, "DMA reads complete color macroblock");
    std::array<std::uint8_t, 768> dma_bytes{};
    for (std::size_t index = 0U; index < dma_words.size(); ++index) {
        for (unsigned lane = 0U; lane < 4U; ++lane) {
            dma_bytes[index * 4U + lane] =
                static_cast<std::uint8_t>(dma_words[index] >> (lane * 8U));
        }
    }
    const auto dma_red = [&dma_bytes](std::size_t x, std::size_t y) {
        return dma_bytes[(y * 16U + x) * 3U];
    };
    require(dma_red(0U, 0U) != dma_red(8U, 0U), "DMA raster crosses Y1/Y2 at x=8");
    require(dma_red(0U, 0U) != dma_red(0U, 8U), "DMA raster crosses Y1/Y3 at y=8");

    Device replay;
    require(replay.write_control_word(0x20000000U).complete, "replay DMA enable");
    send_direct(replay, command(1U, OutputDepth::TwentyFourBit, false, false,
                                static_cast<std::uint16_t>(stream.size())), stream);
    std::array<std::uint32_t, 17> prefix{};
    require(replay.dma_read_words(prefix).complete, "DMA prefix");
    Snapshot snapshot{};
    require(replay.save_snapshot(snapshot), "DMA-reordered snapshot");
    Device restored;
    require(restored.load_snapshot(snapshot), "restore DMA-reordered snapshot");
    std::vector<std::uint32_t> replay_tail(192U - prefix.size());
    std::vector<std::uint32_t> restored_tail(replay_tail.size());
    require(replay.dma_read_words(replay_tail).complete, "original DMA tail");
    require(restored.dma_read_words(restored_tail).complete, "restored DMA tail");
    require_equal(restored_tail, replay_tail, "mid-DMA snapshot preserves reorder epoch");
    require_equal(restored.digest(), replay.digest(), "mid-DMA replay digest");
}

void test_rle_saturation_and_boundaries()
{
    const std::array<std::pair<std::uint8_t, std::int32_t>, 1> ac = {
        std::pair<std::uint8_t, std::int32_t>{0U, 511},
    };
    const auto full_block = make_block(63U, -512, ac);
    const auto words = pack_halfwords(full_block);
    Device saturated;
    send_direct(saturated, command(1U, OutputDepth::EightBit, true, false,
                                   static_cast<std::uint16_t>(words.size())),
                words);
    const auto output = drain(saturated);
    require_equal(output.size(), 16U, "saturated RLE output size");
    require(saturated.fault() == Fault::None, "saturated RLE must be accepted");

    Device truncated;
    const auto truncated_command = truncated.write_command_word(
        command(1U, OutputDepth::EightBit, false, false, 1U));
    require(truncated_command.complete, "truncated command acceptance");
    const std::uint32_t header_only = static_cast<std::uint32_t>(header(0U, 1));
    const auto truncated_write = truncated.write_command_word(header_only);
    require(truncated_write.fault == Fault::TruncatedStream, "truncated stream fault");

    Device terminating_run;
    const auto terminating_words = pack_halfwords({header(0U, 16), run_value(63U, 1)});
    const auto terminating_command = terminating_run.write_command_word(
        command(1U, OutputDepth::EightBit, false, false, 1U));
    require(terminating_command.complete, "terminating-run command acceptance");
    const auto terminating_result = terminating_run.write_command_word(
        terminating_words.front());
    require(terminating_result.fault == Fault::None, "run overflow terminates the block");
    const auto terminating_output = drain(terminating_run);
    require_equal(terminating_output.size(), 16U, "terminating-run output size");
    for (const std::uint32_t word : terminating_output) {
        require_equal(word, 0x84848484U, "out-of-range run value is not stored");
    }

    for (std::uint8_t opcode = 4U; opcode <= 7U; ++opcode) {
        Device no_op;
        const auto no_op_result = no_op.write_command_word(
            command(opcode, OutputDepth::FifteenBit, true, true, 0x1234U));
        require(no_op_result.words == 1U && no_op_result.fault == Fault::None,
                "opcodes 4..7 act as no-op");
        require(!no_op.command_busy(), "invalid-range no-op stays idle");
        require_equal(no_op.remaining_parameter_words_minus_one(), 0x1234U,
                      "invalid-range no-op low status");
        require((no_op.status() & 0x07800000U) == 0x07800000U,
                "invalid-range no-op reflects format bits");
    }
}

void test_parameter_receipt_under_output_backpressure()
{
    const auto one_macroblock = make_color_stream(3, -2, 12, 2U);
    std::vector<std::uint32_t> two_macroblocks = one_macroblock;
    two_macroblocks.insert(two_macroblocks.end(), one_macroblock.begin(), one_macroblock.end());

    Device device;
    require(device.write_control_word(0x60000000U).complete, "enable MDEC DMA requests");
    const auto decode = device.write_command_word(
        command(1U, OutputDepth::TwentyFourBit, false, false,
                static_cast<std::uint16_t>(two_macroblocks.size())));
    require(decode.complete, "backpressure decode command");
    const auto input = device.dma_write_words(two_macroblocks);
    require(input.complete && input.words == two_macroblocks.size(),
            "all declared parameters accepted");
    require(device.command_busy(), "processing remains busy behind output backpressure");
    require((device.status() & 0x40000000U) != 0U, "last input word status");
    require((device.status() & 0x10000000U) == 0U, "input request clears at declared length");
    require_equal(device.remaining_parameter_words_minus_one(), 0xFFFFU,
                  "no unreceived parameters remain");

    const auto direct_overrun = device.write_command_word(0xDEADBEEFU);
    require(direct_overrun.words == 0U && direct_overrun.fault == Fault::CommandBusy,
            "direct overrun rejected while processing");
    const std::array<std::uint32_t, 1> extra = {0xDEADBEEFU};
    const auto dma_overrun = device.dma_write_words(extra);
    require(dma_overrun.words == 0U && dma_overrun.fault == Fault::DmaNotRequested,
            "DMA overrun rejected after final parameter");

    const auto output = drain(device);
    require_equal(output.size(), 384U, "both backpressured macroblocks emitted");
    require(!device.command_busy(), "decode completes after output space is released");
    require_equal(device.input_bytes_available(), 0U, "no parameter bytes leak to next command");
    require((device.status() & 0x40000000U) != 0U, "last-word indication remains observable");

    require(device.write_command_word(command(0U)).complete, "following command accepted");
    require((device.status() & 0x40000000U) == 0U,
            "new command clears last-word indication");
}

void test_dma_widths_and_backpressure()
{
    Device device;
    const auto control = device.write_control_word(0x60000000U);
    require(control.complete, "DMA enable");
    const auto stream = make_mono_stream(1U);
    const auto bytes = std::as_bytes(std::span<const std::uint32_t>(stream));
    const auto bad_byte_dma = device.dma_write_bytes(bytes, DmaWidth::Byte);
    require(bad_byte_dma.fault == Fault::UnsupportedDmaWidth, "byte DMA rejected");
    const auto bad_half_dma = device.dma_write_bytes(bytes, DmaWidth::Halfword);
    require(bad_half_dma.fault == Fault::UnsupportedDmaWidth, "halfword DMA rejected");
    const auto bad_alignment = device.dma_write_bytes(
        std::span<const std::byte>(bytes.data(), bytes.size() - 1U), DmaWidth::Word);
    require(bad_alignment.fault == Fault::DmaAlignment, "unaligned DMA rejected");

    const auto decode = device.write_command_word(command(1U, OutputDepth::EightBit, false,
                                                           false, 1U));
    require(decode.complete, "DMA decode command");
    const auto input = device.dma_write_bytes(bytes, DmaWidth::Word);
    require(input.complete, "word DMA accepted");
    std::array<std::byte, 64> output_bytes{};
    const auto disabled_read = [&]() {
        Device separate;
        separate.write_command_word(command(1U, OutputDepth::EightBit, false, false, 1U));
        separate.write_command_word(stream.front());
        return separate.dma_read_bytes(output_bytes, DmaWidth::Word);
    }();
    require(disabled_read.fault == Fault::DmaNotRequested, "DMA output request gate");

    std::array<std::byte, 64> dma_output{};
    const auto output = device.dma_read_bytes(dma_output, DmaWidth::Word);
    require(output.complete, "word DMA output accepted");
    const auto first = std::to_integer<std::uint8_t>(dma_output[0]);
    require_equal(first, 0x84U, "DMA output byte order");

    Device backpressure;
    backpressure.write_control_word(0x40000000U);
    const auto long_stream = make_mono_stream(80U);
    const auto long_command = backpressure.write_command_word(
        command(1U, OutputDepth::EightBit, false, false,
                static_cast<std::uint16_t>(long_stream.size())));
    require(long_command.complete, "long stream command");
    std::size_t sent = 0U;
    std::size_t accepted_before_full = 0U;
    while (sent < long_stream.size()) {
        const auto result = backpressure.dma_write_words(
            std::span<const std::uint32_t>(long_stream.data() + sent, long_stream.size() - sent));
        sent += result.words;
        if (result.fault == Fault::InputFifoFull) {
            accepted_before_full = sent;
            break;
        }
        require(result.complete || result.words != 0U, "long DMA made no progress");
    }
    if (accepted_before_full == 0U) {
        require(backpressure.input_bytes_available() == Device::kInputCapacityWords * 4U,
                "input FIFO must expose its bounded full state");
        const auto extra = backpressure.dma_write_words(
            std::span<const std::uint32_t>(long_stream.data(), 1U));
        require(extra.fault == Fault::InputFifoFull, "full input FIFO rejects the next word");
    }
    require(backpressure.input_bytes_available() <= Device::kInputCapacityWords * 4U,
            "input FIFO bounded");

    // An odd-halfword macroblock can leave two bytes at the FIFO head when
    // output backpressure stops the next macroblock. DREQ must account for a
    // complete incoming word, not merely test whether the byte FIFO is equal
    // to its absolute capacity.
    std::vector<std::uint16_t> odd_boundary = {
        header(0U, 16), run_value(0U, 1), 0xFE00U,
    };
    for (unsigned block = 1U; block < 6U; ++block) {
        odd_boundary.push_back(header(0U, 16));
        odd_boundary.push_back(0xFE00U);
    }
    odd_boundary.push_back(header(0U, 16));
    const auto odd_boundary_words = pack_halfwords(odd_boundary);
    require_equal(odd_boundary_words.size(), 7U, "odd-boundary fixture words");

    Device partial_word_full;
    require(partial_word_full.write_control_word(0x40000000U).complete,
            "partial-word DMA enable");
    require(partial_word_full
                .write_command_word(command(1U, OutputDepth::TwentyFourBit, false, false, 64U))
                .complete,
            "partial-word decode command");
    require(partial_word_full.dma_write_words(odd_boundary_words).complete,
            "odd-boundary first macroblock");
    require_equal(partial_word_full.input_bytes_available(), 2U,
                  "next header remains behind output backpressure");
    std::array<std::uint32_t, 31> fill_words{};
    require(partial_word_full.dma_write_words(fill_words).complete,
            "fill all remaining complete-word slots");
    require_equal(partial_word_full.input_bytes_available(), 126U,
                  "two-byte head plus 31 words fills word capacity");
    require((partial_word_full.status() & 0x40000000U) != 0U,
            "status reports no complete input-word space");
    require((partial_word_full.status() & 0x10000000U) == 0U,
            "DREQ clears without four free bytes");
    const std::array<std::uint32_t, 1> one_more = {0U};
    const auto partial_overflow = partial_word_full.dma_write_words(one_more);
    require(partial_overflow.words == 0U && partial_overflow.fault == Fault::InputFifoFull,
            "partial-word full FIFO rejects a whole DMA word");
}

void test_snapshot_and_chunking()
{
    const auto stream = make_mono_stream(12U, 0U, 16, true);
    const std::uint32_t decode_command = command(
        1U, OutputDepth::EightBit, false, false, static_cast<std::uint16_t>(stream.size()));

    Device reference;
    send_direct(reference, decode_command, stream);
    const auto reference_output = drain(reference);

    Device chunked;
    send_direct(chunked, decode_command, std::span<const std::uint32_t>(stream.data(), 2U));
    Snapshot snapshot{};
    require(chunked.save_snapshot(snapshot), "snapshot save");
    require(snapshot.storage.back() == std::byte{0}, "snapshot unused storage is canonical zero");
    Device restored;
    require(restored.load_snapshot(snapshot), "snapshot load");
    require_equal(restored.digest(), chunked.digest(), "snapshot digest");
    for (std::size_t index = 2U; index < stream.size(); ++index) {
        const auto result = chunked.write_command_word(stream[index]);
        require(result.words == 1U && result.fault == Fault::None, "chunked input");
        const auto restored_result = restored.write_command_word(stream[index]);
        require(restored_result.words == 1U && restored_result.fault == Fault::None,
                "restored input");
    }
    const auto restored_output = drain(restored);
    const auto chunked_output = drain(chunked);
    require_equal(restored_output, chunked_output, "snapshot continuation output");
    require_equal(chunked_output, reference_output, "chunking output invariance");

    Device reset_after;
    reset_after.write_command_word(decode_command);
    reset_after.write_command_word(stream.front());
    require(reset_after.command_busy(), "partial command before reset");
    reset_after.write_control_word(kStatusBit31);
    require_equal(reset_after.status(), kResetStatus, "control reset status");
    require_equal(reset_after.fault(), Fault::None, "control reset clears fault");

    Snapshot invalid = snapshot;
    invalid.storage[0] = std::byte{0};
    require(!restored.load_snapshot(invalid), "invalid snapshot rejected");

    Snapshot trailing_garbage = snapshot;
    trailing_garbage.storage.back() = std::byte{1};
    const std::uint64_t before_rejection = restored.digest();
    require(!restored.load_snapshot(trailing_garbage), "noncanonical trailing data rejected");
    require_equal(restored.digest(), before_rejection, "rejected snapshot is non-mutating");

    Snapshot hostile{};
    hostile.storage.fill(std::byte{0xFF});
    require(!restored.load_snapshot(hostile), "hostile snapshot rejected safely");

    Snapshot roundtrip{};
    require(restored.save_snapshot(roundtrip), "restored snapshot save");
    Device canonical;
    require(canonical.load_snapshot(roundtrip), "canonical snapshot reload");
    Snapshot canonical_again{};
    require(canonical.save_snapshot(canonical_again), "canonical snapshot resave");
    require_equal(canonical_again.storage, roundtrip.storage, "snapshot wire format stability");
}

void test_randomized_chunk_schedules()
{
    const auto stream = make_mono_stream(12U, 1U, 32);
    const std::uint32_t decode_command = command(
        1U, OutputDepth::EightBit, false, false, static_cast<std::uint16_t>(stream.size()));
    Device baseline;
    send_direct(baseline, decode_command, stream);
    const auto expected = drain(baseline);

    std::uint32_t random_state = 0x13579BDFU;
    for (unsigned trial = 0U; trial < 8U; ++trial) {
        Device candidate;
        const auto command_result = candidate.write_command_word(decode_command);
        require(command_result.complete, "random schedule command");
        std::size_t cursor = 0U;
        std::vector<std::uint32_t> output;
        while (cursor < stream.size() || candidate.output_words_available() != 0U) {
            if (cursor < stream.size()) {
                random_state = random_state * 1664525U + 1013904223U;
                const std::size_t chunk = 1U + (random_state % 7U);
                const std::size_t count = std::min(chunk, stream.size() - cursor);
                for (std::size_t index = 0U; index < count; ++index) {
                    const auto result = candidate.write_command_word(stream[cursor + index]);
                    require(result.words == 1U && result.fault == Fault::None,
                            "random schedule input");
                }
                cursor += count;
            }
            random_state = random_state * 1664525U + 1013904223U;
            const std::size_t read_count = 1U + (random_state % 11U);
            for (std::size_t index = 0U;
                 index < read_count && candidate.output_words_available() != 0U;
                 ++index) {
                std::uint32_t word = 0U;
                const auto result = candidate.read_data_word(word);
                require(result.words == 1U && result.fault == Fault::None,
                        "random schedule output");
                output.push_back(word);
            }
        }
        require_equal(output, expected, "randomized chunk schedule invariance");
        require_equal(candidate.fault(), Fault::None, "randomized schedule fault");
    }
}

void test_cycle_determinism()
{
    const auto stream = make_color_stream(3, -2, 12, 2U);
    const std::uint32_t decode_command = command(
        1U, OutputDepth::TwentyFourBit, false, false, static_cast<std::uint16_t>(stream.size()));
    Device first;
    Device second;
    send_direct(first, decode_command, stream);
    send_direct(second, decode_command, stream);
    require_equal(first.cycles(), second.cycles(), "identical cycle accounting");
    require_equal(first.digest(), second.digest(), "identical state digest");
    require_equal(drain(first), drain(second), "identical color output");
    require(first.cycles() != 0U, "cycle accounting must advance");

    const auto mono_stream = make_mono_stream(1U);
    Device direct;
    Device via_dma;
    require(via_dma.write_control_word(0x60000000U).complete, "cycle DMA enable");
    require(direct.write_command_word(command(1U, OutputDepth::EightBit, false, false, 1U))
                .complete,
            "direct cycle command");
    require(via_dma.write_command_word(command(1U, OutputDepth::EightBit, false, false, 1U))
                .complete,
            "DMA cycle command");
    const auto direct_input = direct.write_command_word(mono_stream.front());
    const auto dma_input = via_dma.dma_write_words(mono_stream);
    require_equal(dma_input.cycles, direct_input.cycles,
                  "DMA word is not double-counted as DMA plus MMIO");
    require_equal(drain(direct), drain(via_dma), "direct and DMA decode output");
}

void test_adversarial_bounded_sequences()
{
    std::uint32_t random_state = 0xC001D00DU;
    const auto next_random = [&random_state]() noexcept {
        random_state = random_state * 1664525U + 1013904223U;
        return random_state;
    };

    for (unsigned trial = 0U; trial < 128U; ++trial) {
        Device device;
        for (unsigned step = 0U; step < 128U; ++step) {
            const std::uint32_t selector = next_random();
            switch (selector % 6U) {
            case 0U:
                (void)device.write_command_word(next_random());
                break;
            case 1U: {
                const std::uint32_t control = next_random() & 0xE0000000U;
                (void)device.write_control_word(control);
                break;
            }
            case 2U: {
                std::uint32_t word = 0U;
                (void)device.read_data_word(word);
                break;
            }
            case 3U: {
                const std::array<std::uint32_t, 2> words = {next_random(), next_random()};
                (void)device.dma_write_words(words);
                break;
            }
            case 4U: {
                std::array<std::uint32_t, 2> words{};
                (void)device.dma_read_words(words);
                break;
            }
            case 5U: {
                Snapshot snapshot{};
                require(device.save_snapshot(snapshot), "adversarial snapshot save");
                const std::size_t index = next_random() % snapshot.storage.size();
                const std::uint8_t mask = static_cast<std::uint8_t>((next_random() | 1U) & 0xFFU);
                snapshot.storage[index] ^= static_cast<std::byte>(mask);
                Device candidate;
                if (candidate.load_snapshot(snapshot)) {
                    Snapshot canonical{};
                    require(candidate.save_snapshot(canonical),
                            "accepted adversarial snapshot remains serializable");
                    (void)candidate.status();
                    (void)candidate.digest();
                }
                break;
            }
            default:
                throw TestFailure("unreachable adversarial selector");
            }
            require(device.input_bytes_available() <= Device::kInputCapacityWords * 4U,
                    "adversarial input bound");
            require(device.output_words_available() <= Device::kOutputCapacityWords,
                    "adversarial output bound");
            if (device.fault() != Fault::None) {
                const auto reset = device.write_control_word(kStatusBit31);
                require(reset.complete && reset.fault == Fault::None,
                        "adversarial fault recovery reset");
            }
        }
    }
}

} // namespace

int main()
{
    const std::array<std::pair<const char*, void (*)()>, 14> tests = {
        std::pair{"reset_and_status", &test_reset_and_status},
        std::pair{"quant_and_scale_loads", &test_quant_and_scale_loads},
        std::pair{"monochrome_output_and_packing", &test_monochrome_output_and_packing},
        std::pair{"reference_rle_idct_vectors", &test_reference_rle_idct_vectors},
        std::pair{"monochrome_uses_luminance_quantization",
                  &test_monochrome_uses_luminance_quantization},
        std::pair{"color_conversion_and_flags", &test_color_conversion_and_flags},
        std::pair{"color_macroblock_raster_order", &test_color_macroblock_raster_order},
        std::pair{"rle_saturation_and_boundaries", &test_rle_saturation_and_boundaries},
        std::pair{"parameter_receipt_under_output_backpressure",
                  &test_parameter_receipt_under_output_backpressure},
        std::pair{"dma_widths_and_backpressure", &test_dma_widths_and_backpressure},
        std::pair{"snapshot_and_chunking", &test_snapshot_and_chunking},
        std::pair{"randomized_chunk_schedules", &test_randomized_chunk_schedules},
        std::pair{"cycle_determinism", &test_cycle_determinism},
        std::pair{"adversarial_bounded_sequences", &test_adversarial_bounded_sequences},
    };

    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "PASS all " << tests.size() << " MDEC tests\n";
    return 0;
}
