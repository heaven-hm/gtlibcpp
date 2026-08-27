/*
 * Structured logging for gtlibcpp. Every public operation and every
 * worker can emit a log event with severity, operation name, error
 * code, address, byte count, and a free-form message. Logs go through
 * an injected sink (function pointer + user data) so the agent
 * service can forward them to its structured log without coupling
 * the core to any particular logging library.
 *
 * The default sink writes to stderr in a single line; tests inject a
 * capture sink to assert on log content.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#include "result.hpp"

namespace gtlibcpp {

enum class LogLevel : std::uint8_t {
    debug   = 0,
    info    = 1,
    warn    = 2,
    error   = 3,
};

struct LogEvent {
    LogLevel   level{LogLevel::info};
    std::string operation{};
    ErrorCode   code{ErrorCode::ok};
    std::string message{};
    std::uint64_t address{0};
    std::size_t   bytes{0};
    std::int64_t  timestamp_ms{0};
};

class Logger {
public:
    using Sink = void (*)(const LogEvent&, void* user);
    static Logger& instance() noexcept {
        static Logger l;
        return l;
    }

    void set_sink(Sink sink, void* user) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = sink;
        user_ = user;
    }
    void reset_sink() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = nullptr;
        user_ = nullptr;
    }
    void set_min_level(LogLevel level) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = level;
    }

    void log(LogLevel level,
             const std::string& operation,
             ErrorCode code,
             const std::string& message,
             std::uint64_t address = 0,
             std::size_t bytes = 0) {
        if (level < min_level_) return;
        LogEvent ev{};
        ev.level = level;
        ev.operation = operation;
        ev.code = code;
        ev.message = message;
        ev.address = address;
        ev.bytes = bytes;
        ev.timestamp_ms = now_ms();
        Sink sink;
        void* user;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sink = sink_ ? sink_ : &default_sink;
            user = user_;
        }
        try {
            sink(ev, user);
        } catch (...) {
            // The sink is best-effort. A misbehaving sink must not
            // crash the core.
        }
    }

    void debug(const std::string& op, const std::string& m,
               std::uint64_t a = 0, std::size_t b = 0) {
        log(LogLevel::debug, op, ErrorCode::ok, m, a, b);
    }
    void info(const std::string& op, const std::string& m,
              std::uint64_t a = 0, std::size_t b = 0) {
        log(LogLevel::info, op, ErrorCode::ok, m, a, b);
    }
    void warn(const std::string& op, const std::string& m,
              ErrorCode c = ErrorCode::ok, std::uint64_t a = 0, std::size_t b = 0) {
        log(LogLevel::warn, op, c, m, a, b);
    }
    void error(const std::string& op, const std::string& m,
               ErrorCode c, std::uint64_t a = 0, std::size_t b = 0) {
        log(LogLevel::error, op, c, m, a, b);
    }

private:
    Logger() = default;

    static std::int64_t now_ms() noexcept {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }
    static void default_sink(const LogEvent& ev, void* /*user*/) {
        static const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        auto idx = static_cast<std::size_t>(ev.level);
        if (idx >= sizeof(names) / sizeof(names[0])) idx = 0;
        std::fprintf(stderr,
                     "ts=%lld level=%s op=%s code=%s msg=%s addr=0x%llx bytes=%zu\n",
                     static_cast<long long>(ev.timestamp_ms),
                     names[idx],
                     ev.operation.c_str(),
                     to_string(ev.code),
                     ev.message.c_str(),
                     static_cast<unsigned long long>(ev.address),
                     ev.bytes);
    }

    std::mutex mutex_;
    Sink sink_{nullptr};
    void* user_{nullptr};
    LogLevel min_level_{LogLevel::info};
};

} // namespace gtlibcpp
