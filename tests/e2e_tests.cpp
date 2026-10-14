// End-to-End Test Suite: Comprehensive validation army
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gtlibcpp/agent.hpp"
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/inproc_transport.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/parser.hpp"
#include "gtlibcpp/policy.hpp"
#include "gtlibcpp_test.hpp"
#include "../examples/simulated_game.hpp"

#define TEST_CASE(name) GTLIBCPP_TEST(name)
#define REQUIRE(c) GTLIBCPP_REQUIRE(c)
#define REQUIRE_EQ(a, b) GTLIBCPP_REQUIRE_EQ(a, b)
#define REQUIRE_FALSE(c) GTLIBCPP_REQUIRE_FALSE(c)

namespace {

TEST_CASE(e2e_memory_session_full_lifecycle) {
    auto game = std::make_shared<gtlibcpp::demo::SimulatedGameProcess>();
    gtlibcpp::MemorySession session(game);

    REQUIRE(session.is_alive());
    REQUIRE_EQ(session.identity().pid, static_cast<std::uint32_t>(4242));
    REQUIRE_EQ(session.identity().architecture, gtlibcpp::Architecture::x64);

    // Resolve pointer chain: Base -> +0x50 to Stats struct
    std::vector<gtlibcpp::Address> offsets = {0, 0x50};
    auto resolved = session.resolve_pointer_chain(gtlibcpp::demo::SimulatedGameProcess::kPlayerPtrAddr, offsets);
    REQUIRE(resolved.ok());
    const auto stats_addr = resolved.value();
    REQUIRE_EQ(stats_addr, gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr);
    const auto health_addr = stats_addr + gtlibcpp::demo::SimulatedGameProcess::kHealthOffset;
    REQUIRE_EQ(health_addr, gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kHealthOffset);

    // Read initial health
    auto health = session.read<std::uint32_t>(health_addr);
    REQUIRE(health.ok());
    REQUIRE_EQ(health.value(), static_cast<std::uint32_t>(100));

    // Write Godmode health
    auto write_res = session.write<std::uint32_t>(health_addr, 9999);
    REQUIRE(write_res.ok());

    auto health_after = session.read<std::uint32_t>(health_addr);
    REQUIRE(health_after.ok());
    REQUIRE_EQ(health_after.value(), static_cast<std::uint32_t>(9999));

    // String read/write
    const gtlibcpp::Address name_addr = gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kNameOffset;
    auto name_res = session.read_string(name_addr, 32, false);
    REQUIRE(name_res.ok());
    std::string current_name(reinterpret_cast<const char*>(name_res.value().bytes().data()));
    REQUIRE_EQ(current_name, std::string("Commander_Shepard"));

    std::string new_name = "Vanguard_Hero";
    std::vector<std::uint8_t> new_bytes(new_name.begin(), new_name.end());
    new_bytes.push_back(0);
    auto bounded_name = gtlibcpp::BoundedString::from_utf8(new_bytes, 32, true);
    REQUIRE(bounded_name.ok());

    auto write_name = session.write_string(name_addr, bounded_name.value(), 32);
    REQUIRE(write_name.ok());

    auto name_after = session.read_string(name_addr, 32, true);
    REQUIRE(name_after.ok());
    std::string updated_name(reinterpret_cast<const char*>(name_after.value().bytes().data()));
    REQUIRE_EQ(updated_name, std::string("Vanguard_Hero"));

    // Compare and write verify
    const gtlibcpp::Address armor_addr = gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kArmorOffset;
    auto cas_ok = session.compare_write_verify<float>(armor_addr, 100.0f);
    REQUIRE(cas_ok.ok());
    REQUIRE_EQ(session.read<float>(armor_addr).value(), 100.0f);
}

TEST_CASE(e2e_freeze_worker_lock_and_restore) {
    auto game = std::make_shared<gtlibcpp::demo::SimulatedGameProcess>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(game);
    auto freeze_mgr = std::make_shared<gtlibcpp::FreezeManager>(session);

    const gtlibcpp::Address ammo_addr = gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kAmmoOffset;

    // Freeze ammo to 99
    gtlibcpp::FreezeRequest req{};
    req.id = "ammo_freeze";
    req.address = ammo_addr;
    req.type_name = "uint32";
    req.value_u64 = 99;
    req.interval = std::chrono::milliseconds(10);

    auto res = freeze_mgr->freeze(req);
    REQUIRE(res.ok());

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    REQUIRE_EQ(session->read<std::uint32_t>(ammo_addr).value(), static_cast<std::uint32_t>(99));

    // Tamper with memory (simulating shooting)
    REQUIRE(session->write<std::uint32_t>(ammo_addr, 5).ok());

    // Worker must intercept and rewrite
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    REQUIRE_EQ(session->read<std::uint32_t>(ammo_addr).value(), static_cast<std::uint32_t>(99));

    // Check status metrics
    auto st = freeze_mgr->status("ammo_freeze");
    REQUIRE(st.ok());
    REQUIRE(st.value().active);
    REQUIRE(st.value().successful_rewrites >= 1);
    REQUIRE_EQ(st.value().original_value_u64, static_cast<std::uint64_t>(30));

    // Restore and unfreeze
    REQUIRE(freeze_mgr->restore("ammo_freeze").ok());
    REQUIRE(freeze_mgr->unfreeze("ammo_freeze").ok());
    REQUIRE_EQ(session->read<std::uint32_t>(ammo_addr).value(), static_cast<std::uint32_t>(30));

    freeze_mgr->cancel_all();
}

TEST_CASE(e2e_cheat_parser_full_matrix) {
    gtlibcpp::CheatTableParser parser;

    const std::string xml = R"(<?xml version="1.0" encoding="utf-8"?>
<CheatTable CheatEngineTableVersion="34">
  <CheatEntries>
    <CheatEntry>
      <ID>2</ID>
      <Description>Player Ammo</Description>
      <VariableType>4 Bytes</VariableType>
      <Address>0x140100000</Address>
      <Offsets>
        <Offset>0x18</Offset>
      </Offsets>
      <Hotkeys>
        <Hotkey>
          <Action>Set Value</Action>
          <Code>112</Code>
        </Hotkey>
      </Hotkeys>
    </CheatEntry>
    <CheatEntry>
      <ID>3</ID>
      <Description>Forbidden Exploit</Description>
      <VariableType>Auto Assembler Script</VariableType>
      <AssemblerScript>alloc(mem, 1024)</AssemblerScript>
    </CheatEntry>
  </CheatEntries>
</CheatTable>)";

