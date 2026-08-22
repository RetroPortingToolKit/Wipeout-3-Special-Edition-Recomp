#ifndef CLEAN_AOT_SPU_SPU_HPP
#define CLEAN_AOT_SPU_SPU_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace clean_aot::spu {

static constexpr std::size_t VoiceCount = 24u;
static constexpr std::size_t SoundRamBytes = 512u * 1024u;
static constexpr std::size_t AdpcmBlockBytes = 16u;
static constexpr std::size_t AdpcmSamplesPerBlock = 28u;
static constexpr std::size_t CaptureSamplesPerHalf = 512u;
/* One legal mono, 18.9 kHz XA sector resamples to 9,408 native frames. */
static constexpr std::size_t AudioFifoCapacity = 16384u;
static constexpr std::size_t TransferFifoCapacity = 32u;
static constexpr std::size_t MaxDmaHalfwordsPerCall = 4096u;
static constexpr std::size_t MaxRenderSamplesPerCall = 8192u;
static constexpr std::uint32_t OutputSampleRateHz = 44'100u;
static constexpr std::uint32_t RemasterUpdateRateHz = 100u;
static constexpr std::size_t SamplesPer25HzUpdate = 1'764u;
static constexpr std::size_t SamplesPer50HzUpdate = 882u;
static constexpr std::size_t SamplesPer100HzUpdate = 441u;
static constexpr std::size_t ReverbFirTapCount = 39u;
static constexpr std::size_t ReverbFirDelaySamples = 19u;
static constexpr std::size_t ReverbCombinedDelaySamples = 38u;

static_assert(OutputSampleRateHz % RemasterUpdateRateHz == 0u);
static_assert(OutputSampleRateHz / RemasterUpdateRateHz ==
              SamplesPer100HzUpdate);
static_assert(OutputSampleRateHz / 50u == SamplesPer50HzUpdate);
static_assert(OutputSampleRateHz / 25u == SamplesPer25HzUpdate);

struct StereoSample final {
    std::int16_t left;
    std::int16_t right;

    friend constexpr bool operator==(const StereoSample&, const StereoSample&) = default;
};

static_assert(sizeof(StereoSample) == sizeof(std::int16_t) * 2u,
              "stereo sample must contain two packed 16-bit values");

enum class MmioWidth : std::uint8_t {
    Bits8 = 1u,
    Bits16 = 2u,
    Bits32 = 4u,
};

enum class MmioFault : std::uint8_t {
    None,
    WidthNot16,
    Unaligned,
    Unmapped,
    ReadOnly,
    WriteOnly,
    ValueOutOfRange,
    TransferFifoUnderflow,
    TransferFifoOverflow,
};

struct MmioReadResult final {
    MmioFault fault{MmioFault::None};
    std::uint16_t value{0u};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return fault == MmioFault::None;
    }
};

struct MmioWriteResult final {
    MmioFault fault{MmioFault::None};
    std::uint16_t accepted_value{0u};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return fault == MmioFault::None;
    }
};

enum class AudioSource : std::uint8_t {
    Cdda,
    XaResampled,
};

struct PushResult final {
    std::size_t accepted{0u};
    std::size_t dropped{0u};
    bool overflow{false};
};

struct InputStatus final {
    std::size_t queued{0u};
    std::uint64_t underflow_count{0u};
    std::uint64_t overflow_count{0u};
    bool underflow{false};
    bool overflow{false};
};

enum class DmaFault : std::uint8_t {
    None,
    WrongTransferMode,
    RequestNotReady,
    TooManyHalfwords,
    CallbackRejected,
};

struct DmaResult final {
    DmaFault fault{DmaFault::None};
    std::size_t transferred_halfwords{0u};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return fault == DmaFault::None;
    }
};

enum class RenderFault : std::uint8_t {
    None,
    TooManySamples,
};

struct RenderResult final {
    RenderFault fault{RenderFault::None};
    std::size_t rendered_samples{0u};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return fault == RenderFault::None;
    }
};

