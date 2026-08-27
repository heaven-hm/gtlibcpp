#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "gtlibcpp/agent.hpp"

namespace gtlibcpp {

class InProcTransportPair;

class InProcTransport final : public IAgentTransport {
public:
    explicit InProcTransport(std::shared_ptr<InProcTransportPair> pair, bool client_side);
    ~InProcTransport() override;

    InProcTransport(const InProcTransport&) = delete;
    InProcTransport& operator=(const InProcTransport&) = delete;

    [[nodiscard]] Result<void> send(const std::string& json) override;
    [[nodiscard]] Result<std::string> receive() override;
    void close() override;

private:
    std::shared_ptr<InProcTransportPair> pair_;
    bool client_side_;
};

class InProcTransportPair
    : public std::enable_shared_from_this<InProcTransportPair> {
public:
    InProcTransportPair() = default;

    [[nodiscard]] std::shared_ptr<InProcTransport> make_client() {
        return std::make_shared<InProcTransport>(shared_from_this(), true);
    }
    [[nodiscard]] std::shared_ptr<InProcTransport> make_server() {
        return std::make_shared<InProcTransport>(shared_from_this(), false);
    }

    void close() noexcept;

private:
    friend class InProcTransport;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> client_to_server_;
    std::deque<std::string> server_to_client_;
    bool closed_{false};
};

} // namespace gtlibcpp
