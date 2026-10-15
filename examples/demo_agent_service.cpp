#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include "gtlibcpp/agent.hpp"
#include "gtlibcpp/inproc_transport.hpp"
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/policy.hpp"
#include "gtlibcpp/parser.hpp"
#include "simulated_game.hpp"

int main() {
    std::cout << "========================================================\n";
    std::cout << "  E2E DEMO 4: Autonomous Agent JSON-RPC 2.0 Control     \n";
    std::cout << "========================================================\n\n";

    // 1. Setup Backend, Session, Policy, Freeze, and Agent Service
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

    auto rpc_call = [&](const std::string& method, const std::string& params_json, const std::string& id = "1") -> std::string {
        std::string req = R"({"jsonrpc":"2.0","id":")" + id + 
                          R"(","method":")" + method + R"(","params":)" + params_json + "}";
        auto res = agent->handle(req);
        assert(res.ok());
        return res.value();
    };

    // 2. Inspect Target
    std::cout << "[*] Dispatching RPC 'inspect'...\n";
    std::string inspect_reply = rpc_call("inspect", "{}", "1");
    std::cout << "    Reply: " << inspect_reply << "\n";
    assert(inspect_reply.find("\"pid\":4242") != std::string::npos);

    // 3. Read Memory via JSON-RPC
    const gtlibcpp::Address health_addr = gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kHealthOffset;
    std::cout << "[*] Dispatching RPC 'read'...\n";
    std::string read_reply = rpc_call("read", R"({"address":)" + std::to_string(health_addr) + R"(,"size":4})", "2");
    std::cout << "    Reply: " << read_reply << "\n";
    assert(read_reply.find("\"hex\"") != std::string::npos);

    // 4. Preview Mutation (Dry Run)
    std::cout << "[*] Dispatching RPC 'preview' for godmode health (999)...\n";
    std::string preview_req = R"({
        "request_id":"op-e2e-1","target_alias":"CyberAssault","capability":"write",
        "address":)" + std::to_string(health_addr) + R"(,"size":4,"expected_current_hash":"40e736c02a102a050e1555781b4171020a4279adaa7ed9ca3cc9633a0ade9c37",
        "value_hex":"e7030000","value_repr":"uint32 999"})";
    std::string preview_reply = rpc_call("preview", preview_req, "3");
    std::cout << "    Reply: " << preview_reply << "\n";
    assert(preview_reply.find("\"error\"") == std::string::npos);

    // Verify memory was NOT altered during preview
    auto actual_val = session->read<std::uint32_t>(health_addr);
    assert(actual_val && actual_val.value() == 100);

    // 5. Apply Mutation
    std::cout << "[*] Dispatching RPC 'apply'...\n";
    std::string apply_req = R"({
        "request_id":"op-e2e-1","target_alias":"CyberAssault","capability":"write",
        "address":)" + std::to_string(health_addr) + R"(,"size":4,"expected_current_hash":"40e736c02a102a050e1555781b4171020a4279adaa7ed9ca3cc9633a0ade9c37",
        "approved":true,"value_hex":"e7030000","value_repr":"uint32 999"})";
    std::string apply_reply = rpc_call("apply", apply_req, "4");
    std::cout << "    Reply: " << apply_reply << "\n";
    assert(apply_reply.find("\"error\"") == std::string::npos);

    // Verify memory is now modified
    actual_val = session->read<std::uint32_t>(health_addr);
    assert(actual_val && actual_val.value() == 999);
    std::cout << "[+] Memory mutated and verified in game: " << actual_val.value() << "\n";

    // 6. Freeze via JSON-RPC
    std::cout << "[*] Dispatching RPC 'freeze'...\n";
    std::string freeze_preview = rpc_call("preview", R"({
        "request_id":"op-freeze-1","target_alias":"CyberAssault","capability":"freeze",
        "address":)" + std::to_string(health_addr) + R"(,"size":4,
        "expected_current_hash":"d8c85b9b0590a3ea8618fca78dd2451ac34658cdbb9bf2bb065564e92260df9d","value_repr":"uint32 999",
        "id":"agent_freeze_1","type":"uint32","value":999,"interval_ms":20})", "5p");
    assert(freeze_preview.find("\"error\"") == std::string::npos);
    std::string freeze_reply = rpc_call("freeze", R"({
        "request_id":"op-freeze-1","target_alias":"CyberAssault","capability":"freeze",
        "approved":true,"expected_current_hash":"d8c85b9b0590a3ea8618fca78dd2451ac34658cdbb9bf2bb065564e92260df9d","value_repr":"uint32 999",
        "id":"agent_freeze_1","address":)" + std::to_string(health_addr) + R"(,
        "size":4,"type":"uint32","value":999,"interval_ms":20})", "5");
    std::cout << "    Reply: " << freeze_reply << "\n";
    assert(freeze_reply.find("\"error\"") == std::string::npos);

    // 7. Query Status via JSON-RPC
    std::string status_reply = rpc_call("status", R"({"id":"agent_freeze_1"})", "6");
    std::cout << "    Status Reply: " << status_reply << "\n";
    assert(status_reply.find("\"error\"") == std::string::npos);

    // 8. Unfreeze via JSON-RPC
    std::string unfreeze_reply = rpc_call("unfreeze", R"({"id":"agent_freeze_1"})", "7");
    std::cout << "    Unfreeze Reply: " << unfreeze_reply << "\n";
    assert(unfreeze_reply.find("\"error\"") == std::string::npos);

    // 9. Emergency Kill Switch
    std::cout << "[*] Dispatching RPC 'kill_switch' engage...\n";
    std::string kill_reply = rpc_call("kill_switch", R"({"engage":true,"reason":"Safety trip"})", "8");
    std::cout << "    KillSwitch Reply: " << kill_reply << "\n";
    assert(kill_reply.find("\"error\"") == std::string::npos);

    // 10. Verify subsequent apply is blocked by kill switch
    std::string blocked_reply = rpc_call("apply", apply_req, "9");
    std::cout << "    Blocked Mutation Reply: " << blocked_reply << "\n";
    assert(blocked_reply.find("\"error\"") != std::string::npos);

    // 11. Test Bidirectional In-Process Transport Loop
    auto transport_pair = std::make_shared<gtlibcpp::InProcTransportPair>();
    auto client = transport_pair->make_client();
    agent->serve(transport_pair->make_server());

    auto sent = client->send(R"({"jsonrpc":"2.0","id":"10","method":"inspect","params":{}})");
    if (!sent) {
        std::cerr << "Could not send agent inspection request\n";
        transport_pair->close();
        agent->stop();
        return 1;
    }
    auto stream_reply = client->receive();
    for (int attempt = 0; !stream_reply && attempt < 50; ++attempt) {
        // InProcTransport::receive may return an ordinary 50ms timeout.
        stream_reply = client->receive();
    }
    if (!stream_reply || stream_reply.value().find("\"result\"") == std::string::npos) {
        std::cerr << "Agent transport did not return a JSON-RPC response\n";
        transport_pair->close();
        agent->stop();
        return 1;
    }
    std::cout << "[+] Stream Transport Reply: " << stream_reply.value() << "\n";
    agent->stop();

    freeze_mgr->cancel_all();
    std::cout << "\n[SUCCESS] E2E Agent JSON-RPC Demo completed flawlessly!\n\n";
    return 0;
}