enum class SnapshotFault : std::uint8_t {
    None,
    BufferTooSmall,
    InvalidHeader,
    InvalidPayload,
    DigestMismatch,
};

struct SnapshotResult final {
    SnapshotFault fault{SnapshotFault::None};
    std::size_t bytes{0u};
    std::uint64_t digest{0u};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return fault == SnapshotFault::None;
    }
};

enum class AdsrPhase : std::uint8_t {
    Off,
    Attack,
    Decay,
    Sustain,
    Release,
};

struct VoiceView final {
    bool valid{false};
    bool active{false};
    bool block_valid{false};
    AdsrPhase phase{AdsrPhase::Off};
    std::uint16_t volume_left_register{0u};
    std::uint16_t volume_right_register{0u};
    std::uint16_t pitch{0u};
    std::uint16_t start_address_units{0u};
    std::uint16_t repeat_address_units{0u};
    std::uint32_t current_address_bytes{0u};
    std::uint32_t pitch_counter{0u};
    std::int16_t adsr_volume{0};
    std::uint32_t adsr_register_low{0u};
    std::uint32_t adsr_register_high{0u};
    std::uint8_t block_flags{0u};
    std::uint8_t block_sample_index{0u};
};

using IrqCallback = void (*)(void* user) noexcept;
using DmaReadCallback = bool (*)(std::span<const std::uint16_t> words,
                                 void* user) noexcept;

class Device final {
public:
    /* All offsets are relative to the SPU register window. */
    static constexpr std::uint32_t VoiceWindowBytes = 0x0180u;
    static constexpr std::uint32_t MainVolume = 0x0180u;
    static constexpr std::uint32_t ReverbOutputVolume = 0x0184u;
    static constexpr std::uint32_t KeyOn = 0x0188u;
    static constexpr std::uint32_t KeyOff = 0x018Cu;
    static constexpr std::uint32_t PitchModulation = 0x0190u;
    static constexpr std::uint32_t NoiseEnable = 0x0194u;
    static constexpr std::uint32_t ReverbEnable = 0x0198u;
    static constexpr std::uint32_t EndFlags = 0x019Cu;
    static constexpr std::uint32_t UnknownStatus = 0x01A0u;
    static constexpr std::uint32_t ReverbBase = 0x01A2u;
    static constexpr std::uint32_t IrqAddress = 0x01A4u;
    static constexpr std::uint32_t TransferAddress = 0x01A6u;
    static constexpr std::uint32_t TransferFifo = 0x01A8u;
    static constexpr std::uint32_t Control = 0x01AAu;
    static constexpr std::uint32_t TransferControl = 0x01ACu;
    static constexpr std::uint32_t Status = 0x01AEu;
    static constexpr std::uint32_t CdVolume = 0x01B0u;
    static constexpr std::uint32_t ExternalVolume = 0x01B4u;
    static constexpr std::uint32_t CurrentMainVolume = 0x01B8u;
    static constexpr std::uint32_t UnknownControlBase = 0x01BCu;
    static constexpr std::uint32_t UnknownControlBytes = 0x0004u;
    static constexpr std::uint32_t ReverbRegisterBase = 0x01C0u;
    static constexpr std::uint32_t ReverbRegisterBytes = 0x0040u;
    static constexpr std::uint32_t InternalVoiceVolumeBase = 0x0200u;
    static constexpr std::uint32_t InternalVoiceVolumeBytes = 0x0060u;
    static constexpr std::uint32_t UnknownInternalBase = 0x0260u;
    static constexpr std::uint32_t UnknownInternalBytes = 0x0020u;

    static constexpr std::uint16_t ControlCdEnable = 0x0001u;
    static constexpr std::uint16_t ControlExternalEnable = 0x0002u;
    static constexpr std::uint16_t ControlCdReverb = 0x0004u;
    static constexpr std::uint16_t ControlExternalReverb = 0x0008u;
    static constexpr std::uint16_t ControlTransferModeMask = 0x0030u;
    static constexpr std::uint16_t ControlIrqEnable = 0x0040u;
    static constexpr std::uint16_t ControlReverbEnable = 0x0080u;
    static constexpr std::uint16_t ControlNoiseStepMask = 0x0300u;
    static constexpr std::uint16_t ControlNoiseShiftMask = 0x3C00u;
    static constexpr std::uint16_t ControlMute = 0x4000u;
    static constexpr std::uint16_t ControlEnable = 0x8000u;

