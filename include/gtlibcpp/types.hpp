/*
 * Architecture-safe address and target identity types.
 *
 * Legacy GTLibc used DWORD for every address, which truncates on x64.
 * Issue #1 requires std::uintptr_t-based types and a typed target
 * identity that records pid, path, image hash, start time, and
 * architecture so a reattach cannot silently jump to a different
 * process.
 */
#pragma once

#include <cstdint>
#include <string>

namespace gtlibcpp {

// Process-architecture type. The legacy code used WORD with IsWow64, but
// never recorded the actual target arch; we make it explicit because the
// pointer size depends on it.
enum class Architecture : std::uint8_t {
    unknown = 0,
    x86     = 1,
    x64     = 2,
};

inline const char* to_string(Architecture arch) noexcept {
    switch (arch) {
        case Architecture::x86:     return "x86";
        case Architecture::x64:     return "x64";
        case Architecture::unknown: return "unknown";
    }
    return "unknown";
}

// Architecture-safe address. The legacy code used DWORD throughout, which
// silently truncated the high half of a 64-bit address.
using Address = std::uint64_t;
constexpr Address invalid_address = 0;

// Describes a process the agent is authorised to mutate. Captured at
// attach time and re-checked on every operation, so a reattach to a
// recycled pid cannot satisfy an old approval.
struct TargetIdentity {
    std::uint32_t pid{};
    std::uint64_t start_time{};   // FILETIME tick count or equivalent
    std::string   image_path{};
    std::string   image_sha256{}; // hex digest, lower-case
    Architecture  architecture{Architecture::unknown};

    [[nodiscard]] bool empty() const noexcept { return pid == 0; }

    bool operator==(const TargetIdentity& other) const noexcept {
        return pid == other.pid
            && start_time == other.start_time
            && image_path == other.image_path
            && image_sha256 == other.image_sha256
            && architecture == other.architecture;
    }
    bool operator!=(const TargetIdentity& other) const noexcept {
        return !(*this == other);
    }
};

// Describes the value the agent expects to read at a given address. The
// agent compares this against a fresh read before issuing the mutation,
// per issue requirement #11 ("compare-before-write preconditions").
struct ExpectedValue {
    std::string kind{"hex"};      // "hex" | "int" | "float" | "double" | "string"
    std::string value{};          // canonical hex/decimal/text representation

    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
};

} // namespace gtlibcpp
