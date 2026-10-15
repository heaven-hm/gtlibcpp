#pragma once

#include <cstdint>
#include <string>

namespace gtlibcpp {

// Process-architecture type. The legacy code used WORD with IsWow64, but
// never recorded the actual target arch; we make it explicit because the
// pointer size depends on it. The library supports both 32-bit and
// 64-bit games: `x86` covers the legacy Win32 trainers, and `x64`
// covers the modern x64 process. `Architecture` is recorded at attach
// time, carried in the `TargetIdentity`, and used by the agent service
// to pick the correct pointer width for pointer-chain resolution and
// the correct value-encoding for typed read/write/verify.
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

using Address = std::uint64_t;
constexpr Address invalid_address = 0;

struct TargetIdentity {
    std::uint32_t pid{};
    std::uint64_t start_time{};
    std::string   image_path{};
    std::string   image_sha256{};
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

struct ExpectedValue {
    std::string kind{"hex"};
    std::string value{};

    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
};

} // namespace gtlibcpp
