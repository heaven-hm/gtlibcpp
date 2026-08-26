#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/policy.hpp"

namespace {

class FakeMemoryBackend final : public gtlibcpp::IMemoryBackend {
public:
    gtlibcpp::Result<std::vector<std::uint8_t>> read(gtlibcpp::Address address, std::size_t size) override {
        if (fail_reads) {
            return gtlibcpp::Result<std::vector<std::uint8_t>>::failure(
                gtlibcpp::make_error(gtlibcpp::ErrorCode::read_failed, "fake read failure", "fake.read", address, size));
        }

        std::vector<std::uint8_t> bytes(size, 0);
        for (std::size_t index = 0; index < size; ++index) {
            const auto it = memory.find(address + index);
            if (it == memory.end()) {
                return gtlibcpp::Result<std::vector<std::uint8_t>>::failure(
                    gtlibcpp::make_error(gtlibcpp::ErrorCode::read_failed, "address not mapped", "fake.read", address, size));
            }
            bytes[index] = it->second;
        }
        return gtlibcpp::Result<std::vector<std::uint8_t>>::success(std::move(bytes));
    }

    gtlibcpp::Result<std::size_t> write(gtlibcpp::Address address, const std::vector<std::uint8_t> &bytes) override {
        if (writes_before_failure == 0) {
            return gtlibcpp::Result<std::size_t>::failure(
                gtlibcpp::make_error(gtlibcpp::ErrorCode::write_failed, "fake write failure", "fake.write", address, bytes.size()));
        }
        if (writes_before_failure > 0) {
            --writes_before_failure;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            memory[address + index] = bytes[index];
        }
        return gtlibcpp::Result<std::size_t>::success(bytes.size());
    }

    bool is_alive() const noexcept override { return alive; }
    std::string target_id() const override { return "fake-target"; }

    void put(gtlibcpp::Address address, const void *data, std::size_t size) {
        const auto *bytes = static_cast<const std::uint8_t *>(data);
        for (std::size_t index = 0; index < size; ++index) {
            memory[address + index] = bytes[index];
        }
    }

    std::map<gtlibcpp::Address, std::uint8_t> memory;
    int writes_before_failure{-1};
    bool fail_reads{false};
    bool alive{true};
};

void test_read_and_write_are_exact() {
    auto backend = std::make_shared<FakeMemoryBackend>();
    gtlibcpp::MemorySession session(backend);
    const std::uint32_t expected = 0xAABBCCDD;
    backend->put(0x1000, &expected, sizeof(expected));

    const auto read = session.read<std::uint32_t>(0x1000);
    assert(read);
    assert(read.value() == expected);

    const std::uint32_t replacement = 0x11223344;
    const auto write = session.write<std::uint32_t>(0x1000, replacement);
    assert(write);
    assert(write.value() == sizeof(replacement));
    assert(session.read<std::uint32_t>(0x1000).value() == replacement);
}

void test_read_failure_is_not_a_default_value() {
    auto backend = std::make_shared<FakeMemoryBackend>();
    backend->fail_reads = true;
    gtlibcpp::MemorySession session(backend);

    const auto read = session.read<std::uint32_t>(0x2000);
    assert(!read);
    assert(read.error().code == gtlibcpp::ErrorCode::read_failed);
    assert(read.error().address == 0x2000);
}

void test_partial_write_is_reported() {
    auto backend = std::make_shared<FakeMemoryBackend>();
    backend->writes_before_failure = 1;
    gtlibcpp::MemorySession session(backend);
    const std::uint32_t value = 99;

    const auto batch = session.write_offsets<std::uint32_t>(0x3000, {0, 4, 8}, value);
    assert(batch);
    assert(!batch.value().complete);
    assert(batch.value().completed_addresses.size() == 1);
    assert(batch.value().failure.has_value());
    assert(batch.value().failure->address == 0x3004);
}

void test_pointer_chain_returns_final_address() {
    auto backend = std::make_shared<FakeMemoryBackend>();
    gtlibcpp::MemorySession session(backend);
    const gtlibcpp::Address first = 0x5000;
    const gtlibcpp::Address second = 0x6000;
    backend->put(0x4000, &first, sizeof(first));
    backend->put(0x5010, &second, sizeof(second));

    const auto resolved = session.resolve_pointer_chain(0x4000, {0, 0x10});
    assert(resolved);
    assert(resolved.value() == second);
}

void test_policy_denies_unapproved_mutation() {
    gtlibcpp::TargetManifest manifest{
        "fixture",
        "C:/fixtures/fixture.exe",
        "sha256:fixture",
        "x64",
        true,
    };
    gtlibcpp::Policy policy({manifest});
    gtlibcpp::TargetIdentity identity{
        42,
        1234,
        "C:/fixtures/fixture.exe",
        "sha256:fixture",
        "x64",
    };

    gtlibcpp::MutationRequest request{
        "op-1",
        "fixture",
        gtlibcpp::Capability::write,
        false,
        false,
        "old-value-hash",
    };
    assert(!policy.authorize(identity, request));
    assert(policy.authorize(identity, request).error().code == gtlibcpp::ErrorCode::approval_required);

    request.preview = true;
    assert(policy.authorize(identity, request));

    request.preview = false;
    request.approved = true;
    assert(policy.authorize(identity, request));
}

} // namespace

int main() {
    test_read_and_write_are_exact();
    test_read_failure_is_not_a_default_value();
    test_partial_write_is_reported();
    test_pointer_chain_returns_final_address();
    test_policy_denies_unapproved_mutation();
    std::cout << "gtlibcpp core tests: 5 passed\n";
}
