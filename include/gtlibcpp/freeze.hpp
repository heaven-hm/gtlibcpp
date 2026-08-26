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

struct FreezeRequest {
    std::string  id{};
    Address      address{0};
    std::string  type_name{};
    std::uint64_t value_u64{0};
    std::chrono::milliseconds interval{std::chrono::milliseconds(50)};
    std::size_t   size{0};
};

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

    void cancel_all();

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
