/*
 * Windows backend — wraps ReadProcessMemory / WriteProcessMemory on a
 * Win32 process handle. Compiles only on Windows; the CMake build
 * isolates it from the cross-platform tests.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gtlibcpp/backend.hpp"
#include "gtlibcpp/result.hpp"
#include "gtlibcpp/types.hpp"

namespace gtlibcpp {

struct WindowsBackendOptions {
    std::uint32_t pid{0};
    std::string   image_path{};
    Architecture  architecture{Architecture::unknown};
    bool          read_only{false};
    std::uint32_t desired_access{0};
};

// Factory declared in the public header. The implementation lives in
// src/windows_backend.cpp and is built only on Windows.
[[nodiscard]] MemoryBackendPtr make_windows_backend(const WindowsBackendOptions& options);

} // namespace gtlibcpp
