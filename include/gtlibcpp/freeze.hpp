/*
 * FreezeManager — cancellable, joinable workers; synchronized state;
 * original-value restoration; correct success checks; process-exit
 * cancellation.
 *
 * Replaces the legacy detached-thread approach at GTLibc.cpp:912-940. The
 * legacy version:
 *   - used std::thread::detach, so the worker could outlive GTLibc;
 *   - checked `!WriteProcessMemory(...) && bytesWritten == sizeof(value)`
 *     which is false on success (the legacy code only ever entered the
 *     failure path on a successful write);
 *   - used unsynchronized state for the cancel flag;
 *   - never restored the original value on unfreeze.
 *
 * The new manager stores the original value at freeze time, runs a worker
 * that re-writes the value on a fixed cadence, cancels on process exit
 * or unfreeze, restores the original value on unfreeze, and joins the
 * worker before destroying the manager.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "memory_session.hpp"
#include "result.hpp"
#include "types.hpp"

namespace gtlibcpp {

// A single freeze request. Captures the address, the value to re-write,
// the cadence, and the declared type. Multiple requests against the same
// address are not allowed — the second call returns an error instead of
// silently overwriting.
struct FreezeRequest {
    std::string  id{};
    Address      address{0};
    std::string  type_name{};     // "uint8" | "uint16" | "uint32" | "uint64" | "float" | "double"
    std::uint64_t value_u64{0};
    std::chrono::milliseconds interval{std::chrono::milliseconds(50)};
    std::size_t   size{0};        // 0 = derive from type_name
};

// Status of a single freeze. Reported by freeze_status and by the agent.
struct FreezeStatus {
    std::string  id{};
    Address      address{0};
    std::size_t  size{0};
    std::uint64_t value_u64{0};
    std::size_t  successful_rewrites{0};
    std::size_t  failed_rewrites{0};
    std::size_t  original_value_u64{0};
    bool         active{false};
    bool         restored{false};
    std::string  last_error{};
};

class FreezeManager {
public:
    explicit FreezeManager(std::shared_ptr<MemorySession> session);
    ~FreezeManager();

    FreezeManager(const FreezeManager&) = delete;
    FreezeManager& operator=(const FreezeManager&) = delete;
    FreezeManager(FreezeManager&&) = delete;
    FreezeManager& operator=(FreezeManager&&) = delete;

    [[nodiscard]] Result<void> freeze(FreezeRequest request);
    [[nodiscard]] Result<void> unfreeze(const std::string& id);
    [[nodiscard]] Result<void> restore(const std::string& id);
    [[nodiscard]] Result<FreezeStatus> status(const std::string& id) const;
    [[nodiscard]] std::vector<FreezeStatus> all_status() const;

    // Cancel every active freeze. Called automatically on destruction
    // and on process-exit detection. Workers are joined.
    void cancel_all();

    // Number of currently-active freezes.
    [[nodiscard]] std::size_t active_count() const;

private:
    struct Entry {
        FreezeRequest request{};
        std::vector<std::uint8_t> original_bytes{};
        std::thread  worker{};
        std::shared_ptr<std::atomic<bool>> cancel{std::make_shared<std::atomic<bool>>(false)};
        std::atomic<bool> active{false};
        std::atomic<bool> restored{false};
        std::atomic<std::size_t> ok_count{0};
        std::atomic<std::size_t> fail_count{0};
        mutable std::mutex status_mutex{};
        std::string last_error{};
    };

    static std::size_t size_for_type(const std::string& type_name);
    void worker_loop(std::shared_ptr<Entry> entry);

    std::shared_ptr<MemorySession> session_;
    mutable std::mutex entries_mutex_;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries_;
};

} // namespace gtlibcpp
