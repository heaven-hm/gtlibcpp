/*
 * gtlibcpp::Result<T> — typed success/error return for every public operation.
 *
 * Rationale: the legacy GTLibc API returned bool / T{} on failure, which made
 * "read failed" indistinguishable from a real zero/empty value. Issue #1
 * requires that every public operation return enough information for the
 * caller (a trainer, an agent, or a test) to:
 *   1. tell success from failure,
 *   2. know the operation that failed,
 *   3. know the target address and byte count,
 *   4. know the underlying system error code (Win32 or errno), and
 *   5. preserve the request id so an audit log can correlate.
 *
 * The Result type is intentionally minimal and dependency-free; it is the
 * shared vocabulary between the cross-platform core, the Windows backend,
 * the parser, the freeze workers, and the agent service.
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gtlibcpp {

// Strongly typed error codes. The agent layer maps these to JSON-RPC error
// numbers, the structured log layer writes them verbatim, and the policy
// layer uses them to decide whether a request is allowed at all.
enum class ErrorCode : std::uint32_t {
    ok                              = 0,
    not_connected                   = 1001,
    target_not_found                = 1002,
    target_arch_mismatch            = 1003,
    target_identity_mismatch        = 1004,
    target_dead                     = 1005,
    invalid_address                 = 2001,
    invalid_size                    = 2002,
    invalid_offsets                 = 2003,
    invalid_hotkey                  = 2004,
    invalid_entry_id                = 2005,
    invalid_type                    = 2006,
    invalid_string                  = 2007,
    read_failed                     = 3001,
    partial_read                    = 3002,
    write_failed                    = 3003,
    partial_write                   = 3004,
    verification_mismatch           = 3005,
    snapshot_missing                = 3006,
    rollback_failed                 = 3007,
    parse_failed                    = 4001,
    parse_unsupported               = 4002,
    parse_diagnostic                = 4003,
    policy_denied                   = 5001,
    approval_required               = 5002,
    capability_missing              = 5003,
    rate_limited                    = 5004,
    timeout                         = 6001,
    cancelled                       = 6002,
    internal                        = 9001,
};

inline const char* to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::ok:                          return "ok";
        case ErrorCode::not_connected:               return "not_connected";
        case ErrorCode::target_not_found:            return "target_not_found";
        case ErrorCode::target_arch_mismatch:        return "target_arch_mismatch";
        case ErrorCode::target_identity_mismatch:    return "target_identity_mismatch";
        case ErrorCode::target_dead:                 return "target_dead";
        case ErrorCode::invalid_address:             return "invalid_address";
        case ErrorCode::invalid_size:                return "invalid_size";
        case ErrorCode::invalid_offsets:             return "invalid_offsets";
        case ErrorCode::invalid_hotkey:              return "invalid_hotkey";
        case ErrorCode::invalid_entry_id:            return "invalid_entry_id";
        case ErrorCode::invalid_type:                return "invalid_type";
        case ErrorCode::invalid_string:              return "invalid_string";
        case ErrorCode::read_failed:                 return "read_failed";
        case ErrorCode::partial_read:                return "partial_read";
        case ErrorCode::write_failed:                return "write_failed";
        case ErrorCode::partial_write:               return "partial_write";
        case ErrorCode::verification_mismatch:       return "verification_mismatch";
        case ErrorCode::snapshot_missing:            return "snapshot_missing";
        case ErrorCode::rollback_failed:             return "rollback_failed";
        case ErrorCode::parse_failed:                return "parse_failed";
        case ErrorCode::parse_unsupported:           return "parse_unsupported";
        case ErrorCode::parse_diagnostic:            return "parse_diagnostic";
        case ErrorCode::policy_denied:               return "policy_denied";
        case ErrorCode::approval_required:           return "approval_required";
        case ErrorCode::capability_missing:          return "capability_missing";
        case ErrorCode::rate_limited:                return "rate_limited";
        case ErrorCode::timeout:                     return "timeout";
        case ErrorCode::cancelled:                   return "cancelled";
        case ErrorCode::internal:                    return "internal";
    }
    return "internal";
}

struct Error {
    ErrorCode   code{ErrorCode::ok};
    std::string message{};
    std::string operation{};
    std::uint64_t address{0};
    std::size_t   bytes{0};
    std::uint32_t system_error{0};
    std::string request_id{};

    [[nodiscard]] bool ok() const noexcept { return code == ErrorCode::ok; }
};

inline Error make_error(ErrorCode code,
                        std::string message,
                        std::string operation,
                        std::uint64_t address = 0,
                        std::size_t   bytes   = 0,
                        std::uint32_t system_error = 0,
                        std::string request_id = {}) {
    return Error{code, std::move(message), std::move(operation),
                 address, bytes, system_error, std::move(request_id)};
}

template <typename T>
class Result {
public:
    Result(const T& value) : storage_(value) {}
    Result(T&& value) : storage_(std::move(value)) {}

    static Result success(T value) {
        return Result(std::move(value));
    }
    static Result failure(Error error) {
        Result r{};
        r.storage_ = std::move(error);
        r.has_value_ = false;
        return r;
    }

    [[nodiscard]] bool ok() const noexcept { return has_value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const T& value() const& {
        if (!has_value_) {
            std::fputs("gtlibcpp::Result::value() called on error\n", stderr);
            std::abort();
        }
        return std::get<T>(storage_);
    }
    [[nodiscard]] T&& value() && {
        if (!has_value_) {
            std::fputs("gtlibcpp::Result::value() called on error\n", stderr);
            std::abort();
        }
        return std::move(std::get<T>(storage_));
    }
    [[nodiscard]] T value_or(T fallback) const& {
        if (!has_value_) return fallback;
        return std::get<T>(storage_);
    }
    [[nodiscard]] T value_or(T fallback) && {
        if (!has_value_) return fallback;
        return std::move(std::get<T>(storage_));
    }
    [[nodiscard]] const T& expect(const char* msg) const& {
        if (!has_value_) {
            std::fprintf(stderr, "gtlibcpp::Result::expect: %s\n",
                         msg ? msg : "(no message)");
            std::abort();
        }
        return std::get<T>(storage_);
    }
    [[nodiscard]] const Error& error() const& {
        static const Error none{};
        if (has_value_) return none;
        return std::get<Error>(storage_);
    }
    [[nodiscard]] Error error() && {
        if (has_value_) return Error{};
        return std::move(std::get<Error>(storage_));
    }

private:
    Result() : storage_(T{}), has_value_(true) {}
    std::variant<T, Error> storage_;
    bool has_value_{true};
};

template <>
class Result<void> {
public:
    Result() = default;
    static Result success() { return Result(true); }
    static Result failure(Error error) {
        Result r;
        r.error_ = std::move(error);
        r.has_value_ = false;
        return r;
    }

    [[nodiscard]] bool ok() const noexcept { return has_value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] const Error& error() const& { return error_; }

private:
    explicit Result(bool ok) : has_value_(ok) {}
    Error error_{};
    bool has_value_{true};
};

} // namespace gtlibcpp
