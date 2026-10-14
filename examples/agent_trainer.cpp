// A runnable, read-only trainer agent gateway using a simulated game backend.
// MCP: newline-delimited JSON-RPC 2.0 on stdin/stdout.
// Direct mode: --json-rpc (GTLibCpp's existing JSON-RPC method names).
#include "gtlibcpp/agent.hpp"
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/parser.hpp"
#include "gtlibcpp/policy.hpp"
#include "simulated_game.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace gtlibcpp;
JsonValue field(std::string name, JsonValue value) {
    return JsonValue::make_object({{std::move(name), std::move(value)}});
}
JsonValue object(JsonObject fields) { return JsonValue::make_object(std::move(fields)); }
JsonValue rpc_result(const JsonValue& id, JsonValue result) {
    return object({{"jsonrpc", JsonValue::make_string("2.0")},
                   {"id", id}, {"result", std::move(result)}});
}
JsonValue rpc_error(const JsonValue& id, int code, const std::string& message) {
    return object({{"jsonrpc", JsonValue::make_string("2.0")}, {"id", id},
                   {"error", object({{"code", JsonValue::make_int(code)},
                                     {"message", JsonValue::make_string(message)}})}});
}
JsonValue schema(JsonObject properties, JsonArray required = {}) {
    return object({{"type", JsonValue::make_string("object")},
                   {"properties", object(std::move(properties))},
                   {"required", JsonValue::make_array(std::move(required))},
                   {"additionalProperties", JsonValue::make_bool(false)}});
}
JsonValue integer_field(const std::string& description) {
    return object({{"type", JsonValue::make_string("integer")},
                   {"minimum", JsonValue::make_int(0)},
                   {"description", JsonValue::make_string(description)}});
}
JsonValue tool(const std::string& name, const std::string& description, JsonValue input) {
    return object({{"name", JsonValue::make_string(name)},
                   {"description", JsonValue::make_string(description)},
                   {"inputSchema", std::move(input)}});
}
JsonValue list_tools() {
    JsonArray tools;
    tools.push_back(tool("gt_inspect", "Inspect the simulated game's target identity (read only).", schema({})));
    tools.push_back(tool("gt_read", "Read 1 to 256 bytes from simulated game memory.", schema({
        {"address", integer_field("Decimal nonnegative target address")},
        {"size", object({{"type", JsonValue::make_string("integer")},
                         {"minimum", JsonValue::make_int(1)}, {"maximum", JsonValue::make_int(256)}})}
    }, {JsonValue::make_string("address"), JsonValue::make_string("size")})));
    tools.push_back(tool("gt_resolve", "Resolve a target pointer chain (read only).", schema({
        {"base", integer_field("Base address")},
        {"offsets", object({{"type", JsonValue::make_string("array")},
                            {"items", integer_field("Pointer offset")},
                            {"minItems", JsonValue::make_int(1)},
                            {"maxItems", JsonValue::make_int(16)}})}
    }, {JsonValue::make_string("base"), JsonValue::make_string("offsets")})));
    tools.push_back(tool("gt_status", "Inspect the simulated trainer's freeze status (read only).", schema({})));
    return field("tools", JsonValue::make_array(std::move(tools)));
}
bool address(const JsonValue& obj, const std::string& key) {
    const auto* v = json_get(obj, key);
    return v && v->kind == JsonValue::Kind::integer && v->integer >= 0;
}
bool validate(const std::string& name, const JsonValue& args) {
    if (args.kind != JsonValue::Kind::object) return false;
    if (name == "gt_inspect" || name == "gt_status") return true;
    if (name == "gt_read") {
        return address(args, "address") && address(args, "size")
            && json_get_int(args, "size") >= 1 && json_get_int(args, "size") <= 256;
    }
    if (name == "gt_resolve") {
        const auto* arr = json_get(args, "offsets");
        if (!address(args, "base") || !arr || arr->kind != JsonValue::Kind::array
            || arr->array.empty() || arr->array.size() > 16) return false;
        for (const auto& off : arr->array)
            if (off.kind != JsonValue::Kind::integer || off.integer < 0) return false;
        return true;
    }
    return false;
}
JsonValue error_content(const std::string& message) {
    return object({{"content", JsonValue::make_array({
                        object({{"type", JsonValue::make_string("text")},
                                {"text", JsonValue::make_string(message)}})})},
                   {"isError", JsonValue::make_bool(true)}});
}
JsonValue tool_call(AgentService& agent, const JsonValue& params) {
    const auto name = json_get_string(params, "name");
    const auto* args = json_get(params, "arguments");
    JsonValue empty = object({});
    const auto& a = args ? *args : empty;
    const std::string method = name == "gt_inspect" ? "inspect"
                             : name == "gt_read" ? "read"
                             : name == "gt_resolve" ? "resolve"
                             : name == "gt_status" ? "status" : "";
    if (method.empty()) return error_content("Unknown or disabled tool: " + name);
    if (!validate(name, a)) return error_content("Invalid tool arguments");
    auto request = object({{"jsonrpc", JsonValue::make_string("2.0")},
                           {"id", JsonValue::make_string("mcp")},
                           {"method", JsonValue::make_string(method)},
                           {"params", a}});
    auto reply = agent.handle(json_encode(request));
    if (!reply) return error_content(reply.error().message);
    auto decoded = json_decode(reply.value());
    if (!decoded) return error_content("Internal JSON-RPC response decode failure");
    return object({{"content", JsonValue::make_array({
                       object({{"type", JsonValue::make_string("text")},
                               {"text", JsonValue::make_string(reply.value())}})})},
                   {"isError", JsonValue::make_bool(json_get(decoded.value(), "error") != nullptr)}});
}
} // namespace

