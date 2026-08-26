#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "result.hpp"
#include "types.hpp"

namespace gtlibcpp {

enum class Capability : std::uint32_t {
    read         = 1u << 0,
    write        = 1u << 1,
    freeze       = 1u << 2,
    unfreeze     = 1u << 3,
    rollback     = 1u << 4,
    parse        = 1u << 5,
};

inline Capability operator|(Capability a, Capability b) noexcept {
    return static_cast<Capability>(static_cast<std::uint32_t>(a) |
                                   static_cast<std::uint32_t>(b));
}
inline std::uint32_t as_bits(Capability c) noexcept {
    return static_cast<std::uint32_t>(c);
}

struct TargetManifest {
    std::string  alias{};
    std::string  image_path{};
    std::string  image_sha256{};
    std::string  architecture{};
    bool         allow_write{false};

    [[nodiscard]] bool matches(const TargetIdentity& id) const noexcept;
};

struct AuditEvent {
    std::string  request_id{};
    std::string  alias{};
    std::uint32_t pid{};
    std::string  operation{};
    std::string  capability{};
    std::string  decision{};
    std::string  reason{};
    std::int64_t timestamp_ms{};
};

struct MutationRequest {
    std::string  request_id{};
    std::string  target_alias{};
    Capability   capability{Capability::read};
    bool         preview{false};
    bool         approved{false};
    std::string  expected_current_hash{};
    Address      address{0};
    std::size_t  size{0};
    std::string  value_repr{};
};

struct ApprovalToken {
    std::string  request_id{};
    std::string  target_alias{};
    std::int64_t issued_at_ms{};
    std::int64_t expires_at_ms{};
    std::string  signature{};
};

class RateLimiter {
public:
    explicit RateLimiter(std::size_t max_per_minute);

    [[nodiscard]] Result<void> allow(const std::string& alias);

private:
    struct Window {
        std::int64_t window_start_ms{};
        std::size_t  count{0};
    };
    std::size_t max_per_minute_;
    std::unordered_map<std::string, Window> windows_;
};

class KillSwitch {
public:
    void engage(std::string reason) noexcept;
    void release() noexcept;
    [[nodiscard]] bool engaged() const noexcept;
    [[nodiscard]] const std::string& reason() const noexcept;

private:
    std::atomic_flag engaged_{};
    std::string reason_{};
};

class Policy {
public:
    using AuditSink = void (*)(const AuditEvent&, void* user);
    Policy(std::vector<TargetManifest> manifests,
           AuditSink sink = nullptr,
           void* user = nullptr);

    [[nodiscard]] Result<void> authorize(const TargetIdentity& identity,
                                         const MutationRequest& request) const;

    [[nodiscard]] Result<void> authorize_with_token(
        const TargetIdentity& identity,
        const MutationRequest& request,
        const ApprovalToken* token) const;

    void set_kill_switch_reason(std::string reason) noexcept;

    [[nodiscard]] const std::vector<TargetManifest>& manifests() const noexcept {
        return manifests_;
    }

    [[nodiscard]] RateLimiter& rate_limiter() noexcept { return limiter_; }
    [[nodiscard]] const RateLimiter& rate_limiter() const noexcept { return limiter_; }

    [[nodiscard]] const KillSwitch& kill_switch() const noexcept { return kill_switch_; }

    void emit_audit(AuditEvent event) const noexcept;

private:
    [[nodiscard]] const TargetManifest*
    find_manifest(const std::string& alias) const noexcept;

    std::vector<TargetManifest> manifests_;
    AuditSink sink_;
    void*      sink_user_;
    mutable RateLimiter limiter_{120};
    KillSwitch kill_switch_;
};

} // namespace gtlibcpp
