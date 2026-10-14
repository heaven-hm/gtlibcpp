// GTLibCpp Native C++ Trainer Client: MCP Tools & Direct JSON-RPC
// Cross-platform (Windows & POSIX), Standard Library + GTLibCpp Core.
// Replaces the legacy Python trainer client with a 100% pure C++ implementation.

#include "gtlibcpp/agent.hpp"
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/parser.hpp"
#include "gtlibcpp/policy.hpp"
#include "simulated_game.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#endif

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <limits>
#include <queue>

namespace {
using namespace gtlibcpp;

class ChildProcess {
public:
    virtual ~ChildProcess() = default;
    virtual bool write_line(const std::string& line) = 0;
    virtual bool read_line(std::string& line) = 0;
    virtual void close() = 0;
};

#ifdef _WIN32
class WindowsProcess final : public ChildProcess {
    HANDLE child_proc_{nullptr};
    HANDLE in_write_{nullptr};
    HANDLE out_read_{nullptr};
    std::string read_buf_;

public:
    static std::unique_ptr<ChildProcess> spawn(const std::vector<std::string>& args) {
        if (args.empty()) return nullptr;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE in_read = nullptr, in_write = nullptr;
        if (!CreatePipe(&in_read, &in_write, &sa, 0)) return nullptr;
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);

        HANDLE out_read = nullptr, out_write = nullptr;
        if (!CreatePipe(&out_read, &out_write, &sa, 0)) {
            CloseHandle(in_read);
            CloseHandle(in_write);
            return nullptr;
        }
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.hStdInput = in_read;
        si.hStdOutput = out_write;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi{};
        std::string cmdline;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) cmdline += " ";
            if (args[i].find(' ') != std::string::npos) {
                cmdline += "\"" + args[i] + "\"";
            } else {
                cmdline += args[i];
            }
        }

        std::vector<char> cmd_buf(cmdline.begin(), cmdline.end());
        cmd_buf.push_back('\0');

        BOOL ok = CreateProcessA(
            nullptr, cmd_buf.data(), nullptr, nullptr, TRUE,
            0, nullptr, nullptr, &si, &pi);

        CloseHandle(in_read);
        CloseHandle(out_write);

        if (!ok) {
            CloseHandle(in_write);
            CloseHandle(out_read);
            return nullptr;
        }

        CloseHandle(pi.hThread);

        auto proc = std::make_unique<WindowsProcess>();
        proc->child_proc_ = pi.hProcess;
        proc->in_write_ = in_write;
        proc->out_read_ = out_read;
        return proc;
    }

    bool write_line(const std::string& line) override {
        if (!in_write_) return false;
        std::string payload = line + "\n";
        DWORD written = 0;
        return WriteFile(in_write_, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) != 0;
    }

    bool read_line(std::string& line) override {
        line.clear();
        while (true) {
            auto pos = read_buf_.find('\n');
            if (pos != std::string::npos) {
                line = read_buf_.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                read_buf_.erase(0, pos + 1);
                return true;
            }
            char buf[1024];
            DWORD read_bytes = 0;
            if (!ReadFile(out_read_, buf, sizeof(buf), &read_bytes, nullptr) || read_bytes == 0) {
                if (!read_buf_.empty()) {
                    line = read_buf_;
                    read_buf_.clear();
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    return true;
                }
                return false;
            }
            read_buf_.append(buf, read_bytes);
        }
    }

    void close() override {
        if (in_write_) { CloseHandle(in_write_); in_write_ = nullptr; }
        if (out_read_) { CloseHandle(out_read_); out_read_ = nullptr; }
        if (child_proc_) {
            TerminateProcess(child_proc_, 0);
            WaitForSingleObject(child_proc_, 3000);
            CloseHandle(child_proc_);
            child_proc_ = nullptr;
        }
    }

    ~WindowsProcess() override { close(); }
};
#else
class PosixProcess final : public ChildProcess {
    pid_t pid_{-1};
    int in_fd_{-1};
    int out_fd_{-1};
    std::string read_buf_;

public:
    static std::unique_ptr<ChildProcess> spawn(const std::vector<std::string>& args) {
        if (args.empty()) return nullptr;
        int in_pipe[2];
        int out_pipe[2];
        if (pipe(in_pipe) != 0) return nullptr;
        if (pipe(out_pipe) != 0) {
            ::close(in_pipe[0]);
            ::close(in_pipe[1]);
            return nullptr;
        }

        pid_t pid = fork();
        if (pid < 0) {
            ::close(in_pipe[0]); ::close(in_pipe[1]);
            ::close(out_pipe[0]); ::close(out_pipe[1]);
            return nullptr;
        }

        if (pid == 0) {
            ::dup2(in_pipe[0], STDIN_FILENO);
            ::dup2(out_pipe[1], STDOUT_FILENO);
            ::close(in_pipe[0]); ::close(in_pipe[1]);
            ::close(out_pipe[0]); ::close(out_pipe[1]);

            std::vector<char*> c_args;
            c_args.reserve(args.size() + 1);
            for (const auto& a : args) c_args.push_back(const_cast<char*>(a.c_str()));
            c_args.push_back(nullptr);
            execvp(c_args[0], c_args.data());
            _exit(127);
        }

        ::close(in_pipe[0]);
        ::close(out_pipe[1]);
        auto proc = std::make_unique<PosixProcess>();
        proc->pid_ = pid;
        proc->in_fd_ = in_pipe[1];
        proc->out_fd_ = out_pipe[0];
        return proc;
    }

