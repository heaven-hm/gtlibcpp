// Core regression tests
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/policy.hpp"
#include "gtlibcpp_test.hpp"

namespace {

class FakeMemoryBackend final : public gtlibcpp::IMemoryBackend {
public:
    gtlibcpp::Result<std::vector<std::uint8_t>>
    read(gtlibcpp::Address address, std::size_t size) override {
        if (fail_reads) {
            return gtlibcpp::Result<std::vector<std::uint8_t>>::failure(
                gtlibcpp::make_error(gtlibcpp::ErrorCode::read_failed,
                                     "fake read failure",
                                     "fake.read", address, size));
        }
        std::vector<std::uint8_t> bytes(size, 0);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = memory.find(address + i);
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

    gtlibcpp::Result<std::size_t>
    write(gtlibcpp::Address address, const std::vector<std::uint8_t>& bytes) override {
        if (writes_before_failure == 0) {
            return gtlibcpp::Result<std::size_t>::failure(
                gtlibcpp::make_error(gtlibcpp::ErrorCode::write_failed,
                                     "fake write failure",
                                     "fake.write", address, bytes.size()));
        }
        if (writes_before_failure > 0) --writes_before_failure;
        for (std::size_t i = 0; i < bytes.size(); ++i) memory[address + i] = bytes[i];
        return gtlibcpp::Result<std::size_t>::success(bytes.size());
    }

    bool is_alive() noexcept override { return alive.load(); }
    std::string target_id() const override { return "fake-target"; }
    gtlibcpp::TargetIdentity identity() const override { return identity_; }

    void put(gtlibcpp::Address a, const void* data, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) memory[a + i] = b[i];
    }

