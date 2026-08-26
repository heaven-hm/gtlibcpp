// AgentService implementation + a hand-rolled JSON encoder/decoder. The
// encoder produces canonical JSON; the decoder accepts the small
// subset the agent needs (objects, arrays, strings, numbers, booleans,
// null).
#include "gtlibcpp/agent.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <utility>

namespace gtlibcpp {

namespace {

std::int64_t now_ms() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

std::string hex_encode_lower(const std::uint8_t* p, std::size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.resize(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out[2 * i]     = kHex[(p[i] >> 4) & 0xF];
        out[2 * i + 1] = kHex[p[i] & 0xF];
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------
// JSON encode / decode
// ---------------------------------------------------------------------

std::string json_encode(const JsonValue& v) {
    std::ostringstream out;
    std::function<void(const JsonValue&)> write = [&](const JsonValue& x) {
        switch (x.kind) {
            case JsonValue::Kind::null:    out << "null"; break;
            case JsonValue::Kind::boolean: out << (x.boolean ? "true" : "false"); break;
            case JsonValue::Kind::integer: out << x.integer; break;
            case JsonValue::Kind::real:    out << x.real; break;
            case JsonValue::Kind::string: {
                out << '"';
                for (char c : x.text) {
                    switch (c) {
                        case '"':  out << "\\\""; break;
                        case '\\': out << "\\\\"; break;
                        case '\n': out << "\\n"; break;
                        case '\r': out << "\\r"; break;
                        case '\t': out << "\\t"; break;
                        default:
                            if (static_cast<unsigned char>(c) < 0x20) {
                                char buf[8];
                                std::snprintf(buf, sizeof(buf), "\\u%04x",
                                              static_cast<unsigned>(c));
                                out << buf;
                            } else {
                                out << c;
                            }
                    }
                }
                out << '"';
                break;
            }
            case JsonValue::Kind::array: {
                out << '[';
                for (std::size_t i = 0; i < x.array.size(); ++i) {
                    if (i) out << ',';
                    write(x.array[i]);
                }
                out << ']';
                break;
            }
            case JsonValue::Kind::object: {
                out << '{';
                for (std::size_t i = 0; i < x.object.size(); ++i) {
                    if (i) out << ',';
                    out << '"' << x.object[i].first << "\":";
                    write(x.object[i].second);
                }
                out << '}';
                break;
            }
        }
    };
    write(v);
    return out.str();
}

namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : src_(text), pos_(0) {}

    [[nodiscard]] Result<JsonValue> parse() {
        skip_ws();
        auto v = parse_value();
        if (!v) return v;
        skip_ws();
        if (pos_ < src_.size()) {
            return Result<JsonValue>::failure(make_error(
                ErrorCode::parse_failed,
                "trailing content at offset " + std::to_string(pos_),
                "JsonParser::parse"));
        }
        return v;
    }

private:
    void skip_ws() {
        while (pos_ < src_.size()
               && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }
    [[nodiscard]] Result<JsonValue> parse_value() {
        skip_ws();
        if (pos_ >= src_.size()) {
            return Result<JsonValue>::failure(make_error(
                ErrorCode::parse_failed, "unexpected end of input",
                "JsonParser::parse_value"));
        }
        const char c = src_[pos_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string_value();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        return parse_number();
    }
    [[nodiscard]] Result<JsonValue> parse_object() {
        JsonObject obj;
        ++pos_;
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == '}') { ++pos_; return Result<JsonValue>::success(JsonValue::make_object(std::move(obj))); }
        while (true) {
            skip_ws();
            auto key = parse_string_value();
            if (!key) return Result<JsonValue>::failure(key.error());
            skip_ws();
            if (pos_ >= src_.size() || src_[pos_] != ':') {
                return Result<JsonValue>::failure(make_error(
                    ErrorCode::parse_failed, "expected ':' after object key",
                    "JsonParser::parse_object"));
            }
            ++pos_;
            auto value = parse_value();
            if (!value) return Result<JsonValue>::failure(value.error());
            obj.emplace_back(std::move(key).value().text, std::move(value).value());
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < src_.size() && src_[pos_] == '}') { ++pos_; break; }
            return Result<JsonValue>::failure(make_error(
                ErrorCode::parse_failed, "expected ',' or '}' in object",
                "JsonParser::parse_object"));
        }
        return Result<JsonValue>::success(JsonValue::make_object(std::move(obj)));
    }
    [[nodiscard]] Result<JsonValue> parse_array() {
        JsonArray arr;
        ++pos_;
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == ']') { ++pos_; return Result<JsonValue>::success(JsonValue::make_array(std::move(arr))); }
        while (true) {
            auto v = parse_value();
            if (!v) return Result<JsonValue>::failure(v.error());
            arr.push_back(std::move(v).value());
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < src_.size() && src_[pos_] == ']') { ++pos_; break; }
            return Result<JsonValue>::failure(make_error(
                ErrorCode::parse_failed, "expected ',' or ']' in array",
                "JsonParser::parse_array"));
        }
        return Result<JsonValue>::success(JsonValue::make_array(std::move(arr)));
    }
    [[nodiscard]] Result<JsonValue> parse_string_value() {
        if (pos_ >= src_.size() || src_[pos_] != '"') {
            return Result<JsonValue>::failure(make_error(
                ErrorCode::parse_failed, "expected '\"' to start string",
                "JsonParser::parse_string_value"));
        }
        ++pos_;
        std::string out;
        while (pos_ < src_.size() && src_[pos_] != '"') {
            char c = src_[pos_++];
            if (c == '\\' && pos_ < src_.size()) {
                char esc = src_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    default:
                        return Result<JsonValue>::failure(make_error(
                            ErrorCode::parse_failed,
                            "unsupported escape",
                            "JsonParser::parse_string_value"));
                }
            } else {
                out.push_back(c);
            }
        }
        if (pos_ >= src_.size()) {
            return Result<JsonValue>::failure(make_error(
                ErrorCode::parse_failed, "unterminated string",
                "JsonParser::parse_string_value"));
        }
        ++pos_;
        return Result<JsonValue>::success(JsonValue::make_string(std::move(out)));
    }
    [[nodiscard]] Result<JsonValue> parse_bool() {
        if (pos_ + 4 <= src_.size() && src_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return Result<JsonValue>::success(JsonValue::make_bool(true));
        }
        if (pos_ + 5 <= src_.size() && src_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return Result<JsonValue>::success(JsonValue::make_bool(false));
        }
        return Result<JsonValue>::failure(make_error(
            ErrorCode::parse_failed, "expected boolean literal",
            "JsonParser::parse_bool"));
    }
    [[nodiscard]] Result<JsonValue> parse_null() {
        if (pos_ + 4 <= src_.size() && src_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return Result<JsonValue>::success(JsonValue::make_null());
        }
        return Result<JsonValue>::failure(make_error(
            ErrorCode::parse_failed, "expected null literal",
            "JsonParser::parse_null"));
    }
    [[nodiscard]] Result<JsonValue> parse_number() {
        const std::size_t start = pos_;
        if (src_[pos_] == '-' || src_[pos_] == '+') ++pos_;
        bool seen_dot = false;
        bool seen_exp = false;
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (std::isdigit(static_cast<unsigned char>(c))) { ++pos_; continue; }
            if (c == '.' && !seen_dot) { seen_dot = true; ++pos_; continue; }
            if ((c == 'e' || c == 'E') && !seen_exp) {
                seen_exp = true;
                ++pos_;
                if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
                continue;
            }
            break;
        }
        if (start == pos_) {
            return Result<JsonValue>::failure(make_error(
                ErrorCode::parse_failed, "expected number",
                "JsonParser::parse_number"));
        }
        const std::string token = src_.substr(start, pos_ - start);
        try {
            if (seen_dot || seen_exp) {
                return Result<JsonValue>::success(
                    JsonValue::make_real(std::stod(token)));
            }
            return Result<JsonValue>::success(
                JsonValue::make_int(static_cast<std::int64_t>(std::stoll(token))));
        } catch (...) {
            return Result<JsonValue>::failure(make_error(
                ErrorCode::parse_failed, "could not parse number " + token,
                "JsonParser::parse_number"));
        }
    }

    const std::string& src_;
    std::size_t pos_;
};

} // namespace