    bool write_line(const std::string& line) override {
        if (in_fd_ < 0) return false;
        std::string payload = line + "\n";
        std::size_t written = 0;
        while (written < payload.size()) {
            ssize_t n = ::write(in_fd_, payload.data() + written, payload.size() - written);
            if (n <= 0) return false;
            written += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool read_line(std::string& line) override {
        line.clear();
        while (true) {
            auto pos = read_buf_.find('\n');
            if (pos != std::string::npos) {
                line = read_buf_.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                read_buf_.erase(0, pos + 1);
                return true;
            }
            char buf[1024];
            ssize_t n = ::read(out_fd_, buf, sizeof(buf));
            if (n <= 0) {
                if (!read_buf_.empty()) {
                    line = read_buf_;
                    read_buf_.clear();
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    return true;
                }
                return false;
            }
            read_buf_.append(buf, static_cast<std::size_t>(n));
        }
    }

    void close() override {
        if (in_fd_ >= 0) { ::close(in_fd_); in_fd_ = -1; }
        if (out_fd_ >= 0) { ::close(out_fd_); out_fd_ = -1; }
        if (pid_ > 0) {
            int status = 0;
            kill(pid_, SIGTERM);
            waitpid(pid_, &status, 0);
            pid_ = -1;
        }
    }

    ~PosixProcess() override { close(); }
};
#endif

// In-process fallback when no external executable path is found or specified.
class InProcessTrainerAdapter final : public ChildProcess {
    std::shared_ptr<MemorySession> session_;
    std::shared_ptr<Policy> policy_;
    std::shared_ptr<FreezeManager> freeze_;
    std::shared_ptr<CheatTableParser> parser_;
    std::unique_ptr<AgentService> agent_;
    bool direct_rpc_{false};
    std::queue<std::string> output_lines_;

public:
    explicit InProcessTrainerAdapter(bool direct_rpc) : direct_rpc_(direct_rpc) {
        auto backend = std::make_shared<demo::SimulatedGameProcess>();
        session_ = std::make_shared<MemorySession>(backend);
        policy_ = std::make_shared<Policy>(std::vector<TargetManifest>{});
        freeze_ = std::make_shared<FreezeManager>(session_);
        parser_ = std::make_shared<CheatTableParser>();
        agent_ = std::make_unique<AgentService>(session_, policy_, freeze_, parser_);
    }

    bool write_line(const std::string& line) override {
        if (line.empty()) return true;
        auto req = json_decode(line);
        if (!req || req.value().kind != JsonValue::Kind::object) {
            output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}");
            return true;
        }
        const auto* id = json_get(req.value(), "id");
        if (!id) return true; // Notification
        const auto method = json_get_string(req.value(), "method");
        const auto* params = json_get(req.value(), "params");
        JsonValue empty_obj = JsonValue::make_object({});
        const auto& p = params ? *params : empty_obj;

        if (direct_rpc_) {
            const std::string tool_name = method == "read" ? "gt_read"
                                        : method == "resolve" ? "gt_resolve"
                                        : method == "inspect" ? "gt_inspect"
                                        : method == "status" ? "gt_status" : "";
            if (tool_name.empty()) {
                output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":" + json_encode(*id) +
                                   ",\"error\":{\"code\":-32601,\"message\":\"Method unavailable in read-only trainer\"}}");
            } else {
                auto handled = agent_->handle(line);
                if (handled.ok()) {
                    output_lines_.push(handled.value());
                } else {
                    output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":" + json_encode(*id) +
                                       ",\"error\":{\"code\":-32603,\"message\":\"" + handled.error().message + "\"}}");
                }
            }
        } else if (method == "initialize") {
            output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":" + json_encode(*id) +
                               ",\"result\":{\"protocolVersion\":\"2025-03-26\","
                               "\"capabilities\":{\"tools\":{}},"
                               "\"serverInfo\":{\"name\":\"gtlibcpp-readonly-trainer\",\"version\":\"1.0.0\"}}}");
        } else if (method == "ping") {
            output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":" + json_encode(*id) + ",\"result\":{}}");
        } else if (method == "tools/list") {
            output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":" + json_encode(*id) +
                               ",\"result\":{\"tools\":["
                               "{\"name\":\"gt_inspect\",\"description\":\"Inspect game identity (read only)\"},"
                               "{\"name\":\"gt_read\",\"description\":\"Read 1-256 bytes memory\"},"
                               "{\"name\":\"gt_resolve\",\"description\":\"Resolve pointer chain\"},"
                               "{\"name\":\"gt_status\",\"description\":\"Inspect freeze status\"}]}}");
        } else if (method == "tools/call") {
            const auto tool_name = json_get_string(p, "name");
            const auto* args = json_get(p, "arguments");
            const auto& a = args ? *args : empty_obj;
            const std::string mapped_method = tool_name == "gt_inspect" ? "inspect"
                                            : tool_name == "gt_read" ? "read"
                                            : tool_name == "gt_resolve" ? "resolve"
                                            : tool_name == "gt_status" ? "status" : "";
            if (mapped_method.empty()) {
                output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":" + json_encode(*id) +
                                   ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"Unknown or disabled tool\"}],\"isError\":true}}");
            } else {
                std::string sub_req = "{\"jsonrpc\":\"2.0\",\"id\":\"mcp\",\"method\":\"" +
                                      mapped_method + "\",\"params\":" + json_encode(a) + "}";
                auto reply = agent_->handle(sub_req);
                if (reply.ok()) {
                    auto parsed = json_decode(reply.value());
                    bool is_err = parsed && json_get(parsed.value(), "error") != nullptr;
                    JsonObject res_obj;
                    res_obj.emplace_back("content", JsonValue::make_array({
                        JsonValue::make_object({
                            {"type", JsonValue::make_string("text")},
                            {"text", JsonValue::make_string(reply.value())}
                        })
                    }));
                    res_obj.emplace_back("isError", JsonValue::make_bool(is_err));
                    output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":" + json_encode(*id) +
                                       ",\"result\":" + json_encode(JsonValue::make_object(std::move(res_obj))) + "}");
                } else {
                    output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":" + json_encode(*id) +
                                       ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"" + reply.error().message + "\"}],\"isError\":true}}");
                }
            }
        } else {
            output_lines_.push("{\"jsonrpc\":\"2.0\",\"id\":" + json_encode(*id) +
                               ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
        }
        return true;
    }

    bool read_line(std::string& line) override {
        if (output_lines_.empty()) return false;
        line = output_lines_.front();
        output_lines_.pop();
        return true;
    }

    void close() override {
        while (!output_lines_.empty()) output_lines_.pop();
    }
};

