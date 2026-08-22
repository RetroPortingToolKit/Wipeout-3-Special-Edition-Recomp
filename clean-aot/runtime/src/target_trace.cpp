#include "clean_aot/runtime/target_trace.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <tuple>
#include <vector>

namespace clean_aot::runtime {
namespace {

bool sha256(std::string_view value) {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool safe_id(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '.' || c == '_' || c == '-';
    });
}

std::string_view status_name(DispatchStatus status) {
    switch (status) {
    case DispatchStatus::Bound: return "bound";
    case DispatchStatus::Executed: return "executed";
    case DispatchStatus::UnknownCode: return "unknown-code";
    case DispatchStatus::IdentityMismatch: return "identity-mismatch";
    case DispatchStatus::InvalidBinding: return "invalid-binding";
    case DispatchStatus::ExecutableHashMismatch: return "executable-hash-mismatch";
    case DispatchStatus::LoadRejected: return "load-rejected";
    case DispatchStatus::CpuFault: return "cpu-fault";
    case DispatchStatus::EpochMismatch: return "epoch-mismatch";
    }
    return "invalid-binding";
}

std::string_view transfer_name(RuntimeTraceTransfer transfer) {
    return transfer == RuntimeTraceTransfer::IndirectCall ? "indirect-call" :
           transfer == RuntimeTraceTransfer::IndirectJump ? "indirect-jump" : "none";
}

} // namespace

RuntimeTargetTrace::RuntimeTargetTrace(std::span<const RuntimeTraceImage> images,
                                       std::size_t event_capacity,
                                       std::uint32_t first_sequence) noexcept
    : event_capacity_(std::min(event_capacity, kMaximumEvents)),
      first_sequence_(first_sequence) {
    if (images.empty() || images.size() > kMaximumImages || event_capacity_ == 0U) return;
    std::string_view previous{};
    for (const auto& image : images) {
        if (!safe_id(image.id) || !sha256(image.sha256) || !sha256(image.variant_sha256) ||
            (image.image_kind != "raw-payload" && image.image_kind != "ps-x-exe") ||
            image.load_start % 4U != 0U || image.load_end_exclusive <= image.load_start ||
            image.load_end_exclusive > (std::uint64_t{1} << 32U) ||
            image.load_end_exclusive % 4U != 0U ||
            (image_count_ != 0U && image.id <= previous)) return;
        images_[image_count_++] = image;
        previous = image.id;
    }
    valid_ = true;
}

const RuntimeTraceImage* RuntimeTargetTrace::image_for(std::string_view identity) const noexcept {
    for (std::size_t index = 0; index < image_count_; ++index) {
        if (images_[index].variant_sha256 == identity) return &images_[index];
    }
    return nullptr;
}

void RuntimeTargetTrace::append(Event event) noexcept {
    if (!valid_ || finalized_) return;
    if (event_count_ >= event_capacity_ ||
        event_count_ > std::numeric_limits<std::uint32_t>::max() - first_sequence_) {
        ++dropped_events_;
        return;
    }
    event.sequence = first_sequence_ + static_cast<std::uint32_t>(event_count_);
    events_[event_count_++] = event;
}

void RuntimeTargetTrace::record_epoch(std::string_view active_identity,
                                      DispatchStatus status) noexcept {
    append(Event{Kind::Epoch, 0U, active_identity, active_identity, active_identity,
                 0U, 0U, RuntimeTraceTransfer::None, status});
}

void RuntimeTargetTrace::record_dispatch(std::string_view active_identity,
                                         std::uint32_t source_pc,
                                         std::uint32_t destination_pc,
                                         DispatchStatus status) noexcept {
    append(Event{Kind::Dispatch, 0U, active_identity, active_identity, active_identity,
                 source_pc, destination_pc, RuntimeTraceTransfer::None, status});
}

void RuntimeTargetTrace::record_target_attempt(
    std::string_view active_identity, std::string_view source_identity,
    std::uint32_t source_pc, std::string_view destination_identity,
    std::uint32_t destination_pc, RuntimeTraceTransfer transfer,
    DispatchStatus status) noexcept {
    append(Event{Kind::TargetAttempt, 0U, active_identity, source_identity,
                 destination_identity, source_pc, destination_pc, transfer, status});
}

