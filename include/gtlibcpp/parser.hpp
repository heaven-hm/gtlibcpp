#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "result.hpp"
#include "types.hpp"

namespace gtlibcpp {

enum class VariableType : std::uint8_t {
    unknown = 0,
    uint8,
    int8,
    uint16,
    int16,
    uint32,
    int32,
    uint64,
    int64,
    float32,
    float64,
    string,
    bytes,
};

struct EntryId {
    std::string value{};
    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
    bool operator==(const EntryId& other) const noexcept { return value == other.value; }
    bool operator!=(const EntryId& other) const noexcept { return !(*this == other); }
};

} // namespace gtlibcpp

namespace std {
template <>
struct hash<gtlibcpp::EntryId> {
    std::size_t operator()(const gtlibcpp::EntryId& id) const noexcept {
        return std::hash<std::string>{}(id.value);
    }
};
}

namespace gtlibcpp {

struct CheatEntry {
    EntryId     id{};
    std::string description{};
    std::string section{};
    Address     address{0};
    std::vector<Address> offsets{};
    VariableType type{VariableType::unknown};
    bool        is_signed{false};
    std::string hotkey_action{};
    std::vector<int> hotkeys{};
    std::string default_value_text{};
    bool        auto_assembler{false};
    bool        shell_command{false};
    bool        dll_load{false};
    bool        raw_byte_write{false};
    std::string failure_reason{};
};

struct CheatTable {
    std::string title{};
    std::uint32_t version_ce{0};
    std::vector<CheatEntry> entries{};
    std::vector<std::string> diagnostics{};
    std::vector<CheatEntry> unsupported_entries{};
};

class CheatTableParser {
public:
    CheatTableParser();
    ~CheatTableParser();

    CheatTableParser(const CheatTableParser&) = delete;
    CheatTableParser& operator=(const CheatTableParser&) = delete;

    [[nodiscard]] Result<CheatTable> parse_file(const std::string& path) const;
    [[nodiscard]] Result<CheatTable> parse_string(const std::string& xml) const;

    [[nodiscard]] static std::string sanitize(const std::string& input);

    [[nodiscard]] static Result<VariableType>
    parse_variable_type(const std::string& text, bool& is_signed);

    [[nodiscard]] static std::string to_string(VariableType t);
};

} // namespace gtlibcpp
