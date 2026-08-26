#include "gtlibcpp/windows_backend.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <cstring>
#include <fstream>
#include <vector>

namespace gtlibcpp {

namespace {

class WindowsBackend final : public IMemoryBackend {
public:
    explicit WindowsBackend(WindowsBackendOptions options) : options_(std::move(options)) {
        identity_.pid = options_.pid;
        identity_.image_path = options_.image_path;
        identity_.architecture = options_.architecture;
        const DWORD access = options_.read_only
            ? PROCESS_VM_READ | PROCESS_QUERY_INFORMATION
            : (options_.desired_access
                 ? options_.desired_access
                 : (PROCESS_VM_READ | PROCESS_VM_WRITE
                    | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION));
        handle_ = ::OpenProcess(access, FALSE, options_.pid);
        if (handle_) {
            identity_.start_time = query_start_time(handle_);
        }
        identity_.image_sha256 = query_image_sha256(options_.image_path);
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
    static std::uint64_t query_start_time(HANDLE process) {
        if (!process) return 0;
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!::GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
            return 0;
        }
        // FILETIME is 100-ns intervals since 1601-01-01. We return the
        // raw value (not Unix-epoch-corrected) so callers can compare
        // identities consistently; the policy layer only does equality.
        return static_cast<std::uint64_t>(creation.dwLowDateTime)
             | (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32);
    }
    static std::string query_image_sha256(const std::string& image_path) {
        if (image_path.empty()) return {};
        HANDLE file = ::CreateFileA(
            image_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return {};
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<std::uint8_t> digest(32);
        if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(
                &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            ::CloseHandle(file); return {};
        }
        if (!BCRYPT_SUCCESS(::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
            ::BCryptCloseAlgorithmProvider(alg, 0);
            ::CloseHandle(file); return {};
        }
        std::vector<std::uint8_t> buffer(64 * 1024);
        for (;;) {
            DWORD got = 0;
            if (!::ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)
                || got == 0) {
                break;
            }
            ::BCryptHashData(hash, buffer.data(), got, 0);
        }
        ::CloseHandle(file);
        ::BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        ::BCryptDestroyHash(hash);
        ::BCryptCloseAlgorithmProvider(alg, 0);
        static const char* kHex = "0123456789abcdef";
        std::string out;
        out.reserve(digest.size() * 2);
        for (auto b : digest) {
            out.push_back(kHex[(b >> 4) & 0xF]);
            out.push_back(kHex[b & 0xF]);
        }
        return out;
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