    auto res = parser.parse_string(xml);
    REQUIRE(res.ok());
    const auto& table = res.value();

    REQUIRE_EQ(table.entries.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(table.entries[0].id.value, std::string("id:2"));
    REQUIRE_EQ(table.entries[0].description, std::string("Player Ammo"));
    REQUIRE_EQ(table.entries[0].hotkeys.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(table.entries[0].hotkeys[0], 112); // F1

    REQUIRE_EQ(table.unsupported_entries.size(), static_cast<std::size_t>(1));
    REQUIRE(table.unsupported_entries[0].auto_assembler);
}

TEST_CASE(e2e_agent_jsonrpc_full_control_plane) {
    auto game = std::make_shared<gtlibcpp::demo::SimulatedGameProcess>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(game);
    auto freeze_mgr = std::make_shared<gtlibcpp::FreezeManager>(session);
    auto parser = std::make_shared<gtlibcpp::CheatTableParser>();

    std::vector<gtlibcpp::TargetManifest> manifests;
    manifests.push_back(gtlibcpp::TargetManifest{
        "CyberAssault",
        "C:/Games/CyberAssault/game.exe",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "x64",
        true,
    });

    auto policy = std::make_shared<gtlibcpp::Policy>(std::move(manifests));
    auto agent = std::make_shared<gtlibcpp::AgentService>(session, policy, freeze_mgr, parser);

    auto call = [&](const std::string& method, const std::string& params, const std::string& id = "1") -> std::string {
        std::string req = R"({"jsonrpc":"2.0","id":")" + id + R"(","method":")" + method + R"(","params":)" + params + "}";
        auto res = agent->handle(req);
        REQUIRE(res.ok());
        return res.value();
    };

    // 1. inspect
    std::string inspect_out = call("inspect", "{}", "1");
    REQUIRE(inspect_out.find("\"pid\":4242") != std::string::npos);

    // 2. preview
    const gtlibcpp::Address health_addr = gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kHealthOffset;
    std::string preview_out = call("preview", R"({
        "request_id":"op-e2e","target_alias":"CyberAssault","capability":"write",
        "address":)" + std::to_string(health_addr) + R"(,"size":4,"expected_current_hash":"abc",
        "value_hex":"e7030000","value_repr":"uint32 999"})", "2");
    REQUIRE(preview_out.find("\"error\"") == std::string::npos);

    // 3. apply
    std::string apply_out = call("apply", R"({
        "request_id":"op-e2e","target_alias":"CyberAssault","capability":"write",
        "address":)" + std::to_string(health_addr) + R"(,"size":4,"expected_current_hash":"abc",
        "approved":true,"value_hex":"e7030000","value_repr":"uint32 999"})", "3");
    REQUIRE(apply_out.find("\"error\"") == std::string::npos);
    REQUIRE_EQ(session->read<std::uint32_t>(health_addr).value(), static_cast<std::uint32_t>(999));

