// Windows named-pipe JSON-RPC transport. Server accepts one client at a
// time per instance; the agent service creates one per process for the
// "local-only service" requirement. Compiled only on Windows.

#include "gtlibcpp/named_pipe_transport.hpp"

#include <windows.h>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace gtlibcpp {

namespace {

constexpr std::uint32_t kProtocolMagic = 0x47545043; // "GTPC"

void write_all(HANDLE pipe, const void* data, std::size_t size) {
    const char* p = static_cast<const char*>(data);
    std::size_t total = 0;
    while (total < size) {
        DWORD written = 0;
        if (!::WriteFile(pipe, p + total, static_cast<DWORD>(size - total),
                         &written, nullptr)) {
            throw std::runtime_error("named pipe write failed");
        }
        if (written == 0) throw std::runtime_error("named pipe closed during write");
        total += written;
    }
}

void read_exact(HANDLE pipe, void* data, std::size_t size) {
    char* p = static_cast<char*>(data);
    std::size_t total = 0;
    while (total < size) {
        DWORD got = 0;
        if (!::ReadFile(pipe, p + total, static_cast<DWORD>(size - total),
                        &got, nullptr)) {
            throw std::runtime_error("named pipe read failed");
        }
        if (got == 0) throw std::runtime_error("named pipe closed during read");
        total += got;
    }
}

class NamedPipeServer final : public IAgentTransport {
public:
    explicit NamedPipeServer(NamedPipeServerOptions options) : options_(std::move(options)) {}
    ~NamedPipeServer() override { close(); }

    [[nodiscard]] Result<void> send(const std::string& json) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pipe_ == INVALID_HANDLE_VALUE) {
            return Result<void>::failure(make_error(
                ErrorCode::not_connected, "no client connected",
                "NamedPipeServer::send"));
        }
        std::uint32_t magic = kProtocolMagic;
        std::uint32_t len = static_cast<std::uint32_t>(json.size());
        try {
            write_all(pipe_, &magic, sizeof(magic));
            write_all(pipe_, &len, sizeof(len));
            write_all(pipe_, json.data(), json.size());
        } catch (const std::exception& e) {
            return Result<void>::failure(make_error(
                ErrorCode::internal, e.what(),
                "NamedPipeServer::send"));
        }
        return Result<void>::success();
    }

    [[nodiscard]] Result<std::string> receive() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pipe_ == INVALID_HANDLE_VALUE) {
            // accept a new client
            pipe_ = ::CreateNamedPipeA(
                options_.pipe_name.c_str(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                options_.max_instances,
                options_.buffer_size,
                options_.buffer_size,
                0,
                nullptr);
            if (pipe_ == INVALID_HANDLE_VALUE) {
                return Result<std::string>::failure(make_error(
                    ErrorCode::not_connected,
                    "CreateNamedPipeA failed",
                    "NamedPipeServer::receive", 0, 0, ::GetLastError()));
            }
        }
        if (!::ConnectNamedPipe(pipe_, nullptr) && ::GetLastError() != ERROR_PIPE_CONNECTED) {
            return Result<std::string>::failure(make_error(
                ErrorCode::not_connected,
                "ConnectNamedPipe failed",
                "NamedPipeServer::receive", 0, 0, ::GetLastError()));
        }
        std::uint32_t magic = 0;
        std::uint32_t len = 0;
        try {
            read_exact(pipe_, &magic, sizeof(magic));
            read_exact(pipe_, &len, sizeof(len));
        } catch (const std::exception& e) {
            return Result<std::string>::failure(make_error(
                ErrorCode::internal, e.what(),
                "NamedPipeServer::receive"));
        }
        if (magic != kProtocolMagic) {
            return Result<std::string>::failure(make_error(
                ErrorCode::parse_failed, "bad protocol magic",
                "NamedPipeServer::receive"));
        }
        std::string out;
        out.resize(len);
        try {
            read_exact(pipe_, out.data(), len);
        } catch (const std::exception& e) {
            return Result<std::string>::failure(make_error(
                ErrorCode::internal, e.what(),
                "NamedPipeServer::receive"));
        }
        return Result<std::string>::success(std::move(out));
    }

    void close() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            ::DisconnectNamedPipe(pipe_);
            ::CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    NamedPipeServerOptions options_;
    HANDLE pipe_{INVALID_HANDLE_VALUE};
    std::mutex mutex_;
};

} // namespace

std::shared_ptr<IAgentTransport>
make_named_pipe_server(const NamedPipeServerOptions& options) {
    return std::make_shared<NamedPipeServer>(options);
}

std::shared_ptr<IAgentTransport>
make_named_pipe_client(const std::string& /*pipe_name*/) {
    // The production deployment uses an out-of-process client that
    // opens the pipe with CreateFileW; the library does not link
    // against that path itself. Tests use the in-process transport.
    return nullptr;
}

} // namespace gtlibcpp
