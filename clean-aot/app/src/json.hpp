#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace clean_aot::app::detail {

struct Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::map<std::string, Json, std::less<>>;

struct Json final {
    using Value = std::variant<std::nullptr_t, bool, std::uint64_t,
                               std::string, JsonArray, JsonObject>;
    Value value{};

    [[nodiscard]] const JsonObject* object() const noexcept;
    [[nodiscard]] const JsonArray* array() const noexcept;
    [[nodiscard]] const std::string* string() const noexcept;
    [[nodiscard]] const std::uint64_t* number() const noexcept;
};

[[nodiscard]] bool parse_json(std::string_view text, Json& result,
                              std::string& error) noexcept;
[[nodiscard]] std::string canonical_json(const Json& value);
[[nodiscard]] const Json* member(const Json& value, std::string_view key) noexcept;

} // namespace clean_aot::app::detail