    // 4. freeze
    std::string freeze_preview = call("preview", R"({
        "request_id":"op-e2e-freeze","target_alias":"CyberAssault","capability":"freeze",
        "address":)" + std::to_string(health_addr) + R"(,"size":4,
        "expected_current_hash":"abc","value_repr":"uint32 999",
        "id":"e2e_freeze","type":"uint32","value":999,"interval_ms":20})", "4p");
    REQUIRE(freeze_preview.find("\"error\"") == std::string::npos);
    std::string freeze_out = call("freeze", R"({
        "request_id":"op-e2e-freeze","target_alias":"CyberAssault","capability":"freeze",
        "approved":true,"expected_current_hash":"abc","value_repr":"uint32 999",
        "id":"e2e_freeze","address":)" + std::to_string(health_addr) + R"(,
        "size":4,"type":"uint32","value":999,"interval_ms":20})", "4");
    REQUIRE(freeze_out.find("\"error\"") == std::string::npos);

    // 5. status
    std::string status_out = call("status", R"({"id":"e2e_freeze"})", "5");
    REQUIRE(status_out.find("\"error\"") == std::string::npos);

    // 6. unfreeze
    std::string unfreeze_out = call("unfreeze", R"({"id":"e2e_freeze"})", "6");
    REQUIRE(unfreeze_out.find("\"error\"") == std::string::npos);

    // 7. kill_switch
    std::string kill_out = call("kill_switch", R"({"engage":true,"reason":"halt"})", "7");
    REQUIRE(kill_out.find("\"error\"") == std::string::npos);

    // 8. blocked mutation under kill switch
    std::string blocked_out = call("apply", R"({
        "request_id":"op-blocked","target_alias":"CyberAssault","capability":"write",
        "address":)" + std::to_string(health_addr) + R"(,"size":4,"expected_current_hash":"abc",
        "approved":true,"value_hex":"00000000","value_repr":"uint32 0"})", "8");
    REQUIRE(blocked_out.find("\"error\"") != std::string::npos);

    freeze_mgr->cancel_all();
}

TEST_CASE(e2e_security_policy_gate_and_audit) {
    std::vector<gtlibcpp::AuditEvent> audits;
    auto sink = [](const gtlibcpp::AuditEvent& ev, void* user) {
        auto* vec = static_cast<std::vector<gtlibcpp::AuditEvent>*>(user);
        vec->push_back(ev);
    };

    gtlibcpp::TargetManifest manifest{
        "ProtectedTarget", "C:/app.exe", "sha256:app", "x64", true
    };
    gtlibcpp::TargetManifest ro_manifest{
        "ReadOnlyTarget", "C:/ro.exe", "sha256:ro", "x64", false
    };

    gtlibcpp::Policy policy({manifest, ro_manifest}, sink, &audits);

    gtlibcpp::TargetIdentity target{
        1, 1, "C:/app.exe", "sha256:app", gtlibcpp::Architecture::x64
    };

    // Allowed in-bounds preview
    gtlibcpp::MutationRequest req{
        "req-preview", "ProtectedTarget", gtlibcpp::Capability::write,
        true, false, "dummy_hash", 0x1500, 4, "uint32"
    };
    REQUIRE(policy.authorize(target, req).ok());

    // Denied write on read-only manifest
    gtlibcpp::TargetIdentity ro_target{
        2, 1, "C:/ro.exe", "sha256:ro", gtlibcpp::Architecture::x64
    };
    gtlibcpp::MutationRequest ro_req{
        "req-ro", "ReadOnlyTarget", gtlibcpp::Capability::write,
        false, true, "dummy_hash", 0x1500, 4, "uint32"
    };
    auto ro_res = policy.authorize(ro_target, ro_req);
    REQUIRE_FALSE(ro_res.ok());
    REQUIRE_EQ(ro_res.error().code, gtlibcpp::ErrorCode::policy_denied);

    // Verify audits collected
    REQUIRE(audits.size() >= 2);
    REQUIRE_EQ(audits[0].decision, std::string("preview"));
    REQUIRE_EQ(audits[1].decision, std::string("deny"));
}

} // namespace

int main() {
    return gtlibcpp_test::run_all();
}