Result<JsonValue> json_decode(const std::string& text) {
    JsonParser p(text);
    return p.parse();
}

const JsonValue* json_get(const JsonValue& obj, const std::string& key) {
    if (obj.kind != JsonValue::Kind::object) return nullptr;
    for (const auto& kv : obj.object) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}
std::string json_get_string(const JsonValue& obj, const std::string& key, const std::string& fallback) {
    const JsonValue* v = json_get(obj, key);
    if (!v || v->kind != JsonValue::Kind::string) return fallback;
    return v->text;
}
std::int64_t json_get_int(const JsonValue& obj, const std::string& key, std::int64_t fallback) {
    const JsonValue* v = json_get(obj, key);
    if (!v) return fallback;
    if (v->kind == JsonValue::Kind::integer) return v->integer;
    if (v->kind == JsonValue::Kind::real) return static_cast<std::int64_t>(v->real);
    return fallback;
}
bool json_get_bool(const JsonValue& obj, const std::string& key, bool fallback) {
    const JsonValue* v = json_get(obj, key);
    if (!v) return fallback;
    if (v->kind == JsonValue::Kind::boolean) return v->boolean;
    return fallback;
}

// ---------------------------------------------------------------------
// AgentService
// ---------------------------------------------------------------------

AgentService::AgentService(std::shared_ptr<MemorySession> session,
                           std::shared_ptr<Policy> policy,
                           std::shared_ptr<FreezeManager> freeze,
                           std::shared_ptr<CheatTableParser> parser)
    : session_(std::move(session)),
      policy_(std::move(policy)),
      freeze_(std::move(freeze)),
      parser_(std::move(parser)) {}

