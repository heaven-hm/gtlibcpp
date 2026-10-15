#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/log.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace gtlibcpp {

namespace {

std::uint64_t bytes_to_u64(const std::uint8_t* p, std::size_t size) {
    std::uint64_t v = 0;
    std::memcpy(&v, p, std::min<std::size_t>(size, sizeof(v)));
    return v;
}

void u64_to_bytes(std::uint64_t v, std::uint8_t* p, std::size_t size) {
    std::memcpy(p, &v, std::min<std::size_t>(size, sizeof(v)));
}

} // namespace

FreezeManager::FreezeManager(std::shared_ptr<MemorySession> session)
    : session_(std::move(session)) {}

FreezeManager::~FreezeManager() {
    cancel_all();
}

std::size_t FreezeManager::size_for_type(const std::string& type_name) {
    if (type_name == "uint8"  || type_name == "int8")  return 1;
    if (type_name == "uint16" || type_name == "int16") return 2;
    if (type_name == "uint32" || type_name == "int32" || type_name == "float") return 4;
    if (type_name == "uint64" || type_name == "int64" || type_name == "double") return 8;
    return 0;
}

Result<void> FreezeManager::freeze(FreezeRequest request) {
    try {
        if (!session_) {
            return Result<void>::failure(make_error(
                ErrorCode::not_connected, "no session attached",
                "FreezeManager::freeze"));
        }
        if (request.address == invalid_address) {
            return Result<void>::failure(make_error(
                ErrorCode::invalid_address, "freeze address is 0",
                "FreezeManager::freeze"));
        }
        const std::size_t required_size = size_for_type(request.type_name);
        if (request.size == 0) request.size = required_size;
        if (required_size == 0) {
            return Result<void>::failure(make_error(
                ErrorCode::invalid_type, "unknown freeze type " + request.type_name,
                "FreezeManager::freeze", request.address));
        }
        if (request.size != required_size) {
            return Result<void>::failure(make_error(
                ErrorCode::invalid_size, "freeze size does not match type " + request.type_name,
                "FreezeManager::freeze", request.address, request.size));
        }
        if (request.id.empty()) {
            return Result<void>::failure(make_error(
                ErrorCode::invalid_entry_id, "freeze id must be non-empty",
                "FreezeManager::freeze", request.address));
        }
        if (request.interval < std::chrono::milliseconds(1)) {
            request.interval = std::chrono::milliseconds(50);
        }
        std::shared_ptr<Entry> entry;
        {
            std::lock_guard<std::mutex> lock(entries_mutex_);
            auto existing = entries_.find(request.id);
            if (existing != entries_.end()) {
                // Keep stopped entries available for restore until a new freeze
                // explicitly replaces their snapshot using the same ID.
                if (existing->second->active.load()) {
                    return Result<void>::failure(make_error(
                        ErrorCode::invalid_entry_id,
                        "freeze id already active: " + request.id,
                        "FreezeManager::freeze", request.address));
                }
                // unfreeze/restore clears active only after the worker joined.
                // This entry is now quiescent and safe to replace.
                if (existing->second->worker.joinable()) {
                    existing->second->worker.join();
                }
                entries_.erase(existing);
            }
            const auto initial = session_->read_bytes(request.address, request.size);
            if (!initial) {
                return Result<void>::failure(make_error(
                    initial.error().code,
                    "freeze could not snapshot the original value: " + initial.error().message,
                    "FreezeManager::freeze", request.address, request.size,
                    initial.error().system_error));
            }
            entry = std::make_shared<Entry>();
            entry->request = request;
            entry->original_bytes = initial.value();
            entry->active.store(true);
            entry->cancel->store(false);
            // Publish the entry and its worker atomically under the registry lock.
            // Shutdown callers cannot see a partially constructed worker.
            entry->worker = std::thread([this, entry]() {
                try {
                    worker_loop(entry);
                } catch (const std::exception& e) {
                    Logger::instance().error("FreezeManager::worker_loop", e.what(),
                                             ErrorCode::internal, entry->request.address);
                } catch (...) {
                    Logger::instance().error("FreezeManager::worker_loop", "unknown exception",
                                             ErrorCode::internal, entry->request.address);
                }
            });
            entries_.emplace(request.id, entry);
        }
        Logger::instance().info("FreezeManager::freeze", "freeze started",
                                request.address, request.size);
        return Result<void>::success();
    } catch (const std::exception& e) {
        Logger::instance().error("FreezeManager::freeze", e.what(),
                                 ErrorCode::internal, request.address);
        return Result<void>::failure(make_error(
            ErrorCode::internal, e.what(), "FreezeManager::freeze", request.address));
    } catch (...) {
        Logger::instance().error("FreezeManager::freeze", "unknown exception",
                                 ErrorCode::internal, request.address);
        return Result<void>::failure(make_error(
            ErrorCode::internal, "unknown exception",
            "FreezeManager::freeze", request.address));
    }
}

