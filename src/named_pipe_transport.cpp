#include "gtlibcpp/named_pipe_transport.hpp"

#include <windows.h>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace gtlibcpp {

namespace {

constexpr std::uint32_t kProtocolMagic = 0x47545043;

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
        HANDLE pipe;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pipe_ == INVALID_HANDLE_VALUE) {
                pipe_ = create_pipe();
            }
            pipe = pipe_;
        }
        if (pipe == INVALID_HANDLE_VALUE) {
            return Result<std::string>::failure(make_error(
                ErrorCode::not_connected,
                "CreateNamedPipeA failed",
                "NamedPipeServer::receive", 0, 0, ::GetLastError()));
        }
        if (!::ConnectNamedPipe(pipe, nullptr)
            && ::GetLastError() != ERROR_PIPE_CONNECTED) {
            return Result<std::string>::failure(make_error(
                ErrorCode::not_connected,
                "ConnectNamedPipe failed",
                "NamedPipeServer::receive", 0, 0, ::GetLastError()));
        }
        std::uint32_t magic = 0;
        std::uint32_t len = 0;
        try {
            read_exact(pipe, &magic, sizeof(magic));
            read_exact(pipe, &len, sizeof(len));
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
            read_exact(pipe, out.data(), len);
        } catch (const std::exception& e) {
            return Result<std::string>::failure(make_error(
                ErrorCode::internal, e.what(),
                "NamedPipeServer::receive"));
        }
        // Re-arm for the next client.
        std::lock_guard<std::mutex> lock(mutex_);
        ::DisconnectNamedPipe(pipe_);
        ::CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
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
    HANDLE create_pipe() const {
        return ::CreateNamedPipeA(
            options_.pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            options_.max_instances,
            options_.buffer_size,
            options_.buffer_size,
            0,
            nullptr);
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

class NamedPipeClient final : public IAgentTransport {
public:
    explicit NamedPipeClient(std::string pipe_path, std::uint32_t buffer_size)
        : pipe_path_(std::move(pipe_path)), buffer_size_(buffer_size) {}
    ~NamedPipeClient() override { close(); }

    NamedPipeClient(const NamedPipeClient&) = delete;
    NamedPipeClient& operator=(const NamedPipeClient&) = delete;

    [[nodiscard]] Result<void> send(const std::string& json) override {
        if (closed_) {
            return Result<void>::failure(make_error(
                ErrorCode::not_connected, "client closed",
                "NamedPipeClient::send"));
        }
        HANDLE pipe = ensure_connected();
        if (pipe == INVALID_HANDLE_VALUE) {
            return Result<void>::failure(make_error(
                ErrorCode::not_connected, "CreateFileA failed",
                "NamedPipeClient::send", 0, 0, ::GetLastError()));
        }
        std::uint32_t magic = kProtocolMagic;
        std::uint32_t len = static_cast<std::uint32_t>(json.size());
        try {
            write_all(pipe, &magic, sizeof(magic));
            write_all(pipe, &len, sizeof(len));
            write_all(pipe, json.data(), json.size());
        } catch (const std::exception& e) {
            return Result<void>::failure(make_error(
                ErrorCode::internal, e.what(), "NamedPipeClient::send"));
        }
        return Result<void>::success();
    }

    [[nodiscard]] Result<std::string> receive() override {
        if (closed_) {
            return Result<std::string>::failure(make_error(
                ErrorCode::not_connected, "client closed",
                "NamedPipeClient::receive"));
        }
        HANDLE pipe = ensure_connected();
        if (pipe == INVALID_HANDLE_VALUE) {
            return Result<std::string>::failure(make_error(
                ErrorCode::not_connected, "CreateFileA failed",
                "NamedPipeClient::receive", 0, 0, ::GetLastError()));
        }
        std::uint32_t magic = 0;
        std::uint32_t len = 0;
        try {
            read_exact(pipe, &magic, sizeof(magic));
            read_exact(pipe, &len, sizeof(len));
        } catch (const std::exception& e) {
            return Result<std::string>::failure(make_error(
                ErrorCode::internal, e.what(), "NamedPipeClient::receive"));
        }
        if (magic != kProtocolMagic) {
            return Result<std::string>::failure(make_error(
                ErrorCode::parse_failed, "bad protocol magic",
                "NamedPipeClient::receive"));
        }
        std::string out;
        out.resize(len);
        try {
            read_exact(pipe, out.data(), len);
        } catch (const std::exception& e) {
            return Result<std::string>::failure(make_error(
                ErrorCode::internal, e.what(), "NamedPipeClient::receive"));
        }
        return Result<std::string>::success(std::move(out));
    }

    void close() override {
        if (closed_) return;
        closed_ = true;
        if (pipe_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE ensure_connected() {
        if (pipe_ != INVALID_HANDLE_VALUE) return pipe_;
        // Try for a few seconds so the agent has time to bring the
        // server up if the client is started first.
        for (int i = 0; i < 50; ++i) {
            pipe_ = ::CreateFileA(
                pipe_path_.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe_ != INVALID_HANDLE_VALUE) return pipe_;
            ::Sleep(100);
        }
        return pipe_;
    }

    std::string pipe_path_;
    std::uint32_t buffer_size_;
    HANDLE pipe_{INVALID_HANDLE_VALUE};
    bool closed_{false};
};

std::shared_ptr<IAgentTransport>
make_named_pipe_client(const std::string& pipe_name) {
    std::string path = "\\\\.\\pipe\\" + pipe_name;
    return std::make_shared<NamedPipeClient>(std::move(path), 64 * 1024);
}

} // namespace gtlibcpp