AgentService::~AgentService() { stop(); }

JsonValue AgentService::envelope(const std::string& method, const JsonValue& params) {
    JsonObject obj;
    obj.emplace_back("jsonrpc", JsonValue::make_string("2.0"));
    obj.emplace_back("method", JsonValue::make_string(method));
    obj.emplace_back("params", params);
    return JsonValue::make_object(std::move(obj));
}

JsonValue AgentService::error_response(const std::string& request_id,
                                       std::int32_t code,
                                       const std::string& message,
                                       const Error* underlying) const {
    JsonObject err;
    err.emplace_back("code", JsonValue::make_int(code));
    err.emplace_back("message", JsonValue::make_string(message));
    if (underlying) {
        err.emplace_back("data", JsonValue::make_string(to_string(underlying->code)));
    }
    JsonObject resp;
    resp.emplace_back("jsonrpc", JsonValue::make_string("2.0"));
    resp.emplace_back("id", JsonValue::make_string(request_id));
    resp.emplace_back("error", JsonValue::make_object(std::move(err)));
    return JsonValue::make_object(std::move(resp));
}

static JsonValue build_capability_response(const MemorySession& session) {
    JsonObject target;
    const auto& id = session.identity();
    target.emplace_back("pid", JsonValue::make_int(id.pid));
    target.emplace_back("image_path", JsonValue::make_string(id.image_path));
    target.emplace_back("image_sha256", JsonValue::make_string(id.image_sha256));
    target.emplace_back("architecture", JsonValue::make_string(to_string(id.architecture)));
    target.emplace_back("start_time", JsonValue::make_int(static_cast<std::int64_t>(id.start_time)));
    JsonObject caps;
    caps.emplace_back("read", JsonValue::make_bool(true));
    caps.emplace_back("write", JsonValue::make_bool(false));
    caps.emplace_back("freeze", JsonValue::make_bool(false));
    caps.emplace_back("rollback", JsonValue::make_bool(false));
    JsonObject root;
    root.emplace_back("target", JsonValue::make_object(std::move(target)));
    root.emplace_back("capabilities", JsonValue::make_object(std::move(caps)));
    return JsonValue::make_object(std::move(root));
}

static std::string hex_bytes(const std::vector<std::uint8_t>& v) {
    return hex_encode_lower(v.data(), v.size());
}

JsonValue AgentService::on_inspect(const JsonValue& /*params*/) {
    return build_capability_response(*session_);
}