int main(int argc, char** argv) {
    const bool direct_rpc = argc == 2 && std::string(argv[1]) == "--json-rpc";
    const bool self_test = argc == 2 && std::string(argv[1]) == "--self-test";
    if (argc > 1 && !direct_rpc && !self_test) {
        std::cerr << "Usage: gtlibcpp_agent_trainer [--json-rpc]\n";
        return 2;
    }
    // Demo-only process: no PID attachment, no writes, no elevated privileges.
    auto game = std::make_shared<gtlibcpp::demo::SimulatedGameProcess>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(game);
    auto policy = std::make_shared<gtlibcpp::Policy>(
        std::vector<gtlibcpp::TargetManifest>{});
    auto freeze = std::make_shared<gtlibcpp::FreezeManager>(session);
    auto parser = std::make_shared<gtlibcpp::CheatTableParser>();
    gtlibcpp::AgentService agent(session, policy, freeze, parser);

    if (self_test) {
        const auto list = list_tools();
        const auto* tools = gtlibcpp::json_get(list, "tools");
        if (!tools || tools->kind != gtlibcpp::JsonValue::Kind::array || tools->array.size() != 4)
            return 1;
        const auto inspected = tool_call(agent, object({
            {"name", gtlibcpp::JsonValue::make_string("gt_inspect")},
            {"arguments", object({})}
        }));
        if (gtlibcpp::json_get_bool(inspected, "isError", true)) return 1;
        const auto read = tool_call(agent, object({
            {"name", gtlibcpp::JsonValue::make_string("gt_read")},
            {"arguments", object({
                {"address", gtlibcpp::JsonValue::make_int(0x300000010LL)},
                {"size", gtlibcpp::JsonValue::make_int(4)}
            })}
        }));
        if (gtlibcpp::json_get_bool(read, "isError", true)) return 1;
        const auto blocked = tool_call(agent, object({
            {"name", gtlibcpp::JsonValue::make_string("gt_apply")},
            {"arguments", object({})}
        }));
        return gtlibcpp::json_get_bool(blocked, "isError", false) ? 0 : 1;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty() || line.size() > 65536) continue;
        auto req = gtlibcpp::json_decode(line);
        if (!req || req.value().kind != gtlibcpp::JsonValue::Kind::object) {
            std::cout << gtlibcpp::json_encode(rpc_error(gtlibcpp::JsonValue::make_null(),
                                                          -32700, "Parse error")) << '\n' << std::flush;
            continue;
        }
        const auto* id = gtlibcpp::json_get(req.value(), "id");
        if (!id) continue; // JSON-RPC notification: no response
        const auto method = gtlibcpp::json_get_string(req.value(), "method");
        const auto* params = gtlibcpp::json_get(req.value(), "params");
        gtlibcpp::JsonValue empty = object({});
        const auto& p = params ? *params : empty;
        gtlibcpp::JsonValue response;
        if (direct_rpc) {
            // Crucial: do not expose unsafe write/freeze/kill-switch handlers
            // through this agent-facing example while policy fixes are pending.
            const auto tool_name = method == "read" ? "gt_read"
                                 : method == "resolve" ? "gt_resolve"
                                 : method == "inspect" ? "gt_inspect"
                                 : method == "status" ? "gt_status" : "";
            if (tool_name.empty() || !validate(tool_name, p)) {
                response = rpc_error(*id, -32601, "Method unavailable in read-only trainer");
            } else {
                auto handled = agent.handle(line);
                if (!handled) response = rpc_error(*id, -32603, handled.error().message);
                else {
                    auto parsed = gtlibcpp::json_decode(handled.value());
                    response = parsed ? parsed.value()
                                      : rpc_error(*id, -32603, "Bad internal reply");
                }
            }
        } else if (method == "initialize") {
            const std::string version = "2025-03-26"; // Supported MCP protocol version
            response = rpc_result(*id, object({
                {"protocolVersion", gtlibcpp::JsonValue::make_string(version)},
                {"capabilities", object({{"tools", object({})}})},
                {"serverInfo", object({{"name", gtlibcpp::JsonValue::make_string("gtlibcpp-readonly-trainer")},
                                       {"version", gtlibcpp::JsonValue::make_string("1.0.0")}})}
            }));
        } else if (method == "ping") {
            response = rpc_result(*id, object({}));
        } else if (method == "tools/list") {
            response = rpc_result(*id, list_tools());
        } else if (method == "tools/call") {
            response = rpc_result(*id, tool_call(agent, p));
        } else {
            response = rpc_error(*id, -32601, "Method not found");
        }
        std::cout << gtlibcpp::json_encode(response) << '\n' << std::flush;
    }
    freeze->cancel_all();
    return 0;
}