    static constexpr std::uint16_t StatusCaptureSecondHalf = 0x0800u;
    static constexpr std::uint16_t StatusTransferBusy = 0x0400u;
    static constexpr std::uint16_t StatusDmaReadRequest = 0x0200u;
    static constexpr std::uint16_t StatusDmaWriteRequest = 0x0100u;
    static constexpr std::uint16_t StatusTransferRequest = 0x0080u;
    static constexpr std::uint16_t StatusIrq = 0x0040u;

    static constexpr std::uint32_t SnapshotHeaderBytes = 24u;
    static constexpr std::uint32_t SnapshotDigestBytes = 8u;
    static constexpr std::uint32_t SnapshotVersion = 3u;
    static constexpr std::size_t VolumeSnapshotBytes = 12u;
    static constexpr std::size_t VoiceSnapshotBytes = 127u;
    static constexpr std::size_t AudioFifoSnapshotBytes =
        6u + 2u + 16u + (AudioFifoCapacity * sizeof(StereoSample));
    static constexpr std::size_t SnapshotPayloadBytes =
        SoundRamBytes + (VoiceCount * VoiceSnapshotBytes) +
        (2u * VolumeSnapshotBytes) + 4u + 4u + 4u + 25u + 4u + 4u + 8u +
        16u + 3u + (TransferFifoCapacity * sizeof(std::uint16_t)) + 2u +
        6u + 1u + 1u + 4u + UnknownControlBytes + UnknownInternalBytes +
        (4u * ReverbFirTapCount * sizeof(std::int16_t)) + 1u +
        (3u * AudioFifoSnapshotBytes) + (32u * sizeof(std::uint16_t));
    static constexpr std::size_t SnapshotBytes =
        SnapshotHeaderBytes + SnapshotPayloadBytes + SnapshotDigestBytes;

    Device() noexcept;

    void reset() noexcept;

    [[nodiscard]] MmioReadResult read_mmio(std::uint32_t offset,
                                           MmioWidth width) noexcept;
    [[nodiscard]] MmioWriteResult write_mmio(std::uint32_t offset,
                                             std::uint32_t value,
                                             MmioWidth width) noexcept;

    [[nodiscard]] VoiceView voice_state(std::size_t voice) const noexcept;

    /*
     * Callbacks are synchronous, non-owning, and intentionally absent from
     * snapshots/digests. The caller must keep `user` alive and must not
     * re-enter this Device from either callback.
     */
    void set_irq_callback(IrqCallback callback, void* user) noexcept;
    [[nodiscard]] bool irq_pending() const noexcept;
    void acknowledge_irq() noexcept;
    [[nodiscard]] bool dma_request() const noexcept;

    void set_audio_source(AudioSource source) noexcept;
    [[nodiscard]] AudioSource audio_source() const noexcept;
    [[nodiscard]] PushResult push_cdda(
        std::span<const StereoSample> samples) noexcept;
    [[nodiscard]] PushResult push_xa_resampled(
        std::span<const StereoSample> samples) noexcept;
    [[nodiscard]] PushResult push_external(
        std::span<const StereoSample> samples) noexcept;
    void clear_audio_input(AudioSource source) noexcept;
    [[nodiscard]] InputStatus input_status(AudioSource source) const noexcept;
    [[nodiscard]] InputStatus external_input_status() const noexcept;
    void clear_input_status() noexcept;

    [[nodiscard]] DmaResult dma_write(
        std::span<const std::uint16_t> words) noexcept;
    [[nodiscard]] DmaResult dma_read(std::span<std::uint16_t> words) noexcept;
    [[nodiscard]] DmaResult dma_read(DmaReadCallback callback,
                                     void* user,
                                     std::size_t halfwords) noexcept;

