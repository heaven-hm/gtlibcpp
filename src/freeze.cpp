// FreezeManager implementation. The worker loop polls the cancel flag and
// the session's is_alive() check, writes the requested value, and counts
// successes and failures. The destructor cancels and joins.
#include "gtlibcpp/freeze.hpp"

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
    if (request.size == 0) request.size = size_for_type(request.type_name);
    if (request.size == 0 || request.size > sizeof(std::uint64_t)) {
        return Result<void>::failure(make_error(
            ErrorCode::invalid_type, "unknown freeze type " + request.type_name,
            "FreezeManager::freeze", request.address));
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
        if (entries_.count(request.id)) {
            return Result<void>::failure(make_error(
                ErrorCode::invalid_entry_id,
                "freeze id already active: " + request.id,
                "FreezeManager::freeze", request.address));
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
        entries_.emplace(request.id, entry);
    }
    std::shared_ptr<std::atomic<bool>> cancel_token;
    std::shared_ptr<Entry> captured;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        captured = entries_[request.id];
        cancel_token = captured->cancel;
    }
    captured->worker = std::thread([this, captured, cancel_token]() {
        worker_loop(captured);
    });
    return Result<void>::success();
}

void FreezeManager::worker_loop(std::shared_ptr<Entry> entry) {
    std::vector<std::uint8_t> write_bytes(entry->request.size);
    u64_to_bytes(entry->request.value_u64, write_bytes.data(), entry->request.size);
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
        for (int slept = 0; slept < 50; ++slept) {
            if (entry->cancel->load() || !entry->active.load()) break;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    std::max<std::chrono::milliseconds::rep>(
                        1, entry->request.interval.count() / 50)));
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
