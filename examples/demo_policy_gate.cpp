#include <iostream>
#include <cassert>
#include <chrono>
#include "gtlibcpp/policy.hpp"

namespace {
std::vector<gtlibcpp::AuditEvent> g_audit_logs;
void sample_audit_sink(const gtlibcpp::AuditEvent& ev, void*) {
    g_audit_logs.push_back(ev);
}
} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << "  E2E DEMO 5: Security Policy & Capability Gatekeeper   \n";
    std::cout << "========================================================\n\n";

    // 1. Configure Target Manifests
    gtlibcpp::TargetManifest read_write_manifest{
        "TacticalGame", "C:/Games/Tactical/game.exe", "sha256:target_hash", "x64", true
    };
    gtlibcpp::TargetManifest read_only_manifest{
        "LockedGame", "C:/Games/Locked/game.exe", "sha256:locked_hash", "x64", false
    };

    gtlibcpp::Policy policy({read_write_manifest, read_only_manifest}, sample_audit_sink, nullptr);

    gtlibcpp::TargetIdentity target{
        1234, 1, "C:/Games/Tactical/game.exe", "sha256:target_hash", gtlibcpp::Architecture::x64
    };

    // 2. Authorized In-Bounds Preview Request
    gtlibcpp::MutationRequest valid_req{
        "req-1", "TacticalGame", gtlibcpp::Capability::write,
        true, false, "current_hash_123", 0x140100000ULL, 4, "uint32 999"
    };

    auto auth_valid = policy.authorize(target, valid_req);
    assert(auth_valid.ok());
    std::cout << "[+] Write Preview Authorization: GRANTED\n";

    // 3. Denied Read-Only Target (Attempting write on read-only manifest)
    gtlibcpp::TargetIdentity locked_target{
        5678, 1, "C:/Games/Locked/game.exe", "sha256:locked_hash", gtlibcpp::Architecture::x64
    };
    gtlibcpp::MutationRequest ro_req{
        "req-2", "LockedGame", gtlibcpp::Capability::write,
        false, true, "current_hash_123", 0x140100000ULL, 4, "uint32 999"
    };
    auto auth_ro = policy.authorize(locked_target, ro_req);
    assert(!auth_ro.ok());
    assert(auth_ro.error().code == gtlibcpp::ErrorCode::policy_denied);
    std::cout << "[+] Unauthorized Write on Read-Only manifest: DENIED as expected\n";

    // 4. Test Approval Token with TTL
    valid_req.preview = false;
    valid_req.approved = true;

    // Expired Token
    gtlibcpp::ApprovalToken expired_token{};
    expired_token.request_id = "req-1";
    expired_token.target_alias = "TacticalGame";
    expired_token.issued_at_ms = 1000;
    expired_token.expires_at_ms = 2000;
    expired_token.capability = valid_req.capability;
    expired_token.address = valid_req.address;
    expired_token.size = valid_req.size;
    expired_token.expected_current_hash = valid_req.expected_current_hash;
    expired_token.value_repr = valid_req.value_repr;
    expired_token.signature = "sig";
    auto auth_expired = policy.authorize_with_token(target, valid_req, &expired_token);
    assert(!auth_expired.ok());
    assert(auth_expired.error().code == gtlibcpp::ErrorCode::policy_denied);
    std::cout << "[+] Expired Approval Token (Out of TTL): DENIED as expected\n";

    // Fresh Valid Token
    using namespace std::chrono;
    int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    auto fresh_token = expired_token;
    fresh_token.issued_at_ms = now - 1000;
    fresh_token.expires_at_ms = now + 60000;
    fresh_token.signature = "valid_sig";
    auto auth_fresh = policy.authorize_with_token(target, valid_req, &fresh_token);
    assert(auth_fresh.ok());
    std::cout << "[+] Valid Approval Token within TTL: GRANTED as expected\n";

    // 5. Emergency Kill Switch Trip
    std::cout << "[*] Engaging Emergency Kill Switch...\n";
    policy.set_kill_switch_reason("Security trip triggered by telemetry");
    auto auth_blocked = policy.authorize_with_token(target, valid_req, &fresh_token);
    assert(!auth_blocked.ok());
    assert(auth_blocked.error().code == gtlibcpp::ErrorCode::policy_denied);
    std::cout << "[+] Kill Switch engaged: All mutations DENIED\n";

    // 6. Verify Structured Audit Logging
    std::cout << "\n[+] Audit Telemetry Summary: " << g_audit_logs.size() << " events recorded\n";
    assert(g_audit_logs.size() >= 4);
    for (const auto& ev : g_audit_logs) {
        std::cout << "    [AUDIT] Action: " << ev.decision << ", Alias: " << ev.alias 
                  << ", Reason: " << ev.reason << "\n";
    }

    std::cout << "\n[SUCCESS] E2E Policy Gate Demo completed flawlessly!\n\n";
    return 0;
}
