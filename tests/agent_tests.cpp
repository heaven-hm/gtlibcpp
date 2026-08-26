// Agent-service regression tests. Drives the JSON-RPC surface end to
// end through the in-process transport so the test can verify the
// protocol contract that the named-pipe transport will inherit.
#include <cassert>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gtlibcpp/agent.hpp"
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/inproc_transport.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/parser.hpp"
#include "gtlibcpp/policy.hpp"

namespace {

class FakeBackend final : public gtlibcpp::IMemoryBackend {
public:
    gtlibcpp::Result<std::vector<std::uint8_t>> read(gtlibcpp::Address address, std::size_t size) override {
        if (fail_reads) {
            return gtlibcpp::Result<std::vector<std::uint8_t>>::failure(
                gtlibcpp::make_error(gtlibcpp::ErrorCode::read_failed, "fake read fail",
                                     "fake.read", address, size));
        }
        std::vector<std::uint8_t> bytes(size, 0);
        for (std::size_t i = 0; i < size; ++i) {
            auto it = memory.find(address + i);
            if (it == memory.end()) {
                return gtlibcpp::Result<std::vector<std::uint8_t>>::failure(
                    gtlibcpp::make_error(gtlibcpp::ErrorCode::read_failed,
                                         "address not mapped",
                                         "fake.read", address, size));
            }
            bytes[i] = it->second;
        }
        return gtlibcpp::Result<std::vector<std::uint8_t>>::success(std::move(bytes));
    }
    gtlibcpp::Result<std::size_t> write(gtlibcpp::Address address, const std::vector<std::uint8_t>& bytes) override {
        if (writes_before_failure == 0) {
            return gtlibcpp::Result<std::size_t>::failure(
                gtlibcpp::make_error(gtlibcpp::ErrorCode::write_failed, "fake write fail",
                                     "fake.write", address, bytes.size()));
        }
        if (writes_before_failure > 0) --writes_before_failure;
        for (std::size_t i = 0; i < bytes.size(); ++i) memory[address + i] = bytes[i];
        return gtlibcpp::Result<std::size_t>::success(bytes.size());
    }
    bool is_alive() noexcept override { return alive; }
    std::string target_id() const override { return "fake-target"; }
    gtlibcpp::TargetIdentity identity() const override { return identity_; }

    void put(gtlibcpp::Address a, const void* data, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) memory[a + i] = b[i];
    }

    std::map<gtlibcpp::Address, std::uint8_t> memory;
    int writes_before_failure{-1};
    bool fail_reads{false};
    bool alive{true};
    gtlibcpp::TargetIdentity identity_{
        4242, 1, "C:/fixtures/fixture.exe", "sha256:fixture", gtlibcpp::Architecture::x64
    };
};

std::shared_ptr<gtlibcpp::AgentService>
build_service(std::shared_ptr<FakeBackend>& backend) {
    backend = std::make_shared<FakeBackend>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    auto parser  = std::make_shared<gtlibcpp::CheatTableParser>();
    auto freeze  = std::make_shared<gtlibcpp::FreezeManager>(session);
    std::vector<gtlibcpp::TargetManifest> manifests;
    manifests.push_back(gtlibcpp::TargetManifest{
        "fixture",
        "C:/fixtures/fixture.exe",
        "sha256:fixture",
        "x64",
        true,
    });
    auto policy = std::make_shared<gtlibcpp::Policy>(std::move(manifests));
    return std::make_shared<gtlibcpp::AgentService>(session, policy, freeze, parser);
}

void test_inspect_reports_target_identity() {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    auto r = svc->handle(R"({"jsonrpc":"2.0","id":"1","method":"inspect","params":{}})");
    assert(r.ok());
    auto j = gtlibcpp::json_decode(r.value());
    assert(j.ok());
    const auto* result = gtlibcpp::json_get(j.value(), "result");
    assert(result);
    const auto* target = gtlibcpp::json_get(*result, "target");
    assert(target);
    assert(gtlibcpp::json_get_int(*target, "pid") == 4242);
    assert(gtlibcpp::json_get_string(*target, "image_path") == "C:/fixtures/fixture.exe");
    std::cout << "test_inspect_reports_target_identity passed\n";
}

void test_read_failure_is_returned_not_swallowed() {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    backend->fail_reads = true;
    auto r = svc->handle(R"({"jsonrpc":"2.0","id":"2","method":"read","params":{"address":4096,"size":4}})");
    assert(r.ok());
    auto j = gtlibcpp::json_decode(r.value());
    assert(j.ok());
    const auto* err = gtlibcpp::json_get(j.value(), "error");
    assert(err);
    assert(gtlibcpp::json_get_int(*err, "code") == static_cast<std::int64_t>(gtlibcpp::ErrorCode::read_failed));
    std::cout << "test_read_failure_is_returned_not_swallowed passed\n";
}