    std::map<gtlibcpp::Address, std::uint8_t> memory;
    int writes_before_failure{-1};
    bool fail_reads{false};
    std::atomic<bool> alive{true};
    gtlibcpp::TargetIdentity identity_{
        1, 1, "C:/fixtures/fixture.exe", "sha256:fixture",
        gtlibcpp::Architecture::x64
    };
};

GTLIBCPP_TEST(read_and_write_are_exact) {
    auto backend = std::make_shared<FakeMemoryBackend>();
    gtlibcpp::MemorySession session(backend);
    const std::uint32_t expected = 0xAABBCCDD;
    backend->put(0x1000, &expected, sizeof(expected));

    const auto read = session.read<std::uint32_t>(0x1000);
    GTLIBCPP_REQUIRE(read);
    GTLIBCPP_REQUIRE_EQ(read.value(), expected);

    const std::uint32_t replacement = 0x11223344;
    const auto write = session.write<std::uint32_t>(0x1000, replacement);
    GTLIBCPP_REQUIRE(write);
    GTLIBCPP_REQUIRE_EQ(write.value(), sizeof(replacement));
    GTLIBCPP_REQUIRE_EQ(session.read<std::uint32_t>(0x1000).value(), replacement);
}

GTLIBCPP_TEST(read_failure_is_not_a_default_value) {
    auto backend = std::make_shared<FakeMemoryBackend>();
    backend->fail_reads = true;
    gtlibcpp::MemorySession session(backend);
    const auto read = session.read<std::uint32_t>(0x2000);
    GTLIBCPP_REQUIRE(!read);
    GTLIBCPP_REQUIRE_EQ(read.error().code, gtlibcpp::ErrorCode::read_failed);
    GTLIBCPP_REQUIRE_EQ(read.error().address, 0x2000u);
}

GTLIBCPP_TEST(partial_write_is_reported) {
    auto backend = std::make_shared<FakeMemoryBackend>();
    backend->writes_before_failure = 1;
    gtlibcpp::MemorySession session(backend);
    const std::uint32_t value = 99;
    const auto batch = session.write_offsets<std::uint32_t>(0x3000, {0, 4, 8}, value);
    GTLIBCPP_REQUIRE(batch);
    GTLIBCPP_REQUIRE_FALSE(batch.value().complete);
    GTLIBCPP_REQUIRE_EQ(batch.value().completed_addresses.size(), 1u);
    GTLIBCPP_REQUIRE(batch.value().failure.has_value());
    GTLIBCPP_REQUIRE_EQ(batch.value().failure->address, 0x3004u);
}

GTLIBCPP_TEST(pointer_chain_returns_final_address) {
    auto backend = std::make_shared<FakeMemoryBackend>();
    gtlibcpp::MemorySession session(backend);
    const gtlibcpp::Address first = 0x5000;
    const gtlibcpp::Address second = 0x6000;
    backend->put(0x4000, &first, sizeof(first));
    backend->put(0x5010, &second, sizeof(second));
    const auto resolved = session.resolve_pointer_chain(0x4000, {0, 0x10});
    GTLIBCPP_REQUIRE(resolved);
    GTLIBCPP_REQUIRE_EQ(resolved.value(), second);
}

GTLIBCPP_TEST(policy_denies_unapproved_mutation) {
    gtlibcpp::TargetManifest manifest{
        "fixture", "C:/fixtures/fixture.exe", "sha256:fixture", "x64", true
    };
    gtlibcpp::Policy policy({manifest});
    gtlibcpp::TargetIdentity identity{
        42, 1234, "C:/fixtures/fixture.exe", "sha256:fixture",
        gtlibcpp::Architecture::x64
    };
    gtlibcpp::MutationRequest request{
        "op-1", "fixture", gtlibcpp::Capability::write,
        false, false, "old-value-hash", 0x5000, 4, "uint32 0"
    };
    auto deny = policy.authorize(identity, request);
    GTLIBCPP_REQUIRE(!deny);
    GTLIBCPP_REQUIRE_EQ(deny.error().code, gtlibcpp::ErrorCode::approval_required);
    request.preview = true;
    GTLIBCPP_REQUIRE(policy.authorize(identity, request));
    request.preview = false;
    request.approved = true;
    GTLIBCPP_REQUIRE(policy.authorize(identity, request));
}

GTLIBCPP_TEST(empty_hotkey_list_is_rejected) {
    gtlibcpp::FreezeRequest fr;
    fr.id = "";
    fr.address = 0x1234;
    fr.type_name = "uint8";
    fr.value_u64 = 0;
    fr.interval = std::chrono::milliseconds(20);
    auto backend = std::make_shared<FakeMemoryBackend>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    gtlibcpp::FreezeManager freeze(session);
    auto r = freeze.freeze(fr);
    GTLIBCPP_REQUIRE(!r);
    GTLIBCPP_REQUIRE_EQ(r.error().code, gtlibcpp::ErrorCode::invalid_entry_id);
}

GTLIBCPP_TEST(addresses_are_64bit_safe) {
    auto backend = std::make_shared<FakeMemoryBackend>();
    gtlibcpp::MemorySession session(backend);
    const gtlibcpp::Address high = 0x7FFFFFFE00000000ULL;
    const std::uint64_t expected = 0x1122334455667788ULL;
    backend->put(high, &expected, sizeof(expected));
    const auto r = session.read<std::uint64_t>(high);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE_EQ(r.value(), expected);
}

GTLIBCPP_TEST(compare_write_verifies) {
    auto backend = std::make_shared<FakeMemoryBackend>();
    gtlibcpp::MemorySession session(backend);
    const std::uint32_t original = 0xDEADBEEF;
    backend->put(0x9000, &original, sizeof(original));
    const auto r = session.compare_write_verify<std::uint32_t>(0x9000, 0xCAFEF00D);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE_EQ(session.read<std::uint32_t>(0x9000).value(), 0xCAFEF00Du);
}

GTLIBCPP_TEST(freeze_restores_original) {
    auto backend = std::make_shared<FakeMemoryBackend>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    gtlibcpp::FreezeManager freeze(session);

    const std::uint32_t original = 0x01020304;
    backend->put(0xA000, &original, sizeof(original));

    gtlibcpp::FreezeRequest fr;
    fr.id = "test-freeze";
    fr.address = 0xA000;
    fr.type_name = "uint32";
    fr.value_u64 = 0x11223344;
    fr.interval = std::chrono::milliseconds(10);

    auto ok = freeze.freeze(fr);
    GTLIBCPP_REQUIRE(ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    GTLIBCPP_REQUIRE_EQ(session->read<std::uint32_t>(0xA000).value(), 0x11223344u);

    auto restored = freeze.restore("test-freeze");
    GTLIBCPP_REQUIRE(restored);
    GTLIBCPP_REQUIRE_EQ(session->read<std::uint32_t>(0xA000).value(), original);
}

GTLIBCPP_TEST(freeze_cancels_on_target_dead) {
    auto backend = std::make_shared<FakeMemoryBackend>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    gtlibcpp::FreezeManager freeze(session);
    const std::uint32_t v = 0x12345678;
    backend->put(0xB000, &v, sizeof(v));

    gtlibcpp::FreezeRequest fr;
    fr.id = "die-fast";
    fr.address = 0xB000;
    fr.type_name = "uint32";
    fr.value_u64 = 0x99;
    fr.interval = std::chrono::milliseconds(50);
    GTLIBCPP_REQUIRE(freeze.freeze(fr));

    // Allow the worker to start and write at least once. Windows
    // has a default ~15.6ms timer resolution so use a generous
    // lower bound; the upper bound is bounded by the test runner.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    backend->alive.store(false);
    // Wait long enough for the worker to notice (one full interval).
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto s = freeze.status("die-fast");
    GTLIBCPP_REQUIRE(s);
    GTLIBCPP_REQUIRE_FALSE(s.value().active);
    freeze.cancel_all();
}

GTLIBCPP_TEST(bounded_string_rejects_overflow) {
    auto backend = std::make_shared<FakeMemoryBackend>();
    gtlibcpp::MemorySession session(backend);
    std::string text = "ABCDEFGH";
    backend->put(0xC000, text.data(), text.size());

    auto need_terminator = session.read_string(0xC000, 4, true);
    GTLIBCPP_REQUIRE(!need_terminator);
    GTLIBCPP_REQUIRE_EQ(need_terminator.error().code, gtlibcpp::ErrorCode::invalid_string);

    auto ok = session.read_string(0xC000, 4, false);
    GTLIBCPP_REQUIRE(ok);
    GTLIBCPP_REQUIRE_EQ(ok.value().size(), 4u);

    auto too_small = session.write_string(0xC000, ok.value(), 2);
    GTLIBCPP_REQUIRE(!too_small);
    GTLIBCPP_REQUIRE_EQ(too_small.error().code, gtlibcpp::ErrorCode::invalid_string);
}

GTLIBCPP_TEST(sessions_do_not_share_state) {
    auto a = std::make_shared<FakeMemoryBackend>();
    auto b = std::make_shared<FakeMemoryBackend>();
    a->identity_.pid = 100;
    b->identity_.pid = 200;
    gtlibcpp::MemorySession sa(a);
    gtlibcpp::MemorySession sb(b);
    GTLIBCPP_REQUIRE_EQ(sa.identity().pid, 100u);
    GTLIBCPP_REQUIRE_EQ(sb.identity().pid, 200u);
}

GTLIBCPP_TEST(kill_switch_denies) {
    gtlibcpp::TargetManifest m{
        "fixture", "C:/fixtures/fixture.exe", "sha256:fixture", "x64", true
    };
    gtlibcpp::Policy policy({m});
    gtlibcpp::TargetIdentity id{
        1, 1, "C:/fixtures/fixture.exe", "sha256:fixture",
        gtlibcpp::Architecture::x64
    };
    gtlibcpp::MutationRequest req{
        "op-ks", "fixture", gtlibcpp::Capability::read,
        true, false, "hash", 0x100, 4, "uint32"
    };
    GTLIBCPP_REQUIRE(policy.authorize(id, req));
    policy.set_kill_switch_reason("operator pull");
    auto denied = policy.authorize(id, req);
    GTLIBCPP_REQUIRE(!denied);
    GTLIBCPP_REQUIRE_EQ(denied.error().code, gtlibcpp::ErrorCode::policy_denied);
}

GTLIBCPP_TEST(rate_limiter_blocks_burst) {
    gtlibcpp::RateLimiter limiter(2);
    GTLIBCPP_REQUIRE(limiter.allow("alias"));
    GTLIBCPP_REQUIRE(limiter.allow("alias"));
    auto third = limiter.allow("alias");
    GTLIBCPP_REQUIRE(!third);
    GTLIBCPP_REQUIRE_EQ(third.error().code, gtlibcpp::ErrorCode::rate_limited);
}

GTLIBCPP_TEST(freeze_interval_honours_short_values) {
    // Regression for the "interval_ms < 50 mis-computed" bug. With
    // a short interval the worker should write multiple times in
    // the window. The lower bound is deliberately generous so the
    // test passes on platforms with a coarse scheduler tick
    // (Windows default timer resolution is ~15.6ms).
    auto backend = std::make_shared<FakeMemoryBackend>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    gtlibcpp::FreezeManager freeze(session);
    const std::uint32_t original = 0;
    backend->put(0xD000, &original, sizeof(original));
    gtlibcpp::FreezeRequest fr;
    fr.id = "short-interval";
    fr.address = 0xD000;
    fr.type_name = "uint32";
    fr.value_u64 = 0xAA;
    fr.interval = std::chrono::milliseconds(20);
    GTLIBCPP_REQUIRE(freeze.freeze(fr));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto s = freeze.status("short-interval");
    GTLIBCPP_REQUIRE(s);
    GTLIBCPP_REQUIRE(s.value().successful_rewrites >= 2u);
    freeze.cancel_all();
}

} // namespace

int main() {
    return GTLIBCPP_RUN_ALL();
}