static JsonValue make_envelope(const std::string& request_id, JsonValue result) {
    JsonObject resp;
    resp.emplace_back("jsonrpc", JsonValue::make_string("2.0"));
    resp.emplace_back("id", JsonValue::make_string(request_id));
    resp.emplace_back("result", std::move(result));
    return JsonValue::make_object(std::move(resp));
}

static JsonValue make_error_envelope(const std::string& request_id, JsonValue error) {
    JsonObject resp;
    resp.emplace_back("jsonrpc", JsonValue::make_string("2.0"));
    resp.emplace_back("id", JsonValue::make_string(request_id));
    resp.emplace_back("error", std::move(error));
    return JsonValue::make_object(std::move(resp));
}

JsonValue AgentService::on_read(const JsonValue& params) {
    const Address address = static_cast<Address>(json_get_int(params, "address"));
    const std::size_t size = static_cast<std::size_t>(json_get_int(params, "size", 4));
    if (!session_->is_alive()) {
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(ErrorCode::target_dead),
                              "target is not alive");
    }
    auto bytes = session_->read_bytes(address, size);
    if (!bytes) {
        const Error& e = bytes.error();
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(e.code), e.message, &e);
    }
    JsonObject root;
    root.emplace_back("address", JsonValue::make_int(static_cast<std::int64_t>(address)));
    root.emplace_back("size", JsonValue::make_int(static_cast<std::int64_t>(bytes.value().size())));
    root.emplace_back("hex", JsonValue::make_string(hex_bytes(bytes.value())));
    return JsonValue::make_object(std::move(root));
}

JsonValue AgentService::on_resolve(const JsonValue& params) {
    const Address base = static_cast<Address>(json_get_int(params, "base"));
    const JsonValue* offsets_node = json_get(params, "offsets");
    std::vector<Address> offsets;
    if (offsets_node && offsets_node->kind == JsonValue::Kind::array) {
        for (const auto& v : offsets_node->array) {
            if (v.kind == JsonValue::Kind::integer) {
                offsets.push_back(static_cast<Address>(v.integer));
            }
        }
    }
    auto r = session_->resolve_pointer_chain(base, offsets);
    if (!r) {
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(r.error().code),
                              r.error().message, &r.error());
    }
    JsonObject root;
    root.emplace_back("address", JsonValue::make_int(static_cast<std::int64_t>(r.value())));
    return JsonValue::make_object(std::move(root));
}

static MutationRequest make_request(const JsonValue& params, const std::string& method) {
    MutationRequest r;
    r.request_id = json_get_string(params, "request_id");
    r.target_alias = json_get_string(params, "target_alias");
    r.expected_current_hash = json_get_string(params, "expected_current_hash");
    r.value_repr = json_get_string(params, "value_repr", method);
    r.address = static_cast<Address>(json_get_int(params, "address"));
    r.size = static_cast<std::size_t>(json_get_int(params, "size", 0));
    r.preview = json_get_bool(params, "preview", false);
    r.approved = json_get_bool(params, "approved", false);
    const std::string cap = json_get_string(params, "capability", "read");
    if (cap == "read") r.capability = Capability::read;
    else if (cap == "write") r.capability = Capability::write;
    else if (cap == "freeze") r.capability = Capability::freeze;
    else if (cap == "unfreeze") r.capability = Capability::unfreeze;
    else if (cap == "rollback") r.capability = Capability::rollback;
    else if (cap == "parse") r.capability = Capability::parse;
    return r;
}

JsonValue AgentService::on_preview(const JsonValue& params) {
    auto request = make_request(params, "preview");
    request.preview = true;
    auto auth = policy_->authorize(session_->identity(), request);
    if (!auth) {
        return error_response(request.request_id,
                              static_cast<std::int32_t>(auth.error().code),
                              auth.error().message, &auth.error());
    }
    PendingApproval pa;
    pa.request_id = request.request_id;
    pa.target_alias = request.target_alias;
    pa.issued_at_ms = now_ms();
    pa.expires_at_ms = pa.issued_at_ms + 5 * 60 * 1000;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_[request.request_id] = pa;
    }
    JsonObject root;
    root.emplace_back("approval_token", JsonValue::make_string(request.request_id));
    root.emplace_back("issued_at_ms", JsonValue::make_int(pa.issued_at_ms));
    root.emplace_back("expires_at_ms", JsonValue::make_int(pa.expires_at_ms));
    root.emplace_back("target", JsonValue::make_string(request.target_alias));
    return JsonValue::make_object(std::move(root));
}

