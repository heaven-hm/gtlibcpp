// Agent-service regression tests
#include <chrono>
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
#include "gtlibcpp_test.hpp"

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

} // namespace

GTLIBCPP_TEST(inspect_reports_target_identity) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    auto r = svc->handle(R"({"jsonrpc":"2.0","id":"1","method":"inspect","params":{}})");
    GTLIBCPP_REQUIRE(r);
    auto j = gtlibcpp::json_decode(r.value());
    GTLIBCPP_REQUIRE(j);
    const auto* result = gtlibcpp::json_get(j.value(), "result");
    GTLIBCPP_REQUIRE(result);
    const auto* target = gtlibcpp::json_get(*result, "target");
    GTLIBCPP_REQUIRE(target);
    GTLIBCPP_REQUIRE_EQ(gtlibcpp::json_get_int(*target, "pid"), 4242);
    GTLIBCPP_REQUIRE_EQ(gtlibcpp::json_get_string(*target, "image_path"),
                       std::string("C:/fixtures/fixture.exe"));
}

GTLIBCPP_TEST(read_failure_is_returned_not_swallowed) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    backend->fail_reads = true;
    auto r = svc->handle(R"({"jsonrpc":"2.0","id":"2","method":"read","params":{"address":4096,"size":4}})");
    GTLIBCPP_REQUIRE(r);
    auto j = gtlibcpp::json_decode(r.value());
    GTLIBCPP_REQUIRE(j);
    const auto* err = gtlibcpp::json_get(j.value(), "error");
    GTLIBCPP_REQUIRE(err);
    GTLIBCPP_REQUIRE_EQ(gtlibcpp::json_get_int(*err, "code"),
                       static_cast<std::int64_t>(gtlibcpp::ErrorCode::read_failed));
}

GTLIBCPP_TEST(preview_then_apply_round_trip) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    const std::string preview = R"({"jsonrpc":"2.0","id":"3","method":"preview","params":{
        "request_id":"op-1","target_alias":"fixture","capability":"write",
        "address":4096,"size":4,"expected_current_hash":"abc",
        "value_repr":"uint32 0x11223344"}})";
    auto pr = svc->handle(preview);
    GTLIBCPP_REQUIRE(pr);
    auto pj = gtlibcpp::json_decode(pr.value());
    GTLIBCPP_REQUIRE(pj);
    GTLIBCPP_REQUIRE(!gtlibcpp::json_get(pj.value(), "error"));
    const std::string apply = R"({"jsonrpc":"2.0","id":"4","method":"apply","params":{
        "request_id":"op-1","target_alias":"fixture","capability":"write",
        "address":4096,"size":4,"expected_current_hash":"abc","approved":true,
        "value_hex":"11223344","value_repr":"uint32 0x11223344"}})";
    auto ar = svc->handle(apply);
    GTLIBCPP_REQUIRE(ar);
    auto aj = gtlibcpp::json_decode(ar.value());
    GTLIBCPP_REQUIRE(aj);
    GTLIBCPP_REQUIRE(!gtlibcpp::json_get(aj.value(), "error"));
}

GTLIBCPP_TEST(apply_without_preview_is_denied) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    const std::string apply = R"({"jsonrpc":"2.0","id":"5","method":"apply","params":{
        "request_id":"op-2","target_alias":"fixture","capability":"write",
        "address":4096,"size":4,"expected_current_hash":"abc","approved":true,
        "value_hex":"01020304","value_repr":"uint32 1"}})";
    auto r = svc->handle(apply);
    GTLIBCPP_REQUIRE(r);
    auto j = gtlibcpp::json_decode(r.value());
    GTLIBCPP_REQUIRE(gtlibcpp::json_get(j.value(), "error"));
}

GTLIBCPP_TEST(kill_switch_engages_and_blocks) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    auto r = svc->handle(R"({"jsonrpc":"2.0","id":"6","method":"kill_switch","params":{"engage":true,"reason":"unit test"}})");
    GTLIBCPP_REQUIRE(r);
    const std::string apply = R"({"jsonrpc":"2.0","id":"7","method":"apply","params":{
        "request_id":"op-k","target_alias":"fixture","capability":"write",
        "address":4096,"size":4,"expected_current_hash":"abc","approved":true,
        "value_hex":"AABBCCDD","value_repr":"uint32"}})";
    auto denied = svc->handle(apply);
    GTLIBCPP_REQUIRE(denied);
    auto dj = gtlibcpp::json_decode(denied.value());
    GTLIBCPP_REQUIRE(gtlibcpp::json_get(dj.value(), "error"));
}

GTLIBCPP_TEST(unknown_target_is_denied) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    auto r = svc->handle(R"({"jsonrpc":"2.0","id":"8","method":"preview","params":{
        "request_id":"op-z","target_alias":"not-listed","capability":"write",
        "address":4096,"size":4,"expected_current_hash":"abc","value_repr":"uint32"}})");
    auto j = gtlibcpp::json_decode(r.value());
    GTLIBCPP_REQUIRE(gtlibcpp::json_get(j.value(), "error"));
}

GTLIBCPP_TEST(freeze_and_status_round_trip) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    auto fr = svc->handle(R"({"jsonrpc":"2.0","id":"9","method":"freeze","params":{
        "id":"freeze-1","address":8192,"type":"uint32","value":99,"interval_ms":20}})");
    GTLIBCPP_REQUIRE(fr);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    auto sr = svc->handle(R"({"jsonrpc":"2.0","id":"10","method":"status","params":{"id":"freeze-1"}})");
    GTLIBCPP_REQUIRE(sr);
    auto ur = svc->handle(R"({"jsonrpc":"2.0","id":"11","method":"unfreeze","params":{"id":"freeze-1"}})");
    GTLIBCPP_REQUIRE(ur);
}

GTLIBCPP_TEST(transport_round_trip) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    auto pair = std::make_shared<gtlibcpp::InProcTransportPair>();
    auto client = pair->make_client();
    svc->serve(pair->make_server());
    (void)client->send(R"({"jsonrpc":"2.0","id":"1","method":"inspect","params":{}})");
    auto r = client->receive();
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE(r.value().find("\"result\"") != std::string::npos);
    svc->stop();
}

GTLIBCPP_TEST(unknown_method_returns_rpc_error) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    auto r = svc->handle(R"({"jsonrpc":"2.0","id":"42","method":"nope","params":{}})");
    GTLIBCPP_REQUIRE(r);
    auto j = gtlibcpp::json_decode(r.value());
    GTLIBCPP_REQUIRE(j);
    const auto* err = gtlibcpp::json_get(j.value(), "error");
    GTLIBCPP_REQUIRE(err);
    GTLIBCPP_REQUIRE_EQ(gtlibcpp::json_get_int(*err, "code"), -32601);
}

GTLIBCPP_TEST(malformed_json_returns_parse_error) {
    std::shared_ptr<FakeBackend> backend;
    auto svc = build_service(backend);
    auto r = svc->handle("not json at all");
    GTLIBCPP_REQUIRE(r);
    auto j = gtlibcpp::json_decode(r.value());
    GTLIBCPP_REQUIRE(j);
    const auto* err = gtlibcpp::json_get(j.value(), "error");
    GTLIBCPP_REQUIRE(err);
    GTLIBCPP_REQUIRE_EQ(gtlibcpp::json_get_int(*err, "code"), -32700);
}

int main() {
    return GTLIBCPP_RUN_ALL();
}
