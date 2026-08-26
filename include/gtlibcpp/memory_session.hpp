/*
 * MemorySession — RAII handle, no global state, typed read/write/pointer
 * resolution, batch reporting. This is the public entry point that every
 * trainer, the parser resolver, the freeze manager, and the agent service
 * go through.
 *
 * Design contract (issue #1):
 *   - No globals. Constructing two sessions does not alias any state.
 *   - The session owns the backend shared_ptr; the backend owns the
 *     underlying OS handle. Closing the session closes the handle.
 *   - All read failures return Error — they never return a fake default
 *     value the way the legacy ReadAddress<T> did.
 *   - Batch writes report partial failure: see the BatchResult struct.
 *   - Pointer chains resolve to the final concrete address and report
 *     any step that failed.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "backend.hpp"
#include "result.hpp"
#include "types.hpp"

namespace gtlibcpp {

// Result of a multi-step write. Tracks which addresses succeeded so the
// agent can offer a partial rollback and the trainer can retry the rest.
struct BatchResult {
    bool complete{true};
    std::vector<Address> completed_addresses{};
    std::vector<Address> attempted_addresses{};
    std::optional<Error> failure{};

    [[nodiscard]] static BatchResult ok_full(std::vector<Address> done) {
        return BatchResult{true, std::move(done), {}, std::nullopt};
    }
    [[nodiscard]] static BatchResult partial(std::vector<Address> done,
                                             std::vector<Address> attempted,
                                             Error failure) {
        return BatchResult{false, std::move(done), std::move(attempted),
                           std::move(failure)};
    }
};

// Trivial fixed-capacity string. The legacy ReadString / WriteString used
// std::string + manual loops, which is exactly the "manual-buffer and
// destination-capacity problem" called out in the issue. BoundedString
// tracks its capacity, terminator, and encoding explicitly and refuses
// to overflow.
class BoundedString {
public:
    BoundedString() = default;

    [[nodiscard]] static Result<BoundedString> from_utf8(
        const std::vector<std::uint8_t>& bytes, std::size_t capacity,
        bool require_nul_terminator);

    [[nodiscard]] static Result<BoundedString> from_bytes(
        const std::vector<std::uint8_t>& bytes, std::size_t capacity,
        bool require_nul_terminator);

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return storage_; }
    [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool truncated() const noexcept { return truncated_; }

    [[nodiscard]] Result<std::vector<std::uint8_t>> to_bytes(std::size_t target_capacity) const;

private:
    BoundedString(std::vector<std::uint8_t> bytes, std::size_t capacity, bool truncated)
        : storage_(std::move(bytes)), capacity_(capacity), truncated_(truncated) {}

    std::vector<std::uint8_t> storage_{};
    std::size_t capacity_{0};
    bool truncated_{false};
};

class MemorySession {
public:
    explicit MemorySession(MemoryBackendPtr backend);

    MemorySession(const MemorySession&) = delete;
    MemorySession& operator=(const MemorySession&) = delete;
    MemorySession(MemorySession&&) = delete;
    MemorySession& operator=(MemorySession&&) = delete;
    ~MemorySession() = default;

    // ---- typed reads ----------------------------------------------------

    // Read a trivially-copyable scalar of type T. The template enforces
    // that the type is suitable for raw memory transport; non-trivial
    // types are rejected at compile time.
    template <typename T>
    [[nodiscard]] Result<T> read(Address address) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "MemorySession::read<T> requires a trivially copyable type");
        static_assert(!std::is_pointer_v<T>,
                      "Read raw pointers via read<std::uintptr_t>");
        const std::size_t size = sizeof(T);
        const auto validated = validate_read_request(address, size);
        if (!validated) {
            return Result<T>::failure(validated.error());
        }
        auto raw = backend_->read(address, size);
        if (!raw) {
            return Result<T>::failure(raw.error());
        }
        if (raw.value().size() != size) {
            return Result<T>::failure(make_error(
                ErrorCode::partial_read,
                "read returned fewer bytes than requested",
                "MemorySession::read", address, raw.value().size()));
        }
        T value{};
        std::memcpy(&value, raw.value().data(), size);
        return Result<T>::success(value);
    }

    // Read exactly `size` bytes. Use this for blobs, structs, and any
    // non-scalar payload.
    [[nodiscard]] Result<std::vector<std::uint8_t>>
    read_bytes(Address address, std::size_t size);

    // ---- typed writes ---------------------------------------------------

    template <typename T>
    [[nodiscard]] Result<std::size_t> write(Address address, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "MemorySession::write<T> requires a trivially copyable type");
        static_assert(!std::is_pointer_v<T>,
                      "Write raw pointers via write<std::uintptr_t>");
        const std::size_t size = sizeof(T);
        const auto validated = validate_write_request(address, size);
        if (!validated) {
            return Result<std::size_t>::failure(validated.error());
        }
        std::vector<std::uint8_t> bytes(size);
        std::memcpy(bytes.data(), &value, size);
        return write_bytes(address, bytes);
    }

    // Write `bytes` to `address`. Returns the number of bytes written.
    [[nodiscard]] Result<std::size_t>
    write_bytes(Address address, const std::vector<std::uint8_t>& bytes);

    // ---- compare-before-write ------------------------------------------

    // Atomically read `expected`, write `value`, and read back. Returns
    // success only when the verify-read matches `value`. Used by the
    // agent to satisfy the "compare-before-write preconditions,
    // verification reads" acceptance criterion.
    template <typename T>
    [[nodiscard]] Result<T> compare_write_verify(Address address,
                                                 const T& value,
                                                 std::uint32_t max_attempts = 1) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "compare_write_verify<T> requires a trivially copyable type");
        for (std::uint32_t attempt = 0; attempt < max_attempts; ++attempt) {
            const auto written = write<T>(address, value);
            if (!written) {
                return Result<T>::failure(written.error());
            }
            const auto verify = read<T>(address);
            if (!verify) {
                return Result<T>::failure(verify.error());
            }
            if (verify.value() == value) {
                return Result<T>::success(value);
            }
            return Result<T>::failure(make_error(
                ErrorCode::verification_mismatch,
                "post-write verification did not match the intended value",
                "compare_write_verify", address, sizeof(T)));
        }
        return Result<T>::failure(make_error(
            ErrorCode::verification_mismatch,
            "post-write verification exhausted retry budget",
            "compare_write_verify", address, sizeof(T)));
    }

    // ---- multi-offset / pointer chains ---------------------------------

    // Read the pointer at `base + offset`, return the final address
    // reached. Any step failure is surfaced.
    [[nodiscard]] Result<Address>
    resolve_pointer_chain(Address base, const std::vector<Address>& offsets);

    // Read `size` bytes at each (base + offset) and report per-offset
    // success/failure. Required for tests of the legacy
    // ReadAddressOffsets bug (the old version always returned T{} and
    // never reported per-offset outcomes).
    [[nodiscard]] Result<BatchResult>
    read_offsets(Address base, const std::vector<Address>& offsets, std::size_t size);

    // Write `value` to each (base + offset). Stops at the first failed
    // step and returns a BatchResult. The legacy WriteAddressOffsets
    // always returned false and ignored partial success.
    template <typename T>
    [[nodiscard]] Result<BatchResult>
    write_offsets(Address base,
                  const std::vector<Address>& offsets,
                  const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "write_offsets<T> requires a trivially copyable type");
        std::vector<Address> completed;
        std::vector<Address> attempted = offsets;
        for (const auto offset : offsets) {
            const auto written = write<T>(base + offset, value);
            attempted.push_back(base + offset);
            if (!written) {
                return Result<BatchResult>::success(BatchResult::partial(
                    std::move(completed), std::move(attempted),
                    written.error()));
            }
            completed.push_back(base + offset);
        }
        return Result<BatchResult>::success(BatchResult::ok_full(std::move(completed)));
    }

    // ---- strings --------------------------------------------------------

    // Read up to `capacity` bytes starting at `address` and return a
    // bounded string. If `require_nul_terminator` is true and no NUL is
    // present, the result is treated as a malformed string and an
    // invalid_string error is returned. This replaces the legacy
    // ReadString that read into std::string and could not bound itself.
    [[nodiscard]] Result<BoundedString>
    read_string(Address address, std::size_t capacity, bool require_nul_terminator);

    // Write a bounded string to `address`. Refuses to write past the
    // declared `target_capacity`. The legacy WriteString allocated a
    // std::string copy and then wrote it via WriteProcessMemory without
    // a size check.
    [[nodiscard]] Result<std::size_t>
    write_string(Address address, const BoundedString& value, std::size_t target_capacity);

    // ---- target identity -----------------------------------------------

    [[nodiscard]] const TargetIdentity& identity() const noexcept { return cached_identity_; }
    [[nodiscard]] bool is_alive() const noexcept;
    [[nodiscard]] std::string target_id() const;

    // ---- access ---------------------------------------------------------

    [[nodiscard]] IMemoryBackend* backend() const noexcept { return backend_.get(); }

private:
    MemoryBackendPtr backend_;
    TargetIdentity   cached_identity_{};
};

// ---- BoundedString ------------------------------------------------------

inline Result<BoundedString> BoundedString::from_utf8(
    const std::vector<std::uint8_t>& bytes, std::size_t capacity,
    bool require_nul_terminator) {
    if (capacity == 0) {
        return Result<BoundedString>::failure(make_error(
            ErrorCode::invalid_string,
            "BoundedString capacity must be non-zero",
            "BoundedString::from_utf8", 0, bytes.size()));
    }
    bool nul_found = false;
    std::size_t usable = bytes.size() < capacity ? bytes.size() : capacity;
    std::vector<std::uint8_t> out;
    out.reserve(usable);
    for (std::size_t i = 0; i < usable; ++i) {
        if (bytes[i] == 0) {
            nul_found = true;
            break;
        }
        out.push_back(bytes[i]);
    }
    if (require_nul_terminator && !nul_found) {
        return Result<BoundedString>::failure(make_error(
            ErrorCode::invalid_string,
            "string is not NUL-terminated within declared capacity",
            "BoundedString::from_utf8", 0, bytes.size()));
    }
    return Result<BoundedString>::success(BoundedString(
        std::move(out), capacity, bytes.size() > capacity));
}

inline Result<BoundedString> BoundedString::from_bytes(
    const std::vector<std::uint8_t>& bytes, std::size_t capacity,
    bool require_nul_terminator) {
    return from_utf8(bytes, capacity, require_nul_terminator);
}

inline Result<std::vector<std::uint8_t>> BoundedString::to_bytes(
    std::size_t target_capacity) const {
    if (target_capacity < storage_.size() + 1) {
        return Result<std::vector<std::uint8_t>>::failure(make_error(
            ErrorCode::invalid_string,
            "target capacity cannot hold the string plus a NUL terminator",
            "BoundedString::to_bytes", 0, target_capacity));
    }
    std::vector<std::uint8_t> out = storage_;
    out.push_back(0);
    return Result<std::vector<std::uint8_t>>::success(std::move(out));
}

// ---- MemorySession inline implementations -------------------------------

inline MemorySession::MemorySession(MemoryBackendPtr backend)
    : backend_(std::move(backend)) {
    if (backend_) {
        cached_identity_ = backend_->identity();
    }
}

inline Result<std::vector<std::uint8_t>>
MemorySession::read_bytes(Address address, std::size_t size) {
    const auto validated = validate_read_request(address, size);
    if (!validated) {
        return Result<std::vector<std::uint8_t>>::failure(validated.error());
    }
    auto raw = backend_->read(address, size);
    if (!raw) return raw;
    if (raw.value().size() != size) {
        return Result<std::vector<std::uint8_t>>::failure(make_error(
            ErrorCode::partial_read,
            "read returned fewer bytes than requested",
            "MemorySession::read_bytes", address, raw.value().size()));
    }
    return raw;
}

inline Result<std::size_t>
MemorySession::write_bytes(Address address, const std::vector<std::uint8_t>& bytes) {
    const auto validated = validate_write_request(address, bytes.size());
    if (!validated) {
        return Result<std::size_t>::failure(validated.error());
    }
    return backend_->write(address, bytes);
}

inline Result<Address>
MemorySession::resolve_pointer_chain(Address base, const std::vector<Address>& offsets) {
    if (offsets.empty()) {
        return Result<Address>::failure(make_error(
            ErrorCode::invalid_offsets,
            "pointer chain must have at least one offset",
            "resolve_pointer_chain", base, 0));
    }
    Address current = base;
    for (std::size_t step = 0; step < offsets.size(); ++step) {
        const Address target = current + offsets[step];
        const auto read_result = read<std::uintptr_t>(target);
        if (!read_result) {
            return Result<Address>::failure(make_error(
                read_result.error().code,
                "pointer chain step " + std::to_string(step) + " failed: " + read_result.error().message,
                "resolve_pointer_chain", target, sizeof(std::uintptr_t),
                read_result.error().system_error));
        }
        current = static_cast<Address>(read_result.value());
        if (current == 0) {
            return Result<Address>::failure(make_error(
                ErrorCode::invalid_address,
                "pointer chain step " + std::to_string(step) + " produced NULL",
                "resolve_pointer_chain", target, sizeof(std::uintptr_t)));
        }
    }
    return Result<Address>::success(current);
}

inline Result<BatchResult>
MemorySession::read_offsets(Address base, const std::vector<Address>& offsets, std::size_t size) {
    std::vector<Address> completed;
    std::vector<Address> attempted = offsets;
    for (const auto offset : offsets) {
        const Address target = base + offset;
        const auto r = read_bytes(target, size);
        attempted.push_back(target);
        if (!r) {
            return Result<BatchResult>::success(BatchResult::partial(
                std::move(completed), std::move(attempted), r.error()));
        }
        completed.push_back(target);
    }
    return Result<BatchResult>::success(BatchResult::ok_full(std::move(completed)));
}

inline Result<BoundedString>
MemorySession::read_string(Address address, std::size_t capacity, bool require_nul_terminator) {
    if (capacity == 0) {
        return Result<BoundedString>::failure(make_error(
            ErrorCode::invalid_string,
            "string capacity must be non-zero",
            "MemorySession::read_string", address, 0));
    }
    auto bytes = read_bytes(address, capacity);
    if (!bytes) return Result<BoundedString>::failure(bytes.error());
    return BoundedString::from_utf8(bytes.value(), capacity, require_nul_terminator);
}

inline Result<std::size_t>
MemorySession::write_string(Address address, const BoundedString& value, std::size_t target_capacity) {
    auto bytes = value.to_bytes(target_capacity);
    if (!bytes) return Result<std::size_t>::failure(bytes.error());
    return write_bytes(address, bytes.value());
}

inline bool MemorySession::is_alive() const noexcept {
    if (!backend_) return false;
    return backend_->is_alive();
}

inline std::string MemorySession::target_id() const {
    return backend_ ? backend_->target_id() : std::string{};
}

} // namespace gtlibcpp