    [[nodiscard]] RenderResult render(
        std::span<StereoSample> output) noexcept;
    [[nodiscard]] RenderResult render_100hz_update(
        std::span<StereoSample, SamplesPer100HzUpdate> output) noexcept;

    [[nodiscard]] std::span<const std::uint8_t> sound_ram() const noexcept;

    [[nodiscard]] static constexpr std::size_t snapshot_size() noexcept {
        return SnapshotBytes;
    }
    [[nodiscard]] SnapshotResult save_snapshot(
        std::span<std::byte> destination) const noexcept;
    [[nodiscard]] SnapshotResult restore_snapshot(
        std::span<const std::byte> source) noexcept;
    [[nodiscard]] std::uint64_t deterministic_digest() const noexcept;

private:
    enum class TransferMode : std::uint8_t {
        Stop = 0u,
        ManualWrite = 1u,
        DmaWrite = 2u,
        DmaRead = 3u,
    };

    struct VolumeControl final {
        std::uint16_t raw{0u};
        std::int16_t current{0};
        std::uint16_t counter{0u};
        std::int16_t pending_current{0};
        std::uint16_t pending_counter{0u};
        bool pending_fixed{false};
    };

    struct Voice final {
        VolumeControl left_volume{};
        VolumeControl right_volume{};
        std::uint16_t pitch{0u};
        std::uint16_t start_address_units{0u};
        std::uint32_t adsr_low{0u};
        std::uint32_t adsr_high{0u};
        std::uint16_t current_adsr_register{0u};
        std::uint16_t repeat_address_units{0u};
        std::uint32_t current_address_bytes{0u};
        std::uint32_t pitch_counter{0u};
        std::uint32_t block_address_bytes{0u};
        std::uint8_t block_flags{0u};
        std::uint8_t block_sample_index{0u};
        AdsrPhase phase{AdsrPhase::Off};
        bool block_valid{false};
        bool active{false};
        std::int16_t decoder_previous{0};
        std::int16_t decoder_previous_previous{0};
        std::array<std::int16_t, 3u> history{};
        std::array<std::int16_t, AdpcmSamplesPerBlock> block_samples{};
        std::int16_t adsr_volume{0};
        std::uint16_t adsr_counter{0u};
        std::uint16_t pending_current_adsr{0u};
        bool pending_current_adsr_valid{false};
    };

    template <std::size_t Capacity>
    struct StereoFifo final {
        std::array<StereoSample, Capacity> samples{};
        std::uint16_t head{0u};
        std::uint16_t tail{0u};
        std::uint16_t count{0u};
        bool underflow{false};
        bool overflow{false};
        std::uint64_t underflow_count{0u};
        std::uint64_t overflow_count{0u};
    };

    struct SnapshotWriter;
    struct SnapshotReader;
    struct DigestSink;

    static constexpr std::uint32_t RamMask =
        static_cast<std::uint32_t>(SoundRamBytes - 1u);
    static constexpr std::uint32_t ReverbEndByte =
        static_cast<std::uint32_t>(SoundRamBytes - 2u);

