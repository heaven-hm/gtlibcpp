#include "gtlibcpp/inproc_transport.hpp"

#include <chrono>
#include <utility>

namespace gtlibcpp {

InProcTransport::InProcTransport(std::shared_ptr<InProcTransportPair> pair, bool client_side)
    : pair_(std::move(pair)), client_side_(client_side) {}

InProcTransport::~InProcTransport() { close(); }

Result<void> InProcTransport::send(const std::string& json) {
    if (!pair_) {
        return Result<void>::failure(make_error(
            ErrorCode::not_connected, "transport detached",
            "InProcTransport::send"));
    }
    std::lock_guard<std::mutex> lock(pair_->mutex_);
    if (pair_->closed_) {
        return Result<void>::failure(make_error(
            ErrorCode::not_connected, "transport closed",
            "InProcTransport::send"));
    }
    if (client_side_) {
        pair_->client_to_server_.push_back(json);
    } else {
        pair_->server_to_client_.push_back(json);
    }
    pair_->cv_.notify_one();
    return Result<void>::success();
}

Result<std::string> InProcTransport::receive() {
    if (!pair_) {
        return Result<std::string>::failure(make_error(
            ErrorCode::not_connected, "transport detached",
            "InProcTransport::receive"));
    }
    std::unique_lock<std::mutex> lock(pair_->mutex_);
    auto& queue = client_side_ ? pair_->server_to_client_ : pair_->client_to_server_;
    pair_->cv_.wait_for(lock, std::chrono::milliseconds(50),
                        [&]() { return !queue.empty() || pair_->closed_; });
    if (queue.empty()) {
        if (pair_->closed_) {
            return Result<std::string>::failure(make_error(
                ErrorCode::not_connected, "transport closed",
                "InProcTransport::receive"));
        }
        return Result<std::string>::failure(make_error(
            ErrorCode::timeout, "no message available",
            "InProcTransport::receive"));
    }
    std::string out = std::move(queue.front());
    queue.pop_front();
    return Result<std::string>::success(std::move(out));
}

void InProcTransport::close() {
    if (pair_) pair_->close();
}

void InProcTransportPair::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    cv_.notify_all();
}

} // namespace gtlibcpp
