#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gtlibcpp/backend.hpp"
#include "gtlibcpp/result.hpp"
#include "gtlibcpp/types.hpp"

namespace gtlibcpp {

struct DarwinBackendOptions {
    std::uint32_t pid{0};
    std::string   image_path{};
    Architecture  architecture{Architecture::unknown};
    bool          read_only{false};
};

[[nodiscard]] MemoryBackendPtr make_darwin_backend(const DarwinBackendOptions& options);

[[nodiscard]] std::uint32_t find_darwin_process(const std::vector<std::string>& process_names);

[[nodiscard]] std::string get_darwin_process_path(std::uint32_t pid);

} // namespace gtlibcpp
