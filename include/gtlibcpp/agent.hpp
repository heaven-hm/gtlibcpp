#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "freeze.hpp"
#include "memory_session.hpp"
#include "parser.hpp"
#include "policy.hpp"
#include "result.hpp"
#include "types.hpp"

namespace gtlibcpp {

class IAgentTransport {
public:
    virtual ~IAgentTransport() = default;
    [[nodiscard]] virtual Result<void> send(const std::string& json) = 0;
    [[nodiscard]] virtual Result<std::string> receive() = 0;
    virtual void close() = 0;
};

struct JsonValue;
using JsonArray  = std::vector<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

struct JsonValue {
    enum class Kind { null, boolean, integer, real, string, array, object } kind{Kind::null};
    bool        boolean{false};
    std::int64_t integer{0};
    double      real{0.0};
    std::string text{};
    JsonArray   array{};
    JsonObject  object{};

    [[nodiscard]] static JsonValue make_null() { return JsonValue{}; }
    [[nodiscard]] static JsonValue make_bool(bool v) {
        JsonValue j; j.kind = Kind::boolean; j.boolean = v; return j;
    }
    [[nodiscard]] static JsonValue make_int(std::int64_t v) {
        JsonValue j; j.kind = Kind::integer; j.integer = v; return j;
    }
    [[nodiscard]] static JsonValue make_real(double v) {
        JsonValue j; j.kind = Kind::real; j.real = v; return j;
    }
    [[nodiscard]] static JsonValue make_string(std::string v) {
        JsonValue j; j.kind = Kind::string; j.text = std::move(v); return j;
    }
    [[nodiscard]] static JsonValue make_array(JsonArray v) {
        JsonValue j; j.kind = Kind::array; j.array = std::move(v); return j;
    }
    [[nodiscard]] static JsonValue make_object(JsonObject v) {
        JsonValue j; j.kind = Kind::object; j.object = std::move(v); return j;
    }
};

[[nodiscard]] std::string json_encode(const JsonValue& v);
[[nodiscard]] Result<JsonValue> json_decode(const std::string& text);

[[nodiscard]] const JsonValue* json_get(const JsonValue& obj, const std::string& key);
[[nodiscard]] std::string       json_get_string(const JsonValue& obj, const std::string& key, const std::string& fallback = {});
[[nodiscard]] std::int64_t      json_get_int(const JsonValue& obj, const std::string& key, std::int64_t fallback = 0);
[[nodiscard]] bool              json_get_bool(const JsonValue& obj, const std::string& key, bool fallback = false);

using PendingApproval = ApprovalToken;

class AgentService {
public:
    static constexpr const char* kProtocolVersion = "gtlibcpp.agent/1.0";

    AgentService(std::shared_ptr<MemorySession> session,
                 std::shared_ptr<Policy> policy,
                 std::shared_ptr<FreezeManager> freeze,
                 std::shared_ptr<CheatTableParser> parser);
    ~AgentService();

    AgentService(const AgentService&) = delete;
    AgentService& operator=(const AgentService&) = delete;
    AgentService(AgentService&&) = delete;
    AgentService& operator=(AgentService&&) = delete;

    [[nodiscard]] Result<std::string> handle(const std::string& request_json);

    void serve(std::shared_ptr<IAgentTransport> transport);
    void stop();

    [[nodiscard]] static JsonValue envelope(const std::string& method,
                                            const JsonValue& params);

private:
    [[nodiscard]] JsonValue on_inspect(const JsonValue& params);
    [[nodiscard]] JsonValue on_read(const JsonValue& params);
    [[nodiscard]] JsonValue on_resolve(const JsonValue& params);
    [[nodiscard]] JsonValue on_preview(const JsonValue& params);
    [[nodiscard]] JsonValue on_apply(const JsonValue& params);
    [[nodiscard]] JsonValue on_verify(const JsonValue& params);
    [[nodiscard]] JsonValue on_restore(const JsonValue& params);
    [[nodiscard]] JsonValue on_freeze(const JsonValue& params);
    [[nodiscard]] JsonValue on_unfreeze(const JsonValue& params);
    [[nodiscard]] JsonValue on_status(const JsonValue& params);
    [[nodiscard]] JsonValue on_parse(const JsonValue& params);
    [[nodiscard]] JsonValue on_kill_switch(const JsonValue& params);

    [[nodiscard]] JsonValue error_response(const std::string& request_id,
                                           std::int32_t code,
                                           const std::string& message,
                                           const Error* underlying = nullptr) const;

    std::shared_ptr<MemorySession>    session_;
    std::shared_ptr<Policy>           policy_;
    std::shared_ptr<FreezeManager>    freeze_;
    std::shared_ptr<CheatTableParser> parser_;
    std::mutex                        pending_mutex_;
    std::unordered_map<std::string, PendingApproval> pending_;
    std::atomic<bool>                 stop_flag_{false};
    std::thread                       server_thread_{};
};

} // namespace gtlibcpp