JsonValue rpc_call(ChildProcess& server, const std::string& method, JsonValue params, const JsonValue& request_id) {
    JsonObject msg;
    msg.emplace_back("jsonrpc", JsonValue::make_string("2.0"));
    msg.emplace_back("id", request_id);
    msg.emplace_back("method", JsonValue::make_string(method));
    msg.emplace_back("params", std::move(params));
    std::string line = json_encode(JsonValue::make_object(std::move(msg)));
    if (!server.write_line(line)) {
        throw std::runtime_error("Trainer server exited before receiving request");
    }
    std::string reply_line;
    if (!server.read_line(reply_line)) {
        throw std::runtime_error("Trainer server exited before responding");
    }
    auto parsed = json_decode(reply_line);
    if (!parsed || parsed.value().kind != JsonValue::Kind::object) {
        throw std::runtime_error("Failed to parse trainer response: " + reply_line);
    }
    const auto& res_obj = parsed.value();
    if (const auto* err = json_get(res_obj, "error")) {
        throw std::runtime_error(method + " returned error: " + json_encode(*err));
    }
    const auto* result = json_get(res_obj, "result");
    if (!result) {
        throw std::runtime_error(method + " missing result field: " + reply_line);
    }
    return *result;
}

std::string get_exe_dir(const char* argv0) {
    std::string path(argv0);
    auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

} // namespace

