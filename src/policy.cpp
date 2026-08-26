// Policy, RateLimiter, and KillSwitch implementation. Pure-stdlib so the
// policy layer compiles and runs identically on Windows and on the test
// host (macOS/Linux).
#include "gtlibcpp/policy.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace gtlibcpp {

namespace {

std::int64_t now_ms() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string cap_to_string(Capability c) {
    std::string out;
    if (as_bits(c) & as_bits(Capability::read))     out += "read ";
    if (as_bits(c) & as_bits(Capability::write))    out += "write ";
    if (as_bits(c) & as_bits(Capability::freeze))   out += "freeze ";
    if (as_bits(c) & as_bits(Capability::unfreeze)) out += "unfreeze ";
    if (as_bits(c) & as_bits(Capability::rollback)) out += "rollback ";
    if (as_bits(c) & as_bits(Capability::parse))    out += "parse ";
    if (out.empty()) out = "none";
    if (out.back() == ' ') out.pop_back();
    return out;
}

} // namespace

bool TargetManifest::matches(const TargetIdentity& id) const noexcept {
    if (id.empty()) return false;
    if (id.image_path != image_path) return false;
    if (!image_sha256.empty() && !id.image_sha256.empty()
        && id.image_sha256 != image_sha256) {
        return false;
    }
    if (!architecture.empty()) {
        if (architecture == "x86" && id.architecture != Architecture::x86) return false;
        if (architecture == "x64" && id.architecture != Architecture::x64) return false;
    }
    return true;
}

RateLimiter::RateLimiter(std::size_t max_per_minute)
    : max_per_minute_(max_per_minute) {}

Result<void> RateLimiter::allow(const std::string& alias) {
    const std::int64_t now = now_ms();
    auto& window = windows_[alias];
    if (window.window_start_ms == 0
        || now - window.window_start_ms >= 60'000) {
        window.window_start_ms = now;
        window.count = 0;
    }
    if (window.count >= max_per_minute_) {
        return Result<void>::failure(make_error(
            ErrorCode::rate_limited,
            "rate limit exceeded for target " + alias,
            "RateLimiter::allow"));
    }
    ++window.count;
    return Result<void>::success();
}

void KillSwitch::engage(std::string reason) noexcept {
    reason_ = std::move(reason);
    engaged_.test_and_set();
}
void KillSwitch::release() noexcept {
    engaged_.clear();
    reason_.clear();
}
bool KillSwitch::engaged() const noexcept { return engaged_.test(); }
const std::string& KillSwitch::reason() const noexcept { return reason_; }

Policy::Policy(std::vector<TargetManifest> manifests, AuditSink sink, void* user)
    : manifests_(std::move(manifests)), sink_(sink), sink_user_(user) {}

const TargetManifest* Policy::find_manifest(const std::string& alias) const noexcept {
    for (const auto& m : manifests_) {
        if (m.alias == alias) return &m;
    }
    return nullptr;
}

Result<void> Policy::authorize(const TargetIdentity& identity,
                               const MutationRequest& request) const {
    return authorize_with_token(identity, request, nullptr);
}

Result<void> Policy::authorize_with_token(const TargetIdentity& identity,
                                          const MutationRequest& request,
                                          const ApprovalToken* token) const {
    auto deny = [&](ErrorCode code, const std::string& reason) -> Result<void> {
        AuditEvent ev{};
        ev.request_id = request.request_id;
        ev.alias = request.target_alias;
        ev.pid = identity.pid;
        ev.operation = request.value_repr;
        ev.capability = cap_to_string(request.capability);
        ev.decision = "deny";
        ev.reason = reason;
        ev.timestamp_ms = now_ms();
        emit_audit(std::move(ev));
        return Result<void>::failure(make_error(code, reason, "Policy::authorize"));
    };

    if (kill_switch_.engaged()) {
        return deny(ErrorCode::policy_denied,
                    "kill switch engaged: " + kill_switch_.reason());
    }

    const TargetManifest* manifest = find_manifest(request.target_alias);
    if (!manifest) {
        return deny(ErrorCode::policy_denied,
                    "no manifest for target alias " + request.target_alias);
    }
    if (!manifest->matches(identity)) {
        return deny(ErrorCode::target_identity_mismatch,
                    "target identity does not match the authorised manifest");
    }
    const std::uint32_t bits = as_bits(request.capability);
    if ((bits & as_bits(Capability::write)) && !manifest->allow_write) {
        return deny(ErrorCode::policy_denied,
                    "manifest does not grant write");
    }
    if (bits & ~as_bits(Capability::read | Capability::write
                         | Capability::freeze | Capability::unfreeze
                         | Capability::rollback | Capability::parse)) {
        return deny(ErrorCode::capability_missing, "unknown capability requested");
    }
    if (request.address == invalid_address) {
        return deny(ErrorCode::invalid_address, "request has no concrete address");
    }
    if (request.size == 0 || request.size > (1u << 20)) {
        return deny(ErrorCode::invalid_size, "request size is out of bounds");
    }
    if (request.expected_current_hash.empty()) {
        return deny(ErrorCode::policy_denied,
                    "expected_current_hash is required for every mutation");
    }
    if (request.preview) {
        AuditEvent ev{};
        ev.request_id = request.request_id;
        ev.alias = request.target_alias;
        ev.pid = identity.pid;
        ev.operation = request.value_repr;
        ev.capability = cap_to_string(request.capability);
        ev.decision = "preview";
        ev.reason = "preview requested";
        ev.timestamp_ms = now_ms();
        emit_audit(std::move(ev));
        return Result<void>::success();
    }
    if (!request.approved) {
        return deny(ErrorCode::approval_required, "mutation requires approval");
    }
    if (token) {
        if (token->request_id != request.request_id) {
            return deny(ErrorCode::policy_denied,
                        "approval token request_id does not match");
        }
        if (token->target_alias != request.target_alias) {
            return deny(ErrorCode::policy_denied,
                        "approval token alias does not match");
        }
        const std::int64_t now = now_ms();
        if (now < token->issued_at_ms || now > token->expires_at_ms) {
            return deny(ErrorCode::policy_denied,
                        "approval token is not within its TTL window");
        }
    }
    auto limit = limiter_.allow(request.target_alias);
    if (!limit) {
        return deny(ErrorCode::rate_limited, limit.error().message);
    }
    AuditEvent ev{};
    ev.request_id = request.request_id;
    ev.alias = request.target_alias;
    ev.pid = identity.pid;
    ev.operation = request.value_repr;
    ev.capability = cap_to_string(request.capability);
    ev.decision = "allow";
    ev.reason = "approved";
    ev.timestamp_ms = now_ms();
    emit_audit(std::move(ev));
    return Result<void>::success();
}

void Policy::set_kill_switch_reason(std::string reason) noexcept {
    kill_switch_.engage(std::move(reason));
}

void Policy::emit_audit(AuditEvent event) const noexcept {
    if (!sink_) return;
    try {
        sink_(event, sink_user_);
    } catch (...) {
        // The audit sink is best-effort. A misbehaving sink must not
        // crash the policy layer.
    }
}

} // namespace gtlibcpp
