#include "json.hpp"

#include <charconv>
#include <limits>

namespace clean_aot::app::detail {

const JsonObject* Json::object() const noexcept { return std::get_if<JsonObject>(&value); }
const JsonArray* Json::array() const noexcept { return std::get_if<JsonArray>(&value); }
const std::string* Json::string() const noexcept { return std::get_if<std::string>(&value); }
const std::uint64_t* Json::number() const noexcept { return std::get_if<std::uint64_t>(&value); }

namespace {
class Parser final {
public:
    explicit Parser(std::string_view input) : input_(input) {}

    bool parse(Json& out, std::string& error) {
        if (!value(out, 0U)) { error = error_; return false; }
        whitespace();
        if (position_ != input_.size()) { error = "trailing JSON data"; return false; }
        return true;
    }

private:
    bool value(Json& out, std::size_t depth) {
        if (depth > 64U) return fail("JSON nesting exceeds limit");
        whitespace();
        if (position_ >= input_.size()) return fail("unexpected end of JSON");
        const char c = input_[position_];
        if (c == '{') return object(out, depth + 1U);
        if (c == '[') return array(out, depth + 1U);
        if (c == '"') { std::string s; if (!string(s)) return false; out.value = std::move(s); return true; }
        if (c >= '0' && c <= '9') return number(out);
        if (literal("true")) { out.value = true; return true; }
        if (literal("false")) { out.value = false; return true; }
        if (literal("null")) { out.value = nullptr; return true; }
        return fail("unsupported JSON token");
    }

    bool object(Json& out, std::size_t depth) {
        ++position_; JsonObject result; whitespace();
        if (take('}')) { out.value = std::move(result); return true; }
        for (;;) {
            std::string key; if (!string(key)) return false;
            whitespace(); if (!take(':')) return fail("missing object colon");
            Json item; if (!value(item, depth)) return false;
            if (!result.emplace(std::move(key), std::move(item)).second)
                return fail("duplicate object key");
            whitespace();
            if (take('}')) break;
            if (!take(',')) return fail("missing object comma");
            whitespace();
        }
        out.value = std::move(result); return true;
    }

    bool array(Json& out, std::size_t depth) {
        ++position_; JsonArray result; whitespace();
        if (take(']')) { out.value = std::move(result); return true; }
        for (;;) {
            Json item; if (!value(item, depth)) return false;
            result.push_back(std::move(item)); whitespace();
            if (take(']')) break;
            if (!take(',')) return fail("missing array comma");
        }
        out.value = std::move(result); return true;
    }

    bool string(std::string& out) {
        whitespace(); if (!take('"')) return fail("expected JSON string");
        while (position_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[position_++]);
            if (c == '"') return true;
            if (c < 0x20U || c >= 0x80U) return fail("JSON strings must be printable ASCII");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            if (position_ >= input_.size()) return fail("truncated JSON escape");
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': case '\\': case '/': out.push_back(escaped); break;
            case 'b': out.push_back('\b'); break; case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break; case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: return fail("unsupported JSON escape");
            }
        }
        return fail("unterminated JSON string");
    }

    bool number(Json& out) {
        const std::size_t start = position_;
        if (input_[position_] == '0') ++position_;
        else while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        if (position_ < input_.size() && (input_[position_] == '.' || input_[position_] == 'e' || input_[position_] == 'E'))
            return fail("non-integer JSON number");
        std::uint64_t value = 0U;
        const auto converted = std::from_chars(input_.data() + start, input_.data() + position_, value);
        if (converted.ec != std::errc{}) return fail("JSON integer overflow");
        out.value = value; return true;
    }

    bool literal(std::string_view token) {
        if (input_.substr(position_, token.size()) != token) return false;
        position_ += token.size(); return true;
    }
    bool take(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) { ++position_; return true; }
        return false;
    }
    void whitespace() { while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\n' || input_[position_] == '\r' || input_[position_] == '\t')) ++position_; }
    bool fail(const char* message) { error_ = message; return false; }
    std::string_view input_; std::size_t position_ = 0U; std::string error_;
};

void append_string(std::string& out, std::string_view value) {
    out.push_back('"');
    for (const char c : value) {
        switch (c) {
        case '"': out += "\\\""; break; case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break; case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break; case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break; default: out.push_back(c); break;
        }
    }
    out.push_back('"');
}

void append_canonical(std::string& out, const Json& json) {
    if (std::holds_alternative<std::nullptr_t>(json.value)) { out += "null"; return; }
    if (const bool* value = std::get_if<bool>(&json.value)) { out += *value ? "true" : "false"; return; }
    if (const auto* value = json.number()) { out += std::to_string(*value); return; }
    if (const auto* value = json.string()) { append_string(out, *value); return; }
    if (const auto* value = json.array()) {
        out.push_back('['); bool first = true;
        for (const Json& item : *value) { if (!first) out.push_back(','); first = false; append_canonical(out, item); }
        out.push_back(']'); return;
    }
    out.push_back('{'); bool first = true;
    for (const auto& [key, value] : *json.object()) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        append_string(out, key);
        out.push_back(':');
        append_canonical(out, value);
    }
    out.push_back('}');
}
} // namespace

bool parse_json(std::string_view text, Json& result, std::string& error) noexcept {
    try { return Parser{text}.parse(result, error); }
    catch (...) { error = "JSON allocation failure"; return false; }
}

std::string canonical_json(const Json& value) { std::string result; append_canonical(result, value); return result; }

const Json* member(const Json& value, std::string_view key) noexcept {
    const JsonObject* object = value.object(); if (object == nullptr) return nullptr;
    const auto found = object->find(key); return found == object->end() ? nullptr : &found->second;
}

} // namespace clean_aot::app::detail