    std::array<std::uint8_t, SoundRamBytes> sound_ram_{};
    std::array<Voice, VoiceCount> voices_{};
    VolumeControl main_left_{};
    VolumeControl main_right_{};
    std::uint16_t reverb_output_left_{0u};
    std::uint16_t reverb_output_right_{0u};
    std::int16_t cd_volume_left_{0};
    std::int16_t cd_volume_right_{0};
    std::int16_t external_volume_left_{0};
    std::int16_t external_volume_right_{0};
    std::uint16_t control_{0u};
    std::uint16_t effective_control_{0u};
    std::uint16_t pending_control_mode_{0u};
    bool pending_control_mode_valid_{false};
    std::uint32_t pending_key_on_mask_{0u};
    std::uint32_t pending_key_off_mask_{0u};
    std::uint16_t transfer_control_{0u};
    std::uint16_t transfer_address_register_{0u};
    std::uint32_t transfer_current_byte_{0u};
    std::uint16_t irq_address_register_{0u};
    std::uint16_t reverb_base_register_{0u};
    std::uint32_t reverb_cursor_byte_{0u};
    std::array<std::uint16_t, 32u> reverb_registers_{};
    std::array<std::uint16_t, TransferFifoCapacity> transfer_fifo_{};
    std::uint8_t transfer_fifo_head_{0u};
    std::uint8_t transfer_fifo_tail_{0u};
    std::uint8_t transfer_fifo_count_{0u};
    std::uint32_t pitch_modulation_mask_{0u};
    std::uint32_t noise_enable_mask_{0u};
    std::uint32_t reverb_enable_mask_{0u};
    std::uint32_t end_flags_{0u};
    std::uint16_t capture_index_{0u};
    std::int16_t noise_level_{0};
    std::int32_t noise_timer_{0};
    std::uint64_t sample_counter_{0u};
    std::int16_t reverb_hold_left_{0};
    std::int16_t reverb_hold_right_{0};
    std::array<std::int16_t, ReverbFirTapCount> reverb_input_left_{};
    std::array<std::int16_t, ReverbFirTapCount> reverb_input_right_{};
    std::array<std::int16_t, ReverbFirTapCount>
        reverb_output_history_left_{};
    std::array<std::int16_t, ReverbFirTapCount>
        reverb_output_history_right_{};
    std::uint8_t reverb_history_index_{0u};
    bool dma_read_request_ready_{false};
    bool transfer_busy_{false};
    std::array<std::uint16_t, UnknownControlBytes / 2u>
        unknown_control_registers_{};
    std::array<std::uint16_t, UnknownInternalBytes / 2u>
        unknown_internal_registers_{};
    bool irq_pending_{false};
    AudioSource audio_source_{AudioSource::Cdda};
    IrqCallback irq_callback_{nullptr};
    void* irq_user_{nullptr};
    StereoFifo<AudioFifoCapacity> cdda_fifo_{};
    StereoFifo<AudioFifoCapacity> xa_fifo_{};
    StereoFifo<AudioFifoCapacity> external_fifo_{};

    static std::int32_t sign_extend(std::uint32_t value,
                                    std::uint32_t bits) noexcept;
    static std::int16_t to_i16(std::uint16_t bits) noexcept;
    static std::uint16_t bits_of_i16(std::int16_t value) noexcept;
    static std::int32_t arithmetic_shift_right(std::int32_t value,
                                                unsigned shift) noexcept;
    static std::int64_t arithmetic_shift_right(std::int64_t value,
                                                unsigned shift) noexcept;
    static std::int16_t saturate_i16(std::int64_t value) noexcept;
    static std::int32_t saturate_i32(std::int64_t value) noexcept;
    static std::int32_t multiply_q15(std::int32_t left,
                                     std::int32_t right) noexcept;
    static std::int64_t multiply_q15_wide(std::int64_t left,
                                          std::int32_t right) noexcept;
    static std::int16_t decode_fixed_volume(std::uint16_t raw) noexcept;
    static std::uint16_t volume_register_for(const VolumeControl& control)
        noexcept;
    static TransferMode transfer_mode(std::uint16_t control) noexcept;

    static std::size_t capture_offset(std::size_t channel,
                                      std::size_t sample) noexcept;
    static std::uint32_t wrap_ram(std::uint32_t address) noexcept;
    std::uint32_t wrap_reverb(std::int64_t address) const noexcept;
    std::uint32_t reverb_address(std::uint16_t relative_units,
                                 std::int32_t extra_units = 0) const noexcept;

    void reset_voice(Voice& voice) noexcept;
    void write_volume(VolumeControl& volume, std::uint16_t raw) noexcept;
    void advance_volume(VolumeControl& volume) noexcept;
    void apply_pending_events() noexcept;
    void key_on(std::uint32_t mask) noexcept;
    void key_off(std::uint32_t mask) noexcept;
    void apply_control(std::uint16_t value) noexcept;
    void apply_transfer_control(std::uint16_t value) noexcept;
    void service_transfer_fifo() noexcept;
    void advance_transfer_address() noexcept;
    void write_transfer_word(std::uint16_t value) noexcept;
    [[nodiscard]] std::uint16_t read_transfer_word() noexcept;