void FreezeManager::worker_loop(std::shared_ptr<Entry> entry) {
    std::vector<std::uint8_t> write_bytes(entry->request.size);
    if (entry->request.type_name == "float" && entry->request.size == sizeof(float)) {
        const float value = static_cast<float>(entry->request.has_floating_value
            ? entry->request.floating_value
            : static_cast<double>(entry->request.value_u64));
        std::memcpy(write_bytes.data(), &value, sizeof(value));
    } else if (entry->request.type_name == "double" && entry->request.size == sizeof(double)) {
        const double value = entry->request.has_floating_value
            ? entry->request.floating_value
            : static_cast<double>(entry->request.value_u64);
        std::memcpy(write_bytes.data(), &value, sizeof(value));
    } else {
        u64_to_bytes(entry->request.value_u64, write_bytes.data(), entry->request.size);
    }
    // Slice the interval into 1 ms steps so we react to cancel quickly
    // even when the requested interval is short or zero.
    const auto total_ms = std::max<std::int64_t>(1, entry->request.interval.count());
    while (!entry->cancel->load() && entry->active.load()) {
        if (!session_->is_alive()) {
            entry->active.store(false);
            std::lock_guard<std::mutex> lock(entry->status_mutex);
            entry->last_error = "target process exited during freeze";
            entry->fail_count.fetch_add(1);
            break;
        }
        const auto write = session_->write_bytes(entry->request.address, write_bytes);
        if (!write) {
            entry->fail_count.fetch_add(1);
            std::lock_guard<std::mutex> lock(entry->status_mutex);
            entry->last_error = write.error().message;
        } else if (write.value() != entry->request.size) {
            entry->fail_count.fetch_add(1);
            std::lock_guard<std::mutex> lock(entry->status_mutex);
            entry->last_error = "partial write during freeze";
        } else {
            entry->ok_count.fetch_add(1);
        }
        // Sleep in 1 ms slices and bail out on cancel.
        for (std::int64_t slept = 0; slept < total_ms; ++slept) {
            if (entry->cancel->load() || !entry->active.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

Result<void> FreezeManager::unfreeze(const std::string& id) {
    std::shared_ptr<Entry> entry;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        auto it = entries_.find(id);
        if (it == entries_.end()) {
            return Result<void>::failure(make_error(
                ErrorCode::snapshot_missing,
                "no freeze registered under " + id,
                "FreezeManager::unfreeze"));
        }
        entry = it->second;
    }
    if (entry->worker.joinable()) {
        entry->cancel->store(true);
        entry->worker.join();
    }
    entry->active.store(false);
    return Result<void>::success();
}

Result<void> FreezeManager::restore(const std::string& id) {
    std::shared_ptr<Entry> entry;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        auto it = entries_.find(id);
        if (it == entries_.end()) {
            return Result<void>::failure(make_error(
                ErrorCode::snapshot_missing,
                "no freeze registered under " + id,
                "FreezeManager::restore"));
        }
        entry = it->second;
    }
    if (entry->worker.joinable()) {
        entry->cancel->store(true);
        entry->worker.join();
    }
    const auto restored = session_->write_bytes(
        entry->request.address, entry->original_bytes);
    if (!restored) {
        return Result<void>::failure(restored.error());
    }
    if (restored.value() != entry->original_bytes.size()) {
        return Result<void>::failure(make_error(
            ErrorCode::partial_write,
            "restored only a subset of the original bytes",
            "FreezeManager::restore", entry->request.address,
            restored.value()));
    }
    entry->restored.store(true);
    entry->active.store(false);
    return Result<void>::success();
}

Result<FreezeStatus> FreezeManager::status(const std::string& id) const {
    std::shared_ptr<Entry> entry;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        auto it = entries_.find(id);
        if (it == entries_.end()) {
            return Result<FreezeStatus>::failure(make_error(
                ErrorCode::snapshot_missing,
                "no freeze registered under " + id,
                "FreezeManager::status"));
        }
        entry = it->second;
    }
    FreezeStatus s{};
    s.id = entry->request.id;
    s.address = entry->request.address;
    s.size = entry->request.size;
    s.value_u64 = entry->request.value_u64;
    s.successful_rewrites = entry->ok_count.load();
    s.failed_rewrites = entry->fail_count.load();
    s.original_value_u64 = bytes_to_u64(entry->original_bytes.data(),
                                        entry->original_bytes.size());
    s.active = entry->active.load();
    s.restored = entry->restored.load();
    {
        std::lock_guard<std::mutex> lock(entry->status_mutex);
        s.last_error = entry->last_error;
    }
    return Result<FreezeStatus>::success(std::move(s));
}

std::vector<FreezeStatus> FreezeManager::all_status() const {
    std::vector<FreezeStatus> out;
    std::vector<std::shared_ptr<Entry>> snapshot;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        snapshot.reserve(entries_.size());
        for (const auto& kv : entries_) snapshot.push_back(kv.second);
    }
    for (const auto& entry : snapshot) {
        FreezeStatus s{};
        s.id = entry->request.id;
        s.address = entry->request.address;
        s.size = entry->request.size;
        s.value_u64 = entry->request.value_u64;
        s.successful_rewrites = entry->ok_count.load();
        s.failed_rewrites = entry->fail_count.load();
        s.original_value_u64 = bytes_to_u64(entry->original_bytes.data(),
                                            entry->original_bytes.size());
        s.active = entry->active.load();
        s.restored = entry->restored.load();
        {
            std::lock_guard<std::mutex> lock(entry->status_mutex);
            s.last_error = entry->last_error;
        }
        out.push_back(std::move(s));
    }
    return out;
}

void FreezeManager::cancel_all() {
    std::vector<std::shared_ptr<Entry>> snapshot;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        snapshot.reserve(entries_.size());
        for (const auto& kv : entries_) snapshot.push_back(kv.second);
    }
    for (auto& entry : snapshot) {
        entry->cancel->store(true);
        entry->active.store(false);
    }
    for (auto& entry : snapshot) {
        if (entry->worker.joinable()) {
            entry->worker.join();
        }
    }
}

std::size_t FreezeManager::active_count() const {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    std::size_t count = 0;
    for (const auto& kv : entries_) {
        if (kv.second->active.load()) ++count;
    }
    return count;
}

} // namespace gtlibcpp