JsonValue AgentService::on_apply(const JsonValue& params) {
    auto request = make_request(params, "apply");
    PendingApproval token;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_.find(request.request_id);
        if (it == pending_.end()) {
            return error_response(request.request_id,
                                  static_cast<std::int32_t>(ErrorCode::policy_denied),
                                  "no pending approval");
        }
        token = it->second;
        pending_.erase(it);
    }
    auto auth = policy_->authorize_with_token(session_->identity(), request, &token);
    if (!auth) {
        return error_response(request.request_id,
                              static_cast<std::int32_t>(auth.error().code),
                              auth.error().message, &auth.error());
    }
    auto bytes_str = json_get_string(params, "value_hex");
    std::vector<std::uint8_t> value_bytes;
    if (!bytes_str.empty()) {
        if (bytes_str.size() % 2 != 0) {
            return error_response(request.request_id,
                                  static_cast<std::int32_t>(ErrorCode::invalid_string),
                                  "value_hex must have an even number of digits");
        }
        for (std::size_t i = 0; i < bytes_str.size(); i += 2) {
            char buf[3] = {bytes_str[i], bytes_str[i + 1], 0};
            value_bytes.push_back(static_cast<std::uint8_t>(std::stoul(buf, nullptr, 16)));
        }
    }
    auto w = session_->write_bytes(request.address, value_bytes);
    if (!w) {
        return error_response(request.request_id,
                              static_cast<std::int32_t>(w.error().code),
                              w.error().message, &w.error());
    }
    if (w.value() != value_bytes.size()) {
        return error_response(request.request_id,
                              static_cast<std::int32_t>(ErrorCode::partial_write),
                              "partial write");
    }
    JsonObject root;
    root.emplace_back("bytes_written", JsonValue::make_int(static_cast<std::int64_t>(w.value())));
    root.emplace_back("address", JsonValue::make_int(static_cast<std::int64_t>(request.address)));
    return JsonValue::make_object(std::move(root));
}

JsonValue AgentService::on_verify(const JsonValue& params) {
    const Address address = static_cast<Address>(json_get_int(params, "address"));
    const std::size_t size = static_cast<std::size_t>(json_get_int(params, "size", 4));
    auto read_back = session_->read_bytes(address, size);
    if (!read_back) {
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(read_back.error().code),
                              read_back.error().message, &read_back.error());
    }
    JsonObject root;
    root.emplace_back("hex", JsonValue::make_string(hex_bytes(read_back.value())));
    return JsonValue::make_object(std::move(root));
}

JsonValue AgentService::on_restore(const JsonValue& params) {
    const std::string id = json_get_string(params, "freeze_id");
    if (id.empty()) {
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(ErrorCode::invalid_entry_id),
                              "freeze_id is required");
    }
    auto r = freeze_->restore(id);
    if (!r) {
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(r.error().code),
                              r.error().message, &r.error());
    }
    JsonObject root;
    root.emplace_back("restored", JsonValue::make_bool(true));
    return JsonValue::make_object(std::move(root));
}

JsonValue AgentService::on_freeze(const JsonValue& params) {
    FreezeRequest fr;
    fr.id = json_get_string(params, "id");
    fr.address = static_cast<Address>(json_get_int(params, "address"));
    fr.type_name = json_get_string(params, "type", "uint32");
    fr.value_u64 = static_cast<std::uint64_t>(json_get_int(params, "value"));
    fr.size = static_cast<std::size_t>(json_get_int(params, "size", 0));
    const std::int64_t interval = json_get_int(params, "interval_ms", 50);
    fr.interval = std::chrono::milliseconds(interval);
    auto r = freeze_->freeze(fr);
    if (!r) {
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(r.error().code),
                              r.error().message, &r.error());
    }
    JsonObject root;
    root.emplace_back("active", JsonValue::make_bool(true));
    root.emplace_back("id", JsonValue::make_string(fr.id));
    return JsonValue::make_object(std::move(root));
}

