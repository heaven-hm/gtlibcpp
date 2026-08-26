/*
 * IMemoryBackend — the abstract I/O surface used by MemorySession, freeze
 * workers, and the parser resolver. A production deployment supplies a
 * Windows backend (ReadProcessMemory / WriteProcessMemory on an opened
 * handle); tests and offline tools supply a fake that records calls and
 * can simulate failure.
 *
 * Every method returns a Result so the failure model is identical to the
 * rest of the library. The backend is responsible for translating
 * GetLastError() / errno into the system_error field of the returned
 * Error; the higher layers never have to know which OS they are on.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "result.hpp"
#include "types.hpp"

namespace gtlibcpp {

// Free-function helpers that match the read/write sizes and ranges a real
// memory backend is allowed to service. Centralised so every backend (and
// the test fake) applies the same range checks; the issue requires
// "validate all addresses, offsets, sizes, type conversions".
[[nodiscard]] inline Result<std::size_t>
validate_read_request(Address address, std::size_t size) {
    if (size == 0) {
        return Result<std::size_t>::failure(make_error(
            ErrorCode::invalid_size, "read size must be non-zero",
            "validate_read_request", address, 0));
    }
    if (size > (1u << 20)) {
        return Result<std::size_t>::failure(make_error(
            ErrorCode::invalid_size, "read size exceeds 1 MiB safety cap",
            "validate_read_request", address, size));
    }
    if (address + size < address) {
        return Result<std::size_t>::failure(make_error(
            ErrorCode::invalid_address, "read range wraps the address space",
            "validate_read_request", address, size));
    }
    return Result<std::size_t>::success(size);
}

[[nodiscard]] inline Result<std::size_t>
validate_write_request(Address address, std::size_t size) {
    if (size == 0) {
        return Result<std::size_t>::failure(make_error(
            ErrorCode::invalid_size, "write size must be non-zero",
            "validate_write_request", address, 0));
    }
    if (size > (1u << 20)) {
        return Result<std::size_t>::failure(make_error(
            ErrorCode::invalid_size, "write size exceeds 1 MiB safety cap",
            "validate_write_request", address, size));
    }
    if (address + size < address) {
        return Result<std::size_t>::failure(make_error(
            ErrorCode::invalid_address, "write range wraps the address space",
            "validate_write_request", address, size));
    }
    return Result<std::size_t>::success(size);
}

class IMemoryBackend {
public:
    virtual ~IMemoryBackend() = default;

    // Read `size` bytes from `address`. The returned vector is exactly
    // `size` bytes on success; on failure the Error carries the
    // underlying system error code and the partial byte count.
    [[nodiscard]] virtual Result<std::vector<std::uint8_t>>
    read(Address address, std::size_t size) = 0;

    // Write `bytes` to `address`. Returns the number of bytes actually
    // written. The Windows backend is required to detect partial writes
    // (where WriteProcessMemory succeeded but bytesWritten < size), which
    // the legacy code at GTLibc.cpp:912-940 ignored.
    [[nodiscard]] virtual Result<std::size_t>
    write(Address address, const std::vector<std::uint8_t>& bytes) = 0;

    // True while the target process is alive. The Windows backend polls
    // process state; the fake lets tests simulate a crash mid-freeze.
    [[nodiscard]] virtual bool is_alive() noexcept = 0;

    // Stable identifier for the bound target. Used by the policy layer
    // and the audit log; never a raw pointer.
    [[nodiscard]] virtual std::string target_id() const = 0;

    // Underlying target identity (pid / path / start time / architecture).
    // Backends should populate this on attach and keep it stable for the
    // lifetime of the session.
    [[nodiscard]] virtual TargetIdentity identity() const = 0;
};

using MemoryBackendPtr = std::shared_ptr<IMemoryBackend>;

} // namespace gtlibcpp
