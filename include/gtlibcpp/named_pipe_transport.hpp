#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "gtlibcpp/agent.hpp"

namespace gtlibcpp {

struct NamedPipeServerOptions {
    std::string pipe_name{"gtlibcpp.agent"};
    std::uint32_t max_instances{1};
    std::uint32_t buffer_size{64 * 1024};
};

[[nodiscard]] std::shared_ptr<IAgentTransport>
make_named_pipe_server(const NamedPipeServerOptions& options);

[[nodiscard]] std::shared_ptr<IAgentTransport>
make_named_pipe_client(const std::string& pipe_name);

} // namespace gtlibcpp