JsonValue AgentService::on_unfreeze(const JsonValue& params) {
    const std::string id = json_get_string(params, "id");
    auto r = freeze_->unfreeze(id);
    if (!r) {
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(r.error().code),
                              r.error().message, &r.error());
    }
    JsonObject root;
    root.emplace_back("unfrozen", JsonValue::make_bool(true));
    return JsonValue::make_object(std::move(root));
}

JsonValue AgentService::on_status(const JsonValue& params) {
    const std::string id = json_get_string(params, "id");
    if (id.empty()) {
        JsonArray list;
        for (const auto& s : freeze_->all_status()) {
            JsonObject entry;
            entry.emplace_back("id", JsonValue::make_string(s.id));
            entry.emplace_back("active", JsonValue::make_bool(s.active));
            entry.emplace_back("successful_rewrites", JsonValue::make_int(static_cast<std::int64_t>(s.successful_rewrites)));
            entry.emplace_back("failed_rewrites", JsonValue::make_int(static_cast<std::int64_t>(s.failed_rewrites)));
            list.push_back(JsonValue::make_object(std::move(entry)));
        }
        JsonObject root;
        root.emplace_back("freezes", JsonValue::make_array(std::move(list)));
        return JsonValue::make_object(std::move(root));
    }
    auto s = freeze_->status(id);
    if (!s) {
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(s.error().code),
                              s.error().message, &s.error());
    }
    JsonObject entry;
    entry.emplace_back("id", JsonValue::make_string(s.value().id));
    entry.emplace_back("active", JsonValue::make_bool(s.value().active));
    entry.emplace_back("successful_rewrites", JsonValue::make_int(static_cast<std::int64_t>(s.value().successful_rewrites)));
    entry.emplace_back("failed_rewrites", JsonValue::make_int(static_cast<std::int64_t>(s.value().failed_rewrites)));
    JsonObject root;
    root.emplace_back("freeze", JsonValue::make_object(std::move(entry)));
    return JsonValue::make_object(std::move(root));
}

JsonValue AgentService::on_parse(const JsonValue& params) {
    const std::string xml = json_get_string(params, "xml");
    auto r = parser_->parse_string(xml);
    if (!r) {
        return error_response(json_get_string(params, "request_id"),
                              static_cast<std::int32_t>(r.error().code),
                              r.error().message, &r.error());
    }
    JsonArray supported;
    for (const auto& e : r.value().entries) {
        JsonObject entry;
        entry.emplace_back("id", JsonValue::make_string(e.id.value));
        entry.emplace_back("description", JsonValue::make_string(e.description));
        entry.emplace_back("address", JsonValue::make_int(static_cast<std::int64_t>(e.address)));
        entry.emplace_back("type", JsonValue::make_string(CheatTableParser::to_string(e.type)));
        entry.emplace_back("is_signed", JsonValue::make_bool(e.is_signed));
        JsonArray hk_array;
        for (int hk : e.hotkeys) {
            hk_array.push_back(JsonValue::make_int(hk));
        }
        entry.emplace_back("hotkeys", JsonValue::make_array(std::move(hk_array)));
        supported.push_back(JsonValue::make_object(std::move(entry)));
    }
    JsonArray unsupported;
    for (const auto& e : r.value().unsupported_entries) {
        JsonObject entry;
        entry.emplace_back("id", JsonValue::make_string(e.id.value));
        entry.emplace_back("reason", JsonValue::make_string(e.failure_reason));
        unsupported.push_back(JsonValue::make_object(std::move(entry)));
    }
    JsonArray diagnostics;
    for (const auto& d : r.value().diagnostics) {
        diagnostics.push_back(JsonValue::make_string(d));
    }
    JsonObject root;
    root.emplace_back("supported", JsonValue::make_array(std::move(supported)));
    root.emplace_back("unsupported", JsonValue::make_array(std::move(unsupported)));
    root.emplace_back("diagnostics", JsonValue::make_array(std::move(diagnostics)));
    return JsonValue::make_object(std::move(root));
}

