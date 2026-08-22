#include "clean_aot/app/memory_card.hpp"

#include "sio.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace clean_aot::app {

namespace {

bool persist_image(const std::filesystem::path& path,
                   std::span<const std::uint8_t> image) noexcept {
    try {
        const auto temporary = path.parent_path() / (path.filename().string() + ".new");
        if (std::filesystem::exists(temporary)) std::filesystem::remove(temporary);
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(image.data()),
                         static_cast<std::streamsize>(image.size()));
            output.flush();
            if (!output) return false;
        }
#ifdef _WIN32
        HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        const BOOL synced = FlushFileBuffers(handle);
        CloseHandle(handle);
        if (synced == 0) return false;
        if (MoveFileExW(temporary.c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) return false;
#else
        std::filesystem::rename(temporary, path);
#endif
        return true;
    } catch (...) { return false; }
}

} // namespace

bool MemoryCard::load_or_create(const std::filesystem::path& path) noexcept {
    try {
        if (path.empty() || std::filesystem::is_symlink(path)) return false;
        std::array<std::uint8_t, kMemoryCardBytes> candidate{};
        if (std::filesystem::exists(path)) {
            if (!std::filesystem::is_regular_file(path) ||
                std::filesystem::file_size(path) != kMemoryCardBytes) return false;
            std::ifstream input(path, std::ios::binary);
            input.read(reinterpret_cast<char*>(candidate.data()),
                       static_cast<std::streamsize>(candidate.size()));
            if (!input || input.peek() != std::ifstream::traits_type::eof()) return false;
        } else {
            const auto parent = path.parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
            if (!sio::MemoryCard::format_new_image(candidate) ||
                !persist_image(path, candidate)) return false;
        }

        path_ = path;
        data_ = candidate;
        loaded_ = true;
        dirty_ = false;
        return true;
    } catch (...) { return false; }
}

bool MemoryCard::write(std::size_t offset, std::span<const std::uint8_t> bytes) noexcept {
    if (!loaded_ || offset > data_.size() || bytes.size() > data_.size() - offset) return false;
    std::copy(bytes.begin(), bytes.end(), data_.begin() + static_cast<std::ptrdiff_t>(offset));
    dirty_ = dirty_ || !bytes.empty();
    return true;
}

bool MemoryCard::flush() noexcept {
    if (!loaded_) return false;
    if (!dirty_) return true;
    if (!persist_image(path_, data_)) return false;
    dirty_ = false;
    return true;
}

} // namespace clean_aot::app