std::string RuntimeTargetTrace::canonical_receipt() const {
    if (!valid_ || !finalized_) return {};
    struct Observation { std::string_view destination; std::uint32_t pc; std::uint64_t count; };
    struct Site {
        std::string_view source; std::uint32_t pc; RuntimeTraceTransfer transfer;
        std::vector<Observation> observations;
    };
    std::vector<Site> sites;
    std::uint64_t accepted_count = 0U;
    for (std::size_t index = 0; index < event_count_; ++index) {
        const Event& event = events_[index];
        if (event.kind != Kind::TargetAttempt || event.status != DispatchStatus::Executed) continue;
        const auto* source = image_for(event.source_identity);
        const auto* destination = image_for(event.destination_identity);
        if (source == nullptr || destination == nullptr || event.transfer == RuntimeTraceTransfer::None)
            continue;
        ++accepted_count;
        auto site = std::find_if(sites.begin(), sites.end(), [&](const Site& row) {
            return row.source == source->id && row.pc == event.source_pc &&
                   row.transfer == event.transfer;
        });
        if (site == sites.end()) {
            sites.push_back(Site{source->id, event.source_pc, event.transfer, {}});
            site = std::prev(sites.end());
        }
        auto observation = std::find_if(site->observations.begin(), site->observations.end(),
            [&](const Observation& row) { return row.destination == destination->id &&
                                                 row.pc == event.destination_pc; });
        if (observation == site->observations.end())
            site->observations.push_back({destination->id, event.destination_pc, 1U});
        else ++observation->count;
    }
    std::sort(sites.begin(), sites.end(), [](const Site& left, const Site& right) {
        return std::tie(left.source, left.pc) < std::tie(right.source, right.pc);
    });
    for (auto& site : sites) std::sort(site.observations.begin(), site.observations.end(),
        [](const Observation& left, const Observation& right) {
            return std::tie(left.destination, left.pc) < std::tie(right.destination, right.pc);
        });

    const bool is_complete = complete();
    std::ostringstream out;
    out << "{\"capture\":{\"complete\":" << (is_complete ? "true" : "false")
        << ",\"dropped_events\":" << dropped_events_ << ",\"event_count\":"
        << accepted_count << ",\"first_sequence\":" << first_sequence_
        << ",\"last_sequence\":" << (accepted_count == 0U ? first_sequence_ :
            first_sequence_ + accepted_count - 1U) << "},\"images\":[";
    for (std::size_t index = 0; index < image_count_; ++index) {
        if (index != 0U) out << ',';
        const auto& image = images_[index];
        out << "{\"id\":\"" << image.id << "\",\"image_kind\":\""
            << image.image_kind << "\",\"load_end_exclusive\":"
            << image.load_end_exclusive << ",\"load_start\":" << image.load_start
            << ",\"sha256\":\"" << image.sha256 << "\",\"variant_sha256\":\""
            << image.variant_sha256 << "\"}";
    }
    out << "],\"schema\":\"clean-aot-runtime-target-trace-v1\",\"sites\":[";
    for (std::size_t index = 0; index < sites.size(); ++index) {
        if (index != 0U) out << ',';
        const auto& site = sites[index];
        out << "{\"complete\":" << (is_complete ? "true" : "false")
            << ",\"observations\":[";
        for (std::size_t target = 0; target < site.observations.size(); ++target) {
            if (target != 0U) out << ',';
            const auto& row = site.observations[target];
            out << "{\"count\":" << row.count << ",\"destination_image\":\""
                << row.destination << "\",\"destination_pc\":" << row.pc << '}';
        }
        out << "],\"source_image\":\"" << site.source << "\",\"source_pc\":"
            << site.pc << ",\"transfer\":\"" << transfer_name(site.transfer) << "\"}";
    }
    out << "],\"telemetry\":{\"complete\":" << (is_complete ? "true" : "false")
        << ",\"dropped_events\":" << dropped_events_ << ",\"event_count\":"
        << event_count_ << ",\"events\":[";
    for (std::size_t index = 0; index < event_count_; ++index) {
        if (index != 0U) out << ',';
        const Event& event = events_[index];
        const auto* active = image_for(event.active_identity);
        const auto* source = image_for(event.source_identity);
        const auto* destination = image_for(event.destination_identity);
        out << "{\"accepted\":" << (event.status == DispatchStatus::Executed ? "true" : "false")
            << ",\"active_epoch_identity_sha256\":\"" << event.active_identity
            << "\",\"destination_image\":\"" << (destination ? destination->id : "unknown")
            << "\",\"destination_pc\":" << event.destination_pc << ",\"kind\":\""
            << (event.kind == Kind::Epoch ? "epoch" : event.kind == Kind::Dispatch ? "dispatch" : "target-attempt")
            << "\",\"sequence\":" << event.sequence << ",\"source_image\":\""
            << (source ? source->id : active ? active->id : "unknown")
            << "\",\"source_pc\":" << event.source_pc << ",\"status\":\""
            << status_name(event.status) << "\",\"transfer\":\""
            << transfer_name(event.transfer) << "\"}";
    }
    out << "],\"first_sequence\":" << first_sequence_ << ",\"last_sequence\":"
        << (event_count_ == 0U ? first_sequence_ :
            first_sequence_ + static_cast<std::uint32_t>(event_count_ - 1U)) << "}}\n";
    return out.str();
}

} // namespace clean_aot::runtime