JsonValue AgentService::on_kill_switch(const JsonValue& params) {
    const bool engage = json_get_bool(params, "engage", true);
    if (engage) {
        policy_->set_kill_switch_reason(json_get_string(params, "reason", "operator"));
    } else {
        const_cast<KillSwitch&>(policy_->kill_switch()).release();
    }
    JsonObject root;
    root.emplace_back("engaged", JsonValue::make_bool(policy_->kill_switch().engaged()));
    return JsonValue::make_object(std::move(root));
}

Result<std::string> AgentService::handle(const std::string& request_json) {
    auto parsed = json_decode(request_json);
    if (!parsed) {
        JsonObject err;
        err.emplace_back("code", JsonValue::make_int(-32700));
        err.emplace_back("message", JsonValue::make_string("parse error"));
        JsonValue err_val = JsonValue::make_object(std::move(err));
        return Result<std::string>::success(json_encode(
            make_error_envelope("", std::move(err_val))));
    }
    const JsonValue& req = parsed.value();
    if (req.kind != JsonValue::Kind::object) {
        JsonObject err;
        err.emplace_back("code", JsonValue::make_int(-32600));
        err.emplace_back("message", JsonValue::make_string("invalid request"));
        return Result<std::string>::success(json_encode(
            make_error_envelope("", JsonValue::make_object(std::move(err)))));
    }
    const std::string method = json_get_string(req, "method");
    const std::string request_id = json_get_string(req, "id");
    const JsonValue* params = json_get(req, "params");
    JsonValue empty_params = JsonValue::make_object({});
    const JsonValue& p = params ? *params : empty_params;
    JsonValue result;
    JsonValue error;
    bool is_error = false;
    auto call = [&](JsonValue r) { result = std::move(r); };
    auto fail = [&](JsonValue e) { error = std::move(e); is_error = true; };
    if (method == "inspect") call(on_inspect(p));
    else if (method == "read") call(on_read(p));
    else if (method == "resolve") call(on_resolve(p));
    else if (method == "preview") call(on_preview(p));
    else if (method == "apply") call(on_apply(p));
    else if (method == "verify") call(on_verify(p));
    else if (method == "restore") call(on_restore(p));
    else if (method == "freeze") call(on_freeze(p));
    else if (method == "unfreeze") call(on_unfreeze(p));
    else if (method == "status") call(on_status(p));
    else if (method == "parse") call(on_parse(p));
    else if (method == "kill_switch") call(on_kill_switch(p));
    else {
        JsonObject err;
        err.emplace_back("code", JsonValue::make_int(-32601));
        err.emplace_back("message", JsonValue::make_string("method not found: " + method));
        fail(JsonValue::make_object(std::move(err)));
    }
    if (is_error) {
        return Result<std::string>::success(json_encode(
            make_error_envelope(request_id, std::move(error))));
    }
    // If the result already has a "jsonrpc" field, treat it as a
    // pre-wrapped response and only set the id.
    bool already_wrapped = false;
    if (result.kind == JsonValue::Kind::object) {
        for (const auto& kv : result.object) {
            if (kv.first == "jsonrpc") { already_wrapped = true; break; }
        }
    }
    if (already_wrapped) {
        for (auto& kv : result.object) {
            if (kv.first == "id") { kv.second = JsonValue::make_string(request_id); break; }
        }
        return Result<std::string>::success(json_encode(std::move(result)));
    }
    return Result<std::string>::success(json_encode(
        make_envelope(request_id, std::move(result))));
}

void AgentService::serve(std::shared_ptr<IAgentTransport> transport) {
    stop_flag_.store(false);
    server_thread_ = std::thread([this, transport]() {
        while (!stop_flag_.load()) {
            auto request = transport->receive();
            if (!request) {
                if (stop_flag_.load()) return;
                continue;
            }
            auto response = handle(request.value());
            if (!response) continue;
            auto send = transport->send(response.value());
            (void)send;
        }
    });
}

void AgentService::stop() {
    stop_flag_.store(true);
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

} // namespace gtlibcpp