void test_preview_then_apply_round_trip() {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    const std::string preview = R"({"jsonrpc":"2.0","id":"3","method":"preview","params":{
        "request_id":"op-1",
        "target_alias":"fixture",
        "capability":"write",
        "address":4096,
        "size":4,
        "expected_current_hash":"abc",
        "value_repr":"uint32 0x11223344"
    }})";
    auto pr = svc->handle(preview);
    assert(pr.ok());
    auto pj = gtlibcpp::json_decode(pr.value());
    assert(pj.ok());
    const auto* perr = gtlibcpp::json_get(pj.value(), "error");
    assert(!perr);
    const std::string apply = R"({"jsonrpc":"2.0","id":"4","method":"apply","params":{
        "request_id":"op-1",
        "target_alias":"fixture",
        "capability":"write",
        "address":4096,
        "size":4,
        "expected_current_hash":"abc",
        "approved":true,
        "value_hex":"11223344",
        "value_repr":"uint32 0x11223344"
    }})";
    auto ar = svc->handle(apply);
    assert(ar.ok());
    auto aj = gtlibcpp::json_decode(ar.value());
    assert(aj.ok());
    const auto* aerr = gtlibcpp::json_get(aj.value(), "error");
    assert(!aerr);
    std::cout << "test_preview_then_apply_round_trip passed\n";
}

void test_apply_without_preview_is_denied() {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    const std::string apply = R"({"jsonrpc":"2.0","id":"5","method":"apply","params":{
        "request_id":"op-2",
        "target_alias":"fixture",
        "capability":"write",
        "address":4096,
        "size":4,
        "expected_current_hash":"abc",
        "approved":true,
        "value_hex":"01020304",
        "value_repr":"uint32 1"
    }})";
    auto r = svc->handle(apply);
    assert(r.ok());
    auto j = gtlibcpp::json_decode(r.value());
    const auto* err = gtlibcpp::json_get(j.value(), "error");
    assert(err);
    std::cout << "test_apply_without_preview_is_denied passed\n";
}

void test_kill_switch_engages_and_blocks() {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    const std::string engage = R"({"jsonrpc":"2.0","id":"6","method":"kill_switch","params":{"engage":true,"reason":"unit test"}})";
    auto r = svc->handle(engage);
    assert(r.ok());
    const std::string apply = R"({"jsonrpc":"2.0","id":"7","method":"apply","params":{
        "request_id":"op-k",
        "target_alias":"fixture",
        "capability":"write",
        "address":4096,
        "size":4,
        "expected_current_hash":"abc",
        "approved":true,
        "value_hex":"AABBCCDD",
        "value_repr":"uint32"
    }})";
    // No preview first; the apply should be denied by the kill switch.
    auto denied = svc->handle(apply);
    assert(denied.ok());
    auto dj = gtlibcpp::json_decode(denied.value());
    const auto* err = gtlibcpp::json_get(dj.value(), "error");
    assert(err);
    std::cout << "test_kill_switch_engages_and_blocks passed\n";
}

void test_unknown_target_is_denied() {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    const std::string preview = R"({"jsonrpc":"2.0","id":"8","method":"preview","params":{
        "request_id":"op-z",
        "target_alias":"not-listed",
        "capability":"write",
        "address":4096,
        "size":4,
        "expected_current_hash":"abc",
        "value_repr":"uint32"
    }})";
    auto r = svc->handle(preview);
    auto j = gtlibcpp::json_decode(r.value());
    const auto* err = gtlibcpp::json_get(j.value(), "error");
    assert(err);
    std::cout << "test_unknown_target_is_denied passed\n";
}

void test_freeze_and_status_round_trip() {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    const std::string freeze = R"({"jsonrpc":"2.0","id":"9","method":"freeze","params":{
        "id":"freeze-1",
        "address":8192,
        "type":"uint32",
        "value":99,
        "interval_ms":20
    }})";
    auto fr = svc->handle(freeze);
    assert(fr.ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    const std::string status = R"({"jsonrpc":"2.0","id":"10","method":"status","params":{"id":"freeze-1"}})";
    auto sr = svc->handle(status);
    assert(sr.ok());
    const std::string unfreeze = R"({"jsonrpc":"2.0","id":"11","method":"unfreeze","params":{"id":"freeze-1"}})";
    auto ur = svc->handle(unfreeze);
    assert(ur.ok());
    std::cout << "test_freeze_and_status_round_trip passed\n";
}

void test_transport_round_trip() {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    auto pair = std::make_shared<gtlibcpp::InProcTransportPair>();
    auto client = pair->make_client();
    svc->serve(pair->make_server());
    (void)client->send(R"({"jsonrpc":"2.0","id":"1","method":"inspect","params":{}})");
    auto r = client->receive();
    assert(r.ok());
    assert(r.value().find("\"result\"") != std::string::npos);
    svc->stop();
    std::cout << "test_transport_round_trip passed\n";
}

} // namespace

int main() {
    test_inspect_reports_target_identity();
    test_read_failure_is_returned_not_swallowed();
    test_preview_then_apply_round_trip();
    test_apply_without_preview_is_denied();
    test_kill_switch_engages_and_blocks();
    test_unknown_target_is_denied();
    test_freeze_and_status_round_trip();
    test_transport_round_trip();
    std::cout << "gtlibcpp agent tests: all passed\n";
    return 0;
}
