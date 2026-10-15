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

[[nodiscard]] MemoryBackendPtr make_windows_backend(const WindowsBackendOptions& options);

} // namespace gtlibcpp
