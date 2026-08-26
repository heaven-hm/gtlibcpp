/*
 * Policy — target manifest, capabilities, approval tokens, rate limits.
 *
 * The agent service exposes mutations only after Policy.authorize returns
 * success. Policy is in-process and stateless, so a test or the core
 * library can run the same gates without involving any IPC.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "result.hpp"
#include "types.hpp"

namespace gtlibcpp {

// Mutations the agent layer will accept. Read-only defaults to on; every
// other capability must be granted by an explicit, short-lived token.
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

// Declares the set of targets a deployment is allowed to mutate. The
// agent service refuses any TargetIdentity whose path/hash/arch is not
// listed here.
struct TargetManifest {
    std::string  alias{};                 // human-readable handle
    std::string  image_path{};            // expected EXE path
    std::string  image_sha256{};          // expected image hash (lower hex)
    std::string  architecture{};          // "x86" or "x64"
    bool         allow_write{false};      // false => read-only

    [[nodiscard]] bool matches(const TargetIdentity& id) const noexcept;
};

// An audit record. The agent writes one per request.
struct AuditEvent {
    std::string  request_id{};
    std::string  alias{};
    std::uint32_t pid{};
    std::string  operation{};
    std::string  capability{};
    std::string  decision{};              // "allow" / "deny" / "preview"
    std::string  reason{};
    std::int64_t timestamp_ms{};
};

// A single requested mutation. Constructed by the agent, validated by
// Policy, executed by the core. Captures every precondition the issue
// requires (target alias, capability, preview flag, approval, expected
// current-value hash).
struct MutationRequest {
    std::string  request_id{};
    std::string  target_alias{};
    Capability   capability{Capability::read};
    bool         preview{false};
    bool         approved{false};
    std::string  expected_current_hash{};  // hex digest of current value
    Address      address{0};
    std::size_t  size{0};
    std::string  value_repr{};            // canonical text of the intended value
};

// Approval token. The agent mints these only after a human (or a higher
// level of automation) has signed off. Tokens are short-lived and
// single-shot.
struct ApprovalToken {
    std::string  request_id{};
    std::string  target_alias{};
    std::int64_t issued_at_ms{};
    std::int64_t expires_at_ms{};
    std::string  signature{};             // opaque to the policy; the agent
                                           // checks it before minting.
};

// Simple per-target sliding-window rate limiter. Issue acceptance
// requires "rate limits" for the agent; the policy layer carries the
// counter so it is exercised in tests.
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

// A kill switch. Once flipped, every authorize() call returns a deny
// with code kill_switch_active. Exposed so the test suite and the agent
// can both exercise it.
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

// Top-level policy. Holds the manifest, the rate limiter, the kill
// switch, and the audit sink. Audit events are appended via a callback so
// the agent can forward them to its structured log without coupling the
// core to a specific log implementation.
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

    // Emit an audit event. The constructor-supplied sink receives it; if
    // no sink is registered, the event is dropped on the floor (the
    // agent service registers one in production).
    void emit_audit(AuditEvent event) const noexcept;

private:
    [[nodiscard]] const TargetManifest*
    find_manifest(const std::string& alias) const noexcept;

    std::vector<TargetManifest> manifests_;
    AuditSink sink_;
    void*      sink_user_;
    mutable RateLimiter limiter_{120};     // 120 mutations / minute / target
    KillSwitch kill_switch_;
};

} // namespace gtlibcpp
