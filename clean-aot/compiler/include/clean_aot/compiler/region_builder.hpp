#pragma once

#include "clean_aot/compiler/ir.hpp"
#include "clean_aot/compiler/psx_exe.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace clean_aot::compiler {

struct RegionBuildOptions final {
    std::vector<GuestAddress> entry_points;
    std::optional<GuestAddress> start_inclusive;
    std::optional<std::uint64_t> end_exclusive;
};

class RegionBuilder final {
public:
    [[nodiscard]] static Region build(const PsxExeImage& image,
                                      RegionBuildOptions options);
};

} // namespace clean_aot::compiler
