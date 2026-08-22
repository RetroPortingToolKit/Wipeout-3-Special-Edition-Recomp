#include "clean_aot/app/memory_card.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace clean_aot::app {

bool MemoryCard::load_or_create(const std::filesystem::path& path) noexcept {
    try {
        if (path.empty() || std::filesystem::is_symlink(path)) return false;
        path_ = path;
        if (std::filesystem::exists(path_)) {
            if (!std::filesystem::is_regular_file(path_) ||
                std::filesystem::file_size(path_) != kMemoryCardBytes) return false;
            std::ifstream input(path_, std::ios::binary);
            input.read(reinterpret_cast<char*>(data_.data()),
                       static_cast<std::streamsize>(data_.size()));
            if (!input || input.peek() != std::ifstream::traits_type::eof()) return false;
            loaded_ = true;
            dirty_ = false;
            return true;
        }
        std::filesystem::create_directories(path_.parent_path());
        data_.fill(0U);
        loaded_ = true;
        dirty_ = true;
        return flush();
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
    try {
        const auto temporary = path_.parent_path() / (path_.filename().string() + ".new");
        if (std::filesystem::exists(temporary)) std::filesystem::remove(temporary);
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(data_.data()),
                         static_cast<std::streamsize>(data_.size()));
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
        if (MoveFileExW(temporary.c_str(), path_.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) return false;
#else
        std::filesystem::rename(temporary, path_);
#endif
        dirty_ = false;
        return true;
    } catch (...) { return false; }
}

} // namespace clean_aot::app