    void touch_ram_range(std::uint32_t start,
                         std::uint32_t bytes) noexcept;
    void trigger_irq() noexcept;
    void write_ram_word(std::uint32_t address, std::uint16_t value) noexcept;
    [[nodiscard]] std::uint16_t read_ram_word(std::uint32_t address) noexcept;

    void decode_block(Voice& voice, std::uint32_t address) noexcept;
    void touch_voice_ram(const Voice& voice) noexcept;
    void finish_block(Voice& voice) noexcept;
    void ensure_block(Voice& voice) noexcept;
    void advance_block(Voice& voice) noexcept;
    [[nodiscard]] std::int16_t sample_before(const Voice& voice,
                                             std::uint32_t index,
                                             std::uint32_t distance) const
        noexcept;
    [[nodiscard]] std::int16_t interpolate(const Voice& voice) const noexcept;
    [[nodiscard]] std::uint32_t pitch_step(const Voice& voice,
                                            std::int16_t previous_output,
                                            std::size_t index) const noexcept;
    [[nodiscard]] std::int16_t next_noise() noexcept;

    void release_adsr(Voice& voice) noexcept;
    void advance_adsr(Voice& voice) noexcept;
    [[nodiscard]] std::int32_t adsr_step(const Voice& voice,
                                         bool increasing,
                                         bool exponential,
                                         std::int32_t level,
                                         std::uint16_t& counter) const
        noexcept;
    template <std::size_t Capacity>
    static PushResult push_fifo(StereoFifo<Capacity>& fifo,
                                std::span<const StereoSample> samples) noexcept;
    template <std::size_t Capacity>
    static InputStatus fifo_status(const StereoFifo<Capacity>& fifo) noexcept;
    template <std::size_t Capacity>
    static void clear_fifo_status(StereoFifo<Capacity>& fifo) noexcept;
    template <std::size_t Capacity>
    static bool pop_fifo(StereoFifo<Capacity>& fifo,
                         StereoSample& sample) noexcept;

    [[nodiscard]] StereoSample read_cd_sample(bool needed) noexcept;
    [[nodiscard]] StereoSample read_external_sample(bool needed) noexcept;
    void capture_sample(std::size_t channel,
                        std::int16_t sample,
                        bool irq_enabled) noexcept;
    void capture_voice_samples(std::int16_t voice_one,
                               std::int16_t voice_three,
                               bool irq_enabled) noexcept;

    [[nodiscard]] std::int16_t reverb_fir_sample(
        const std::array<std::int16_t, ReverbFirTapCount>& history,
        bool interpolate) const noexcept;
    [[nodiscard]] StereoSample process_reverb_side(
        bool right_channel,
        std::int16_t input,
        bool write_buffer) noexcept;
    [[nodiscard]] std::int16_t reverb_multiply(std::int32_t left,
                                               std::int32_t right) const
        noexcept;
    [[nodiscard]] std::int16_t read_reverb_sample(std::uint32_t address) noexcept;
    void write_reverb_sample(std::uint32_t address,
                             std::int16_t sample) noexcept;

    [[nodiscard]] MmioReadResult read_mmio16(std::uint32_t offset) noexcept;
    [[nodiscard]] MmioWriteResult write_mmio16(std::uint32_t offset,
                                               std::uint16_t value) noexcept;
    [[nodiscard]] bool is_known_even_offset(std::uint32_t offset) const noexcept;

    template <typename Sink>
    void serialize_payload(Sink& sink) const noexcept;
    [[nodiscard]] bool validate_payload(SnapshotReader& reader) const noexcept;
    [[nodiscard]] bool deserialize_payload(SnapshotReader& reader) noexcept;
};

} // namespace clean_aot::spu

#endif // CLEAN_AOT_SPU_SPU_HPP
