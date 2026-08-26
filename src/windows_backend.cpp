#include "gtlibcpp/windows_backend.hpp"

#include <windows.h>
#include <psapi.h>
#include <cstring>

namespace gtlibcpp {

namespace {

class WindowsBackend final : public IMemoryBackend {
public:
    explicit WindowsBackend(WindowsBackendOptions options) : options_(std::move(options)) {
        identity_.pid = options_.pid;
        identity_.image_path = options_.image_path;
        identity_.architecture = options_.architecture;
        identity_.start_time = query_start_time(options_.pid);
        identity_.image_sha256 = query_image_sha256(options_.pid);
        const DWORD access = options_.read_only
            ? PROCESS_VM_READ | PROCESS_QUERY_INFORMATION
            : (options_.desired_access
                 ? options_.desired_access
                 : (PROCESS_VM_READ | PROCESS_VM_WRITE
                    | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION));
        handle_ = ::OpenProcess(access, FALSE, options_.pid);
    }

    ~WindowsBackend() override {
        if (handle_) {
            ::CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    [[nodiscard]] bool is_connected() const noexcept { return handle_ != nullptr; }

    [[nodiscard]] Result<std::vector<std::uint8_t>>
    read(Address address, std::size_t size) override {
        if (!handle_) {
            return Result<std::vector<std::uint8_t>>::failure(make_error(
                ErrorCode::not_connected,
                "OpenProcess failed for pid",
                "WindowsBackend::read", address, size, ::GetLastError()));
        }
        std::vector<std::uint8_t> buffer(size);
        SIZE_T bytes_read = 0;
        const BOOL ok = ::ReadProcessMemory(
            handle_, reinterpret_cast<LPCVOID>(address),
            buffer.data(), size, &bytes_read);
        if (!ok) {
            return Result<std::vector<std::uint8_t>>::failure(make_error(
                ErrorCode::read_failed,
                "ReadProcessMemory failed",
                "WindowsBackend::read", address, size, ::GetLastError()));
        }
        if (bytes_read != size) {
            return Result<std::vector<std::uint8_t>>::failure(make_error(
                ErrorCode::partial_read,
                "ReadProcessMemory returned fewer bytes than requested",
                "WindowsBackend::read", address, bytes_read, ::GetLastError()));
        }
        return Result<std::vector<std::uint8_t>>::success(std::move(buffer));
    }

    [[nodiscard]] Result<std::size_t>
    write(Address address, const std::vector<std::uint8_t>& bytes) override {
        if (!handle_) {
            return Result<std::size_t>::failure(make_error(
                ErrorCode::not_connected,
                "OpenProcess failed for pid",
                "WindowsBackend::write", address, bytes.size(), ::GetLastError()));
        }
        SIZE_T written = 0;
        const BOOL ok = ::WriteProcessMemory(
            handle_, reinterpret_cast<LPVOID>(address),
            bytes.data(), bytes.size(), &written);
        if (!ok) {
            return Result<std::size_t>::failure(make_error(
                ErrorCode::write_failed,
                "WriteProcessMemory failed",
                "WindowsBackend::write", address, bytes.size(), ::GetLastError()));
        }
        if (written != bytes.size()) {
            return Result<std::size_t>::failure(make_error(
                ErrorCode::partial_write,
                "WriteProcessMemory returned fewer bytes than requested",
                "WindowsBackend::write", address, written, ::GetLastError()));
        }
        return Result<std::size_t>::success(static_cast<std::size_t>(written));
    }

    [[nodiscard]] bool is_alive() noexcept override {
        if (!handle_) return false;
        DWORD exit_code = 0;
        if (!::GetExitCodeProcess(handle_, &exit_code)) return false;
        return exit_code == STILL_ACTIVE;
    }

    [[nodiscard]] std::string target_id() const override {
        return "pid:" + std::to_string(options_.pid);
    }

    [[nodiscard]] TargetIdentity identity() const override { return identity_; }

private:
    static std::uint64_t query_start_time(std::uint32_t pid) {
        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        std::uint64_t start = 0;
        if (::Process32First(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == pid) {
                    start = static_cast<std::uint64_t>(entry.dwFlags) << 32
                          | static_cast<std::uint64_t>(entry.pcPriClassBase);
                    break;
                }
            } while (::Process32Next(snapshot, &entry));
        }
        ::CloseHandle(snapshot);
        return start;
    }
    static std::string query_image_sha256(std::uint32_t /*pid*/) {
        return {};
    }

    WindowsBackendOptions options_;
    HANDLE handle_{nullptr};
    TargetIdentity identity_{};
};

} // namespace

MemoryBackendPtr make_windows_backend(const WindowsBackendOptions& options) {
    return std::make_shared<WindowsBackend>(options);
}

} // namespace gtlibcpp
