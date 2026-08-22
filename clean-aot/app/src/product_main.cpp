#include "clean_aot/host/sdl_vulkan.hpp"
#include "clean_aot/runtime/runtime.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef CLEAN_AOT_PRODUCT_BINDING_SYMBOL
extern "C" const clean_aot::runtime::GeneratedExecutableBinding*
CLEAN_AOT_PRODUCT_BINDING_SYMBOL() noexcept;
#endif

namespace {
struct Receipt final {
    std::string bios_identity_sha256;
    std::string bios_sha256;
    std::string reason = "product-setup-unavailable";
    std::string stage = "rejected";
    std::uint32_t stop_pc = 0U;
    std::uint64_t dispatches = 0U;
    std::uint64_t instructions = 0U;
    void print() const {
        std::cout << "{\"bios_generated_dispatches\":" << dispatches
                  << ",\"bios_identity_sha256\":\"" << bios_identity_sha256
                  << "\",\"bios_instructions_executed\":" << instructions
                  << ",\"bios_sha256\":\"" << bios_sha256
                  << "\",\"catalog_sha256\":\"\",\"disc_sectors_read\":0,\"entry_pc\":0"
                  << ",\"executable_sha256\":\"\",\"generated_dispatches\":0"
                  << ",\"guest_instructions_executed\":0,\"reason\":\"" << reason
                  << "\",\"schema\":\"clean-aot.bootstrap-receipt.v1\",\"stage\":\""
                  << stage << "\",\"stop_pc\":" << stop_pc
                  << ",\"variant_identity_sha256\":\"\"}\n";
    }
};
std::vector<std::uint8_t> read_bios(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path) ||
        std::filesystem::file_size(path) != 512U * 1024U) return {};
    std::vector<std::uint8_t> bytes(512U * 1024U);
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return input ? bytes : std::vector<std::uint8_t>{};
}
}

int main(int argc, char** argv) {
    std::filesystem::path user_data;
    std::filesystem::path bios_path;
    bool hidden = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if ((argument == "--user-data" || argument == "--bios") && index + 1 < argc) {
            const auto value = std::filesystem::path(argv[++index]);
            if (argument == "--user-data") user_data = value; else bios_path = value;
        } else if (argument == "--smoke-hidden") hidden = true;
        else { std::cerr << "unsupported product argument\n"; return 64; }
    }
    Receipt receipt{};
    if (bios_path.empty() && !user_data.empty()) bios_path = user_data / "runtime" / "bios.bin";
    const auto bios = read_bios(bios_path);
    if (bios.empty()) { receipt.print(); return 78; }
#ifdef CLEAN_AOT_PRODUCT_BINDING_SYMBOL
    const auto* binding = CLEAN_AOT_PRODUCT_BINDING_SYMBOL();
#else
    const clean_aot::runtime::GeneratedExecutableBinding* binding = nullptr;
#endif
    if (binding == nullptr) {
        receipt.reason = "missing-authenticated-generated-bios-binding";
        receipt.print(); return 78;
    }
    receipt.bios_identity_sha256 = std::string(binding->variant_identity_sha256);
    receipt.bios_sha256 = std::string(binding->executable_sha256);
    clean_aot::host::SdlVulkanConfig host_config{};
    host_config.title = "Clean AOT Product";
    host_config.hidden = hidden;
    clean_aot::host::SdlVulkanPresentation host(host_config);
    if (!host.initialize().success()) {
        receipt.reason = "sdl3-vulkan-initialization-failed";
        receipt.print(); return 70;
    }
    clean_aot::runtime::Configuration configuration{};
    configuration.bios = bios;
    configuration.initial_vblank_rate = clean_aot::machine::VBlankRate::Hz100;
    clean_aot::runtime::Runtime runtime(configuration);
    const auto bound = runtime.bind_authenticated_bios(binding->variant_identity_sha256, *binding);
    if (bound != clean_aot::runtime::DispatchStatus::Bound) {
        receipt.reason = "authenticated-bios-bind-rejected";
        host.shutdown(); receipt.print(); return 65;
    }
    receipt.stage = "bios-bound";
    constexpr std::uint64_t dispatch_limit = 1'000'000U;
    while (receipt.dispatches < dispatch_limit) {
        const auto status = runtime.dispatch_bios();
        if (status != clean_aot::runtime::DispatchStatus::Executed) {
            receipt.stage = "bios-slice-stopped";
            receipt.reason = status == clean_aot::runtime::DispatchStatus::UnknownCode
                ? "unknown-generated-bios-pc" : "generated-bios-dispatch-rejected";
            break;
        }
        ++receipt.dispatches;
        receipt.instructions += runtime.last_bios_instruction_count();
    }
    receipt.stop_pc = runtime.bios_pc();
    if (receipt.dispatches == dispatch_limit)
        receipt.reason = "bios-dispatch-budget-exhausted";
    host.shutdown();
    receipt.print();
    return receipt.stage == "bios-slice-stopped" ? 0 : 69;
}
