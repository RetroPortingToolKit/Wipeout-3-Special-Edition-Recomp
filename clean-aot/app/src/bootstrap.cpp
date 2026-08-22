#include "clean_aot/app/bootstrap.hpp"

#include "sha256.hpp"

#include <span>
#include <sstream>

namespace clean_aot::app {
namespace {

const char* stage_name(BootstrapStage stage) noexcept {
    switch (stage) {
    case BootstrapStage::Rejected: return "rejected";
    case BootstrapStage::MediaMounted: return "media-mounted";
    case BootstrapStage::RuntimeInitialized: return "runtime-initialized";
    case BootstrapStage::BiosBound: return "bios-bound";
    case BootstrapStage::BiosSliceStopped: return "bios-slice-stopped";
    case BootstrapStage::ExecutableLoaded: return "executable-loaded";
    case BootstrapStage::GeneratedSliceStopped: return "generated-slice-stopped";
    }
    return "rejected";
}

void json_string(std::ostringstream& output, std::string_view value) {
    output << '"';
    for (const char character : value) {
        if (character == '"' || character == '\\') output << '\\';
        output << character;
    }
    output << '"';
}

} // namespace

std::string BootstrapReceipt::canonical_json() const {
    std::ostringstream output;
    output << "{\"bios_generated_dispatches\":" << bios_generated_dispatches
           << ",\"bios_identity_sha256\":"; json_string(output, bios_identity_sha256);
    output << ",\"bios_instructions_executed\":" << bios_instructions_executed
           << ",\"bios_sha256\":"; json_string(output, bios_sha256);
    output << ",\"catalog_sha256\":"; json_string(output, catalog_sha256);
    output << ",\"disc_sectors_read\":" << disc_sectors_read
           << ",\"entry_pc\":" << entry_pc
           << ",\"executable_sha256\":"; json_string(output, executable_sha256);
    output << ",\"generated_dispatches\":" << generated_dispatches
           << ",\"guest_instructions_executed\":" << guest_instructions_executed
           << ",\"reason\":"; json_string(output, reason);
    output << ",\"schema\":\"clean-aot.bootstrap-receipt.v1\",\"stage\":";
    json_string(output, stage_name(stage));
    output << ",\"stop_pc\":" << stop_pc << ",\"variant_identity_sha256\":";
    json_string(output, variant_identity_sha256);
    output << '}';
    return output.str();
}

BootstrapReceipt BootstrapSession::start_bios_to_game(
    Authorization authorization,
    const BiosBootstrapPlan& plan,
    std::uint64_t maximum_bios_dispatches,
    std::uint64_t maximum_game_dispatches) noexcept {
    BootstrapReceipt receipt{};
    receipt.bios_identity_sha256 = plan.bios_identity_sha256;
    if (!authorization.authorized()) {
        receipt.reason = "authorization-rejected:" + authorization.detail;
        return receipt;
    }
    receipt.catalog_sha256 = authorization.catalog_sha256;
    receipt.variant_identity_sha256 = authorization.variant_identity_sha256;
    receipt.executable_sha256 = authorization.executable_sha256;
    receipt.entry_pc = authorization.selected_entry_pc;
    try {
        receipt.bios_sha256 = detail::sha256(authorization.bios);
        std::string diagnostic;
        disc_ = DiscSource::open(authorization.cue_path, diagnostic);
        if (disc_ == nullptr) { receipt.reason = diagnostic; return receipt; }
        receipt.stage = BootstrapStage::MediaMounted;
        runtime::Configuration configuration{};
        configuration.bios = authorization.bios;
        configuration.initial_vblank_rate = machine::VBlankRate::Hz100;
        configuration.disc_source = disc_.get();
        configuration.disc_layout = &disc_->layout();
        runtime_ = std::make_unique<runtime::Runtime>(configuration);
        if (!runtime_->configured()) {
            receipt.reason = "clean-runtime-configuration-rejected";
            return receipt;
        }
        receipt.stage = BootstrapStage::RuntimeInitialized;
        if (plan.bios_binding == nullptr) {
            receipt.reason = "missing-authenticated-generated-bios-binding";
            return receipt;
        }
        const auto bios_bound = runtime_->bind_authenticated_bios(
            plan.bios_identity_sha256, *plan.bios_binding);
        if (bios_bound != runtime::DispatchStatus::Bound) {
            receipt.reason = "authenticated-bios-bind-rejected:" +
                std::to_string(static_cast<unsigned>(bios_bound));
            return receipt;
        }
        receipt.stage = BootstrapStage::BiosBound;
        receipt.stop_pc = runtime_->bios_pc();
        while (receipt.bios_generated_dispatches < maximum_bios_dispatches &&
               runtime_->bios_pc() != plan.authenticated_game_transition_pc) {
            const auto status = runtime_->dispatch_bios();
            if (status != runtime::DispatchStatus::Executed) {
                receipt.stage = BootstrapStage::BiosSliceStopped;
                receipt.stop_pc = runtime_->bios_pc();
                receipt.reason = status == runtime::DispatchStatus::UnknownCode
                    ? "unknown-generated-bios-pc"
                    : "generated-bios-dispatch-rejected:" +
                        std::to_string(static_cast<unsigned>(status));
                receipt.disc_sectors_read = disc_->sectors_read();
                return receipt;
            }
            ++receipt.bios_generated_dispatches;
            receipt.bios_instructions_executed +=
                runtime_->last_bios_instruction_count();
            receipt.stop_pc = runtime_->bios_pc();
        }
        if (runtime_->bios_pc() != plan.authenticated_game_transition_pc) {
            receipt.stage = BootstrapStage::BiosSliceStopped;
            receipt.reason = "bios-dispatch-budget-exhausted";
            return receipt;
        }
        if (plan.game_binding == nullptr) {
            receipt.stage = BootstrapStage::BiosSliceStopped;
            receipt.reason = "missing-authenticated-generated-game-binding";
            return receipt;
        }
        const auto loaded = runtime_->load_authenticated_executable(
            authorization.variant_identity_sha256, authorization.executable,
            authorization.selected_entry_pc, *plan.game_binding);
        if (loaded != runtime::DispatchStatus::Bound) {
            receipt.reason = "bios-to-game-transition-rejected:" +
                std::to_string(static_cast<unsigned>(loaded));
            return receipt;
        }
        receipt.stage = BootstrapStage::ExecutableLoaded;
        receipt.stop_pc = runtime_->selected_pc();
        while (receipt.generated_dispatches < maximum_game_dispatches) {
            const auto status = runtime_->dispatch_selected(false);
            if (status != runtime::DispatchStatus::Executed) {
                receipt.stage = BootstrapStage::GeneratedSliceStopped;
                receipt.stop_pc = runtime_->selected_pc();
                receipt.reason = status == runtime::DispatchStatus::UnknownCode
                    ? "unknown-generated-game-pc"
                    : "generated-game-dispatch-rejected:" +
                        std::to_string(static_cast<unsigned>(status));
                break;
            }
            ++receipt.generated_dispatches;
            receipt.guest_instructions_executed +=
                runtime_->last_selected_instruction_count();
            receipt.stop_pc = runtime_->selected_pc();
        }
        if (receipt.generated_dispatches == maximum_game_dispatches) {
            receipt.stage = BootstrapStage::GeneratedSliceStopped;
            receipt.reason = "game-dispatch-budget-exhausted";
        }
        receipt.disc_sectors_read = disc_->sectors_read();
        authorization_ = std::move(authorization);
        return receipt;
    } catch (...) {
        receipt.reason = "bootstrap-exception";
        receipt.disc_sectors_read = disc_ == nullptr ? 0U : disc_->sectors_read();
        return receipt;
    }
}

BootstrapReceipt BootstrapSession::start(
    Authorization authorization,
    const runtime::GeneratedExecutableBinding* binding,
    std::uint64_t maximum_dispatches) noexcept {
    BootstrapReceipt receipt{};
    if (!authorization.authorized()) {
        receipt.reason = "authorization-rejected:" + authorization.detail;
        return receipt;
    }
    receipt.catalog_sha256 = authorization.catalog_sha256;
    receipt.variant_identity_sha256 = authorization.variant_identity_sha256;
    receipt.executable_sha256 = authorization.executable_sha256;
    receipt.entry_pc = authorization.selected_entry_pc;
    receipt.stop_pc = authorization.selected_entry_pc;
    try {
        receipt.bios_sha256 = detail::sha256(authorization.bios);
        std::string media_diagnostic;
        disc_ = DiscSource::open(authorization.cue_path, media_diagnostic);
        if (disc_ == nullptr) {
            receipt.reason = media_diagnostic;
            return receipt;
        }
        receipt.stage = BootstrapStage::MediaMounted;

        runtime::Configuration configuration{};
        configuration.bios = authorization.bios;
        configuration.initial_vblank_rate = machine::VBlankRate::Hz100;
        configuration.cpu_multiplier_numerator = 1U;
        configuration.cpu_multiplier_denominator = 1U;
        configuration.disc_source = disc_.get();
        configuration.disc_layout = &disc_->layout();
        runtime_ = std::make_unique<runtime::Runtime>(configuration);
        if (!runtime_->configured()) {
            receipt.reason = "clean-runtime-configuration-rejected";
            return receipt;
        }
        receipt.stage = BootstrapStage::RuntimeInitialized;
        if (binding == nullptr) {
            receipt.reason = "missing-authenticated-generated-binding";
            return receipt;
        }
        const runtime::DispatchStatus loaded = runtime_->load_authenticated_executable(
            authorization.variant_identity_sha256,
            authorization.executable,
            authorization.selected_entry_pc,
            *binding);
        if (loaded != runtime::DispatchStatus::Bound) {
            receipt.reason = "authenticated-executable-load-rejected:" +
                std::to_string(static_cast<unsigned>(loaded));
            return receipt;
        }
        receipt.stage = BootstrapStage::ExecutableLoaded;
        receipt.stop_pc = runtime_->selected_pc();
        while (receipt.generated_dispatches < maximum_dispatches) {
            const runtime::DispatchStatus dispatched =
                runtime_->dispatch_selected(false);
            if (dispatched != runtime::DispatchStatus::Executed) {
                receipt.stage = BootstrapStage::GeneratedSliceStopped;
                receipt.stop_pc = runtime_->selected_pc();
                receipt.reason = dispatched == runtime::DispatchStatus::UnknownCode
                    ? "unknown-generated-pc"
                    : "generated-dispatch-rejected:" +
                        std::to_string(static_cast<unsigned>(dispatched));
                break;
            }
            ++receipt.generated_dispatches;
            receipt.guest_instructions_executed +=
                runtime_->last_selected_instruction_count();
            receipt.stop_pc = runtime_->selected_pc();
        }
        if (receipt.generated_dispatches == maximum_dispatches) {
            receipt.stage = BootstrapStage::GeneratedSliceStopped;
            receipt.reason = "dispatch-budget-exhausted";
        }
        receipt.disc_sectors_read = disc_->sectors_read();
        authorization_ = std::move(authorization);
        return receipt;
    } catch (...) {
        receipt.reason = "bootstrap-exception";
        receipt.disc_sectors_read = disc_ == nullptr ? 0U : disc_->sectors_read();
        return receipt;
    }
}

} // namespace clean_aot::app
