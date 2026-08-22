#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace clean_aot::app::detail {

[[nodiscard]] std::string sha256(std::span<const std::uint8_t> bytes);

} // namespace clean_aot::app::detail
