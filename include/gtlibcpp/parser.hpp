/*
 * CheatTableParser — versioned, XML-driven parser for Cheat Engine .ct
 * files. The legacy CEParser.{hpp,cpp} was regex-based and lost
 * metadata; the issue requires a real parser that:
 *   - uses a maintained XML library (pugixml is header-only and
 *     permissively licensed, so we vendor it),
 *   - exposes a versioned supported subset (CE 7.x entries with a fixed
 *     variable schema),
 *   - preserves nested entries, multiple hotkeys, string metadata,
 *     signed values, and hex values,
 *   - returns per-entry diagnostics, and
 *   - fails closed on unsupported Auto Assembler entries, shell-exec
 *     entries, DLL load entries, and unconstrained byte writes.
 *
 * The parser is dependency-injected: it does not know about Win32. The
 * Windows backend and the agent service consume the resulting table.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "result.hpp"
#include "types.hpp"

namespace gtlibcpp {

// One Cheat Engine variable. The parser supports the long, byte, float,
// double, string, and 2-byte/4-byte/8-byte signed/unsigned forms that
// ship with CE 7.x. The legacy code conflated "uint8" and "char" and
// allowed character extraction; this enum forbids that confusion.
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

// Stable ID for an entry. CE 7.x files do not always assign an ID, so
// the parser derives one from the entry's text and address. The ID is
// stable across reloads of the same file.
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
    std::string hotkey_action{};       // "set value" | "freeze" | "increase" | "decrease" | ...
    std::vector<int> hotkeys{};        // VK_* values; empty == un-keyed entry
    std::string default_value_text{};  // textual form of the value in the .ct
    bool        auto_assembler{false}; // true => "must fail closed"
    bool        shell_command{false};
    bool        dll_load{false};
    bool        raw_byte_write{false};
    std::string failure_reason{};      // populated when entry is unsupported
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

    // Strips control characters that the legacy regex parser tolerated
    // and that crash the XML parser.
    [[nodiscard]] static std::string sanitize(const std::string& input);

    // Maps a Cheat Engine "type" string ("Byte", "2 Bytes", "Float", ...)
    // to the typed enum. The legacy parser at GTLibc.cpp:1370-1392
    // matched on string contains, which gave the wrong answer for
    // "2 Bytes" (matched the "Byte" case) and pulled character data
    // out of a uint8 value.
    [[nodiscard]] static Result<VariableType>
    parse_variable_type(const std::string& text, bool& is_signed);

    // Stable, human-readable name for a variable type. Used by the
    // agent to label its preview payload.
    [[nodiscard]] static std::string to_string(VariableType t);
};

} // namespace gtlibcpp