int main(int argc, char** argv) {
    std::string exe_path;
    std::string mode = "mcp";
    std::int64_t target_pid = -1;
    std::string architecture;
    std::string image_path;
    bool interactive = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::cout << "GTLibCpp Native C++ Trainer Client\n"
                      << "Usage: gtlibcpp_agent_trainer_client [options]\n\n"
                      << "Options:\n"
                      << "  --exe=PATH          Path to gtlibcpp_agent_trainer executable\n"
                      << "  --mode=mcp|json-rpc Communication mode (default: mcp)\n"
                      << "  --pid=NUMBER        Target Windows process ID (read-only)\n"
                      << "  --arch=x86|x64      Target architecture\n"
                      << "  --image-path=PATH   Target executable image path\n"
                      << "  --interactive       Start interactive command shell\n"
                      << "  --help              Display this help message\n";
            return 0;
        } else if (arg.rfind("--exe=", 0) == 0) {
            exe_path = arg.substr(6);
        } else if (arg == "--exe" && i + 1 < argc) {
            exe_path = argv[++i];
        } else if (arg.rfind("--mode=", 0) == 0) {
            mode = arg.substr(7);
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg.rfind("--pid=", 0) == 0) {
            target_pid = std::stoll(arg.substr(6));
        } else if (arg.rfind("--arch=", 0) == 0) {
            architecture = arg.substr(7);
        } else if (arg.rfind("--image-path=", 0) == 0) {
            image_path = arg.substr(13);
        } else if (arg == "--interactive") {
            interactive = true;
        }
    }

    // Attempt to locate server binary if not explicitly provided
    if (exe_path.empty()) {
        std::string dir = get_exe_dir(argv[0]);
        std::vector<std::string> candidates = {
            dir + "/gtlibcpp_agent_trainer",
            dir + "/gtlibcpp_agent_trainer.exe",
            dir + "/Release/gtlibcpp_agent_trainer.exe",
            "build/examples/gtlibcpp_agent_trainer",
            "build/examples/gtlibcpp_agent_trainer.exe",
            "build/examples/Release/gtlibcpp_agent_trainer.exe"
        };
        for (const auto& candidate : candidates) {
            FILE* f = fopen(candidate.c_str(), "rb");
            if (f) {
                fclose(f);
                exe_path = candidate;
                break;
            }
        }
    }

    std::unique_ptr<ChildProcess> server;
    if (!exe_path.empty()) {
        std::vector<std::string> cmd = {exe_path};
        if (mode == "json-rpc") cmd.push_back("--json-rpc");
        if (target_pid > 0) cmd.push_back("--pid=" + std::to_string(target_pid));
        if (!architecture.empty()) cmd.push_back("--arch=" + architecture);
        if (!image_path.empty()) cmd.push_back("--image-path=" + image_path);

#ifdef _WIN32
        server = WindowsProcess::spawn(cmd);
#else
        server = PosixProcess::spawn(cmd);
#endif
        if (server) {
            std::cout << "[+] Spawned external trainer process: " << exe_path << "\n";
        }
    }

    if (!server) {
        std::cout << "[+] Running in-process GTLibCpp trainer engine (" << mode << " mode)\n";
        server = std::make_unique<InProcessTrainerAdapter>(mode == "json-rpc");
    }

    try {
        if (mode == "mcp") {
            std::cout << "========================================================\n";
            std::cout << "  GTLibCpp C++ Trainer: Model Context Protocol (MCP)    \n";
            std::cout << "========================================================\n\n";

            // 1. Initialize MCP
            JsonObject init_params;
            init_params.emplace_back("protocolVersion", JsonValue::make_string("2025-03-26"));
            init_params.emplace_back("capabilities", JsonValue::make_object({}));
            init_params.emplace_back("clientInfo", JsonValue::make_object({
                {"name", JsonValue::make_string("gtlibcpp-trainer-client-cpp")},
                {"version", JsonValue::make_string("1.0.0")}
            }));

            auto init_res = rpc_call(*server, "initialize", JsonValue::make_object(std::move(init_params)), JsonValue::make_int(1));
            std::cout << "[+] MCP Protocol Version: " << json_get_string(init_res, "protocolVersion") << "\n";

            // 2. Notification initialized
            server->write_line("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");

            // 3. List Tools
            auto tools_res = rpc_call(*server, "tools/list", JsonValue::make_object({}), JsonValue::make_int(2));
            const auto* tools_arr = json_get(tools_res, "tools");
            std::cout << "[+] Available MCP tools: ";
            if (tools_arr && tools_arr->kind == JsonValue::Kind::array) {
                for (std::size_t i = 0; i < tools_arr->array.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << json_get_string(tools_arr->array[i], "name");
                }
            }
            std::cout << "\n\n";

            // 4. Call gt_inspect
            std::cout << "[*] Executing MCP tool 'gt_inspect'...\n";
            auto insp_reply = rpc_call(*server, "tools/call", JsonValue::make_object({
                {"name", JsonValue::make_string("gt_inspect")},
                {"arguments", JsonValue::make_object({})}
            }), JsonValue::make_int(3));

            const auto* insp_content = json_get(insp_reply, "content");
            if (insp_content && insp_content->kind == JsonValue::Kind::array && !insp_content->array.empty()) {
                std::cout << "    " << json_get_string(insp_content->array[0], "text") << "\n";
            }

            // 5. Call gt_read
            if (target_pid < 0) {
                const Address health_addr = 0x300000010ULL; // Health in simulated game
                std::cout << "[*] Executing MCP tool 'gt_read' (Address: 0x" << std::hex << health_addr << std::dec << ", Size: 4)...\n";
                auto read_reply = rpc_call(*server, "tools/call", JsonValue::make_object({
                    {"name", JsonValue::make_string("gt_read")},
                    {"arguments", JsonValue::make_object({
                        {"address", JsonValue::make_int(static_cast<std::int64_t>(health_addr))},
                        {"size", JsonValue::make_int(4)}
                    })}
                }), JsonValue::make_int(4));

                const auto* read_content = json_get(read_reply, "content");
                if (read_content && read_content->kind == JsonValue::Kind::array && !read_content->array.empty()) {
                    std::cout << "    " << json_get_string(read_content->array[0], "text") << "\n";
                }
            } else {
                std::cout << "[*] Read-only Windows target attached. Pass verified memory address to inspect.\n";
            }

            // 6. Call gt_resolve
            if (target_pid < 0) {
                const Address base_ptr = 0x140100000ULL;
                std::cout << "[*] Executing MCP tool 'gt_resolve' (Pointer Chain: [0x" << std::hex << base_ptr << ", 0, 0x50])...\n" << std::dec;
                auto resolve_reply = rpc_call(*server, "tools/call", JsonValue::make_object({
                    {"name", JsonValue::make_string("gt_resolve")},
                    {"arguments", JsonValue::make_object({
                        {"base", JsonValue::make_int(static_cast<std::int64_t>(base_ptr))},
                        {"offsets", JsonValue::make_array({
                            JsonValue::make_int(0),
                            JsonValue::make_int(0x50)
                        })}
                    })}
                }), JsonValue::make_int(5));

                const auto* res_content = json_get(resolve_reply, "content");
                if (res_content && res_content->kind == JsonValue::Kind::array && !res_content->array.empty()) {
                    std::cout << "    " << json_get_string(res_content->array[0], "text") << "\n";
                }
            }

            // 7. Call gt_status
            std::cout << "[*] Executing MCP tool 'gt_status'...\n";
            auto status_reply = rpc_call(*server, "tools/call", JsonValue::make_object({
                {"name", JsonValue::make_string("gt_status")},
                {"arguments", JsonValue::make_object({})}
            }), JsonValue::make_int(6));

            const auto* st_content = json_get(status_reply, "content");
            if (st_content && st_content->kind == JsonValue::Kind::array && !st_content->array.empty()) {
                std::cout << "    " << json_get_string(st_content->array[0], "text") << "\n";
            }

        } else {
            std::cout << "========================================================\n";
            std::cout << "  GTLibCpp C++ Trainer: Direct JSON-RPC 2.0 Gateway     \n";
            std::cout << "========================================================\n\n";

            std::cout << "[*] Dispatching 'inspect'...\n";
            auto insp = rpc_call(*server, "inspect", JsonValue::make_object({}), JsonValue::make_int(1));
            std::cout << "    Reply: " << json_encode(insp) << "\n";

            if (target_pid < 0) {
                const Address health_addr = 0x300000010ULL;
                std::cout << "[*] Dispatching 'read' (Address: 0x" << std::hex << health_addr << std::dec << ", Size: 4)...\n";
                auto read_res = rpc_call(*server, "read", JsonValue::make_object({
                    {"address", JsonValue::make_int(static_cast<std::int64_t>(health_addr))},
                    {"size", JsonValue::make_int(4)}
                }), JsonValue::make_int(2));
                std::cout << "    Reply: " << json_encode(read_res) << "\n";

                const Address base_ptr = 0x140100000ULL;
                std::cout << "[*] Dispatching 'resolve' (Base: 0x" << std::hex << base_ptr << ", Offsets: [0, 0x50])...\n" << std::dec;
                auto resolve_res = rpc_call(*server, "resolve", JsonValue::make_object({
                    {"base", JsonValue::make_int(static_cast<std::int64_t>(base_ptr))},
                    {"offsets", JsonValue::make_array({
                        JsonValue::make_int(0),
                        JsonValue::make_int(0x50)
                    })}
                }), JsonValue::make_int(3));
                std::cout << "    Reply: " << json_encode(resolve_res) << "\n";
            }

            std::cout << "[*] Dispatching 'status'...\n";
            auto st = rpc_call(*server, "status", JsonValue::make_object({}), JsonValue::make_int(4));
            std::cout << "    Reply: " << json_encode(st) << "\n";
        }

        if (interactive) {
            std::cout << "\n--- Interactive C++ Trainer Shell (type 'help' or 'exit') ---\n";
            std::string line;
            int cmd_id = 100;
            while (std::cout << "gtlibcpp> " && std::getline(std::cin, line)) {
                if (line.empty()) continue;
                if (line == "exit" || line == "quit") break;
                if (line == "help") {
                    std::cout << "Commands:\n"
                              << "  inspect             - Inspect target identity\n"
                              << "  read <addr> <size>  - Read memory bytes\n"
                              << "  status              - Check freeze status\n"
                              << "  exit                - Exit shell\n";
                    continue;
                }
                std::istringstream iss(line);
                std::string cmd;
                iss >> cmd;
                if (cmd == "inspect") {
                    if (mode == "mcp") {
                        auto r = rpc_call(*server, "tools/call", JsonValue::make_object({
                            {"name", JsonValue::make_string("gt_inspect")},
                            {"arguments", JsonValue::make_object({})}
                        }), JsonValue::make_int(++cmd_id));
                        std::cout << json_encode(r) << "\n";
                    } else {
                        auto r = rpc_call(*server, "inspect", JsonValue::make_object({}), JsonValue::make_int(++cmd_id));
                        std::cout << json_encode(r) << "\n";
                    }
                } else if (cmd == "status") {
                    if (mode == "mcp") {
                        auto r = rpc_call(*server, "tools/call", JsonValue::make_object({
                            {"name", JsonValue::make_string("gt_status")},
                            {"arguments", JsonValue::make_object({})}
                        }), JsonValue::make_int(++cmd_id));
                        std::cout << json_encode(r) << "\n";
                    } else {
                        auto r = rpc_call(*server, "status", JsonValue::make_object({}), JsonValue::make_int(++cmd_id));
                        std::cout << json_encode(r) << "\n";
                    }
                } else if (cmd == "read") {
                    std::string addr_str;
                    std::size_t size = 4;
                    iss >> addr_str >> size;
                    Address addr = 0;
                    if (addr_str.rfind("0x", 0) == 0 || addr_str.rfind("0X", 0) == 0) {
                        addr = std::stoull(addr_str, nullptr, 16);
                    } else {
                        addr = std::stoull(addr_str, nullptr, 10);
                    }
                    if (mode == "mcp") {
                        auto r = rpc_call(*server, "tools/call", JsonValue::make_object({
                            {"name", JsonValue::make_string("gt_read")},
                            {"arguments", JsonValue::make_object({
                                {"address", JsonValue::make_int(static_cast<std::int64_t>(addr))},
                                {"size", JsonValue::make_int(static_cast<std::int64_t>(size))}
                            })}
                        }), JsonValue::make_int(++cmd_id));
                        std::cout << json_encode(r) << "\n";
                    } else {
                        auto r = rpc_call(*server, "read", JsonValue::make_object({
                            {"address", JsonValue::make_int(static_cast<std::int64_t>(addr))},
                            {"size", JsonValue::make_int(static_cast<std::int64_t>(size))}
                        }), JsonValue::make_int(++cmd_id));
                        std::cout << json_encode(r) << "\n";
                    }
                } else {
                    std::cout << "Unknown command: " << cmd << ". Type 'help' for available commands.\n";
                }
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[-] Error: " << e.what() << "\n";
        server->close();
        return 1;
    }

    server->close();
    std::cout << "\n[+] C++ Trainer execution completed successfully.\n";
    return 0;
}
