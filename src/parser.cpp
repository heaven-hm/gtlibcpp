#include "gtlibcpp/parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace gtlibcpp {

namespace {

struct XmlNode {
    std::string name;
    std::string text;
    std::map<std::string, std::string> attrs;
    std::vector<XmlNode> children;
};

class XmlReader {
public:
    explicit XmlReader(const std::string& input) : src_(input), pos_(0) {}

    [[nodiscard]] Result<XmlNode> parse_root() {
        skip_whitespace();
        skip_bom();
        while (!done() && pos_ + 1 < src_.size() && src_[pos_] == '<'
               && src_[pos_ + 1] == '?') {
            pos_ += 2;
            while (!done() && pos_ + 1 < src_.size()
                   && !(src_[pos_] == '?' && src_[pos_ + 1] == '>')) {
                ++pos_;
            }
            if (pos_ + 1 < src_.size()) pos_ += 2;
            skip_whitespace_and_comments();
        }
        if (done() || src_[pos_] != '<') {
            return Result<XmlNode>::failure(make_error(
                ErrorCode::parse_failed,
                "document does not begin with an element",
                "XmlReader::parse_root"));
        }
        auto root = parse_element();
        if (!root) return Result<XmlNode>::failure(root.error());
        skip_whitespace_and_comments();
        if (!done()) {
            return Result<XmlNode>::failure(make_error(
                ErrorCode::parse_failed,
                "trailing content after the root element",
                "XmlReader::parse_root"));
        }
        return root;
    }

private:
    void skip_bom() {
        if (pos_ + 3 <= src_.size()
            && static_cast<unsigned char>(src_[pos_]) == 0xEF
            && static_cast<unsigned char>(src_[pos_ + 1]) == 0xBB
            && static_cast<unsigned char>(src_[pos_ + 2]) == 0xBF) {
            pos_ += 3;
        }
    }
    void skip_whitespace() {
        while (!done() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }
    void skip_whitespace_and_comments() {
        while (!done()) {
            skip_whitespace();
            if (pos_ + 4 <= src_.size()
                && src_[pos_] == '<'
                && src_[pos_ + 1] == '!') {
                if (src_[pos_ + 2] == '-' && src_[pos_ + 3] == '-') {
                    pos_ += 4;
                    while (pos_ + 3 <= src_.size()
                           && !(src_[pos_] == '-' && src_[pos_ + 1] == '-'
                                && src_[pos_ + 2] == '>')) {
                        ++pos_;
                    }
                    if (pos_ + 3 <= src_.size()) pos_ += 3;
                } else {
                    ++pos_;
                }
            } else {
                break;
            }
        }
    }
    [[nodiscard]] bool done() const noexcept { return pos_ >= src_.size(); }

    [[nodiscard]] Result<XmlNode> parse_element() {
        if (done() || src_[pos_] != '<') {
            return Result<XmlNode>::failure(make_error(
                ErrorCode::parse_failed,
                "expected '<' to start an element",
                "XmlReader::parse_element"));
        }
        ++pos_;
        if (!done() && src_[pos_] == '/') {
            return Result<XmlNode>::failure(make_error(
                ErrorCode::parse_failed,
                "unexpected closing tag without an open element",
                "XmlReader::parse_element"));
        }
        auto name = parse_name();
        if (!name) return Result<XmlNode>::failure(name.error());
        XmlNode node;
        node.name = name.value();
        auto attr = parse_attributes();
        if (!attr) return Result<XmlNode>::failure(attr.error());
        node.attrs = std::move(attr).value();
        skip_whitespace();
        if (!done() && src_[pos_] == '/') {
            ++pos_;
            if (done() || src_[pos_] != '>') {
                return Result<XmlNode>::failure(make_error(
                    ErrorCode::parse_failed,
                    "expected '>' to close a self-closing tag",
                    "XmlReader::parse_element"));
            }
            ++pos_;
            return Result<XmlNode>::success(std::move(node));
        }
        if (done() || src_[pos_] != '>') {
            return Result<XmlNode>::failure(make_error(
                ErrorCode::parse_failed,
                "expected '>' to close an open tag",
                "XmlReader::parse_element"));
        }
        ++pos_;
        node.text = parse_text_until_open_brace();
        while (!done()) {
            skip_whitespace_and_comments();
            if (done()) break;
            if (src_[pos_] != '<') {
                return Result<XmlNode>::failure(make_error(
                    ErrorCode::parse_failed,
                    "expected '<' to start a child or closing tag",
                    "XmlReader::parse_element"));
            }
            if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
                pos_ += 2;
                auto close_name = parse_name();
                if (!close_name) return Result<XmlNode>::failure(close_name.error());
                if (close_name.value() != node.name) {
                    return Result<XmlNode>::failure(make_error(
                        ErrorCode::parse_failed,
                        "mismatched closing tag: expected " + node.name
                            + " got " + close_name.value(),
                        "XmlReader::parse_element"));
                }
                skip_whitespace();
                if (done() || src_[pos_] != '>') {
                    return Result<XmlNode>::failure(make_error(
                        ErrorCode::parse_failed,
                        "expected '>' to close closing tag",
                        "XmlReader::parse_element"));
                }
                ++pos_;
                return Result<XmlNode>::success(std::move(node));
            }
            auto child = parse_element();
            if (!child) return Result<XmlNode>::failure(child.error());
            node.children.push_back(std::move(child).value());
        }
        return Result<XmlNode>::failure(make_error(
            ErrorCode::parse_failed,
            "unterminated element " + node.name,
            "XmlReader::parse_element"));
    }

    [[nodiscard]] Result<std::string> parse_name() {
        std::string out;
        while (!done()) {
            const char c = src_[pos_];
            if (std::isalnum(static_cast<unsigned char>(c))
                || c == '_' || c == '-' || c == ':' || c == '.') {
                out.push_back(c);
                ++pos_;
            } else {
                break;
            }
        }
        if (out.empty()) {
            return Result<std::string>::failure(make_error(
                ErrorCode::parse_failed,
                "expected an element name",
                "XmlReader::parse_name"));
        }
        return Result<std::string>::success(std::move(out));
    }

    [[nodiscard]] Result<std::map<std::string, std::string>>
    parse_attributes() {
        std::map<std::string, std::string> out;
        while (!done()) {
            skip_whitespace();
            if (done()) break;
            const char c = src_[pos_];
            if (c == '/' || c == '>') break;
            auto key = parse_name();
            if (!key) return Result<std::map<std::string, std::string>>::failure(key.error());
            skip_whitespace();
            if (done() || src_[pos_] != '=') {
                return Result<std::map<std::string, std::string>>::failure(make_error(
                    ErrorCode::parse_failed,
                    "expected '=' after attribute name " + key.value(),
                    "XmlReader::parse_attributes"));
            }
            ++pos_;
            skip_whitespace();
            if (done() || (src_[pos_] != '"' && src_[pos_] != '\'')) {
                return Result<std::map<std::string, std::string>>::failure(make_error(
                    ErrorCode::parse_failed,
                    "expected quoted attribute value",
                    "XmlReader::parse_attributes"));
            }
            const char quote = src_[pos_++];
            std::string value;
            while (!done() && src_[pos_] != quote) {
                if (src_[pos_] == '&') {
                    auto ent = parse_entity();
                    if (!ent) return Result<std::map<std::string, std::string>>::failure(ent.error());
                    value += ent.value();
                } else {
                    value.push_back(src_[pos_++]);
                }
            }
            if (done() || src_[pos_] != quote) {
                return Result<std::map<std::string, std::string>>::failure(make_error(
                    ErrorCode::parse_failed,
                    "unterminated attribute value",
                    "XmlReader::parse_attributes"));
            }
            ++pos_;
            out.emplace(key.value(), std::move(value));
        }
        return Result<std::map<std::string, std::string>>::success(std::move(out));
    }

    [[nodiscard]] Result<std::string> parse_entity() {
        if (pos_ + 2 < src_.size() && src_[pos_] == '&'
            && src_[pos_ + 1] == 'l' && src_[pos_ + 2] == 't') {
            pos_ += 3;
            if (!done() && src_[pos_] == ';') ++pos_;
            return Result<std::string>::success("<");
        }
        if (pos_ + 3 < src_.size() && src_[pos_] == '&'
            && src_[pos_ + 1] == 'g' && src_[pos_ + 2] == 't') {
            pos_ += 3;
            if (!done() && src_[pos_] == ';') ++pos_;
            return Result<std::string>::success(">");
        }
        if (pos_ + 4 < src_.size() && src_[pos_] == '&'
            && src_[pos_ + 1] == 'a' && src_[pos_ + 2] == 'm'
            && src_[pos_ + 3] == 'p') {
            pos_ += 4;
            if (!done() && src_[pos_] == ';') ++pos_;
            return Result<std::string>::success("&");
        }
        if (pos_ + 5 < src_.size() && src_[pos_] == '&'
            && src_[pos_ + 1] == 'q' && src_[pos_ + 2] == 'u'
            && src_[pos_ + 3] == 'o' && src_[pos_ + 4] == 't') {
            pos_ += 5;
            if (!done() && src_[pos_] == ';') ++pos_;
            return Result<std::string>::success("\"");
        }
        if (pos_ + 5 < src_.size() && src_[pos_] == '&'
            && src_[pos_ + 1] == 'a' && src_[pos_ + 2] == 'p'
            && src_[pos_ + 3] == 'o' && src_[pos_ + 4] == 's') {
            pos_ += 5;
            if (!done() && src_[pos_] == ';') ++pos_;
            return Result<std::string>::success("'");
        }
        return Result<std::string>::failure(make_error(
            ErrorCode::parse_failed,
            "unsupported XML entity",
            "XmlReader::parse_entity"));
    }

    [[nodiscard]] std::string parse_text_until_open_brace() {
        std::string out;
        while (!done() && src_[pos_] != '<') {
            if (src_[pos_] == '&') {
                auto ent = parse_entity();
                if (ent) {
                    out += ent.value();
                    continue;
                }
            }
            out.push_back(src_[pos_++]);
        }
        return out;
    }

    std::string src_;
    std::size_t pos_;
};

const XmlNode* find_child(const XmlNode* node, const std::string& name) {
    if (!node) return nullptr;
    for (const auto& child : node->children) {
        if (child.name == name) return &child;
    }
    return nullptr;
}
const XmlNode* find_child(const XmlNode& node, const std::string& name) {
    for (const auto& child : node.children) {
        if (child.name == name) return &child;
    }
    return nullptr;
}

bool parse_int64(const std::string& text, std::int64_t& out) {
    if (text.empty()) return false;
    try {
        std::size_t consumed = 0;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            out = std::stoll(text.substr(2), &consumed, 16);
        } else {
            out = std::stoll(text, &consumed, 0);
        }
        return consumed > 0;
    } catch (...) {
        return false;
    }
}

std::vector<int> parse_hotkey_text(const std::string& text) {
    std::vector<int> out;
    std::string cur;
    for (char c : text) {
        if (c == ',' || c == ' ' || c == '\t') {
            if (!cur.empty()) {
                std::int64_t v = 0;
                if (parse_int64(cur, v)) out.push_back(static_cast<int>(v));
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        std::int64_t v = 0;
        if (parse_int64(cur, v)) out.push_back(static_cast<int>(v));
    }
    return out;
}

std::string build_entry_id(const XmlNode& entry, std::size_t fallback_index) {
    auto id_attr = entry.attrs.find("ID");
    if (id_attr != entry.attrs.end() && !id_attr->second.empty()) {
        return "id:" + id_attr->second;
    }
    if (const auto* id_node = find_child(entry, "ID")) {
        if (!id_node->text.empty()) {
            return "id:" + id_node->text;
        }
    }
    auto desc_attr = entry.attrs.find("Description");
    if (desc_attr != entry.attrs.end() && !desc_attr->second.empty()) {
        return "desc:" + desc_attr->second;
    }
    if (const auto* desc_node = find_child(entry, "Description")) {
        if (!desc_node->text.empty()) {
            return "desc:" + desc_node->text;
        }
    }
    return "anon:" + std::to_string(fallback_index);
}

} // namespace

CheatTableParser::CheatTableParser() = default;
CheatTableParser::~CheatTableParser() = default;

std::string CheatTableParser::sanitize(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc == '\t' || uc == '\n' || uc == '\r') {
            out.push_back(c);
        } else if (uc < 0x20) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

Result<VariableType>
CheatTableParser::parse_variable_type(const std::string& text, bool& is_signed) {
    is_signed = false;
    std::string s = text;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto trim = [&]() {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    };
    trim();
    if (s == "byte" || s == "uint8" || s == "1 byte") {
        return Result<VariableType>::success(VariableType::uint8);
    }
    if (s == "char" || s == "int8" || s == "signed byte") {
        is_signed = true;
        return Result<VariableType>::success(VariableType::int8);
    }
    if (s == "2 bytes" || s == "2 byte" || s == "word" || s == "uint16") {
        return Result<VariableType>::success(VariableType::uint16);
    }
    if (s == "short" || s == "int16" || s == "signed 2 bytes") {
        is_signed = true;
        return Result<VariableType>::success(VariableType::int16);
    }
    if (s == "4 bytes" || s == "4 byte" || s == "dword" || s == "uint32") {
        return Result<VariableType>::success(VariableType::uint32);
    }
    if (s == "int" || s == "int32" || s == "long" || s == "signed 4 bytes") {
        is_signed = true;
        return Result<VariableType>::success(VariableType::int32);
    }
    if (s == "8 bytes" || s == "8 byte" || s == "qword" || s == "uint64") {
        return Result<VariableType>::success(VariableType::uint64);
    }
    if (s == "int64" || s == "signed 8 bytes") {
        is_signed = true;
        return Result<VariableType>::success(VariableType::int64);
    }
    if (s == "float" || s == "float32" || s == "single") {
        return Result<VariableType>::success(VariableType::float32);
    }
    if (s == "double" || s == "float64") {
        return Result<VariableType>::success(VariableType::float64);
    }
    if (s == "string") {
        return Result<VariableType>::success(VariableType::string);
    }
    if (s == "bytes" || s == "array of bytes") {
        return Result<VariableType>::success(VariableType::bytes);
    }
    return Result<VariableType>::failure(make_error(
        ErrorCode::parse_unsupported,
        "unknown variable type: " + text,
        "CheatTableParser::parse_variable_type"));
}

std::string CheatTableParser::to_string(VariableType t) {
    switch (t) {
        case VariableType::uint8:   return "uint8";
        case VariableType::int8:    return "int8";
        case VariableType::uint16:  return "uint16";
        case VariableType::int16:   return "int16";
        case VariableType::uint32:  return "uint32";
        case VariableType::int32:   return "int32";
        case VariableType::uint64:  return "uint64";
        case VariableType::int64:   return "int64";
        case VariableType::float32: return "float32";
        case VariableType::float64: return "float64";
        case VariableType::string:  return "string";
        case VariableType::bytes:   return "bytes";
        case VariableType::unknown: return "unknown";
    }
    return "unknown";
}

Result<CheatTable> CheatTableParser::parse_string(const std::string& xml) const {
    const std::string cleaned = sanitize(xml);
    XmlReader reader(cleaned);
    auto root = reader.parse_root();
    if (!root) {
        return Result<CheatTable>::failure(make_error(
            root.error().code,
            "XML parse failed: " + root.error().message,
            "CheatTableParser::parse_string", 0, 0, root.error().system_error));
    }
    if (root.value().name != "CheatTable") {
        return Result<CheatTable>::failure(make_error(
            ErrorCode::parse_failed,
            "root element is not <CheatTable>: " + root.value().name,
            "CheatTableParser::parse_string"));
    }
    CheatTable table;
    if (auto* title = find_child(root.value(), "Title")) {
        table.title = title->text;
    }
    if (auto* ver = find_child(root.value(), "VersionCE")) {
        std::int64_t v = 0;
        if (parse_int64(ver->text, v)) table.version_ce = static_cast<std::uint32_t>(v);
    }
    const XmlNode* entries_node = find_child(root.value(), "CheatEntries");
    if (!entries_node) {
        table.diagnostics.push_back("table has no <CheatEntries> block");
        return Result<CheatTable>::success(std::move(table));
    }
    std::size_t index = 0;
    for (const auto& child : entries_node->children) {
        if (child.name != "CheatEntry") {
            table.diagnostics.push_back(
                "ignoring unknown child of CheatEntries: " + child.name);
            continue;
        }
        CheatEntry entry;
        entry.id.value = build_entry_id(child, index);
        ++index;
        auto desc = child.attrs.find("Description");
        if (desc != child.attrs.end()) entry.description = desc->second;
        else if (const auto* desc_node = find_child(child, "Description")) {
            entry.description = desc_node->text;
        }
        auto section = find_child(child, "Section");
        if (section) entry.section = section->text;

        const XmlNode* variable = find_child(child, "Variable");
        if (!variable) variable = &child;
        auto addr = variable->attrs.find("Address");
        if (addr != variable->attrs.end() && !addr->second.empty()) {
            std::int64_t v = 0;
            if (parse_int64(addr->second, v)) entry.address = static_cast<Address>(v);
        }
        if (entry.address == 0) {
            if (const auto* addr_node = find_child(variable, "Address")) {
                std::int64_t v = 0;
                if (parse_int64(addr_node->text, v)) {
                    entry.address = static_cast<Address>(v);
                } else if (!addr_node->text.empty() && addr_node->text.front() == '"') {
                    table.diagnostics.push_back(
                        "entry " + entry.id.value +
                        " has a symbolic address expression: " + addr_node->text);
                }
            }
        }
        const XmlNode* offsets_node = find_child(variable, "Offsets");
        if (offsets_node) {
            std::int64_t v = 0;
            if (parse_int64(offsets_node->text, v)) {
                entry.offsets.push_back(static_cast<Address>(v));
            }
            for (const auto& sub : offsets_node->children) {
                if (sub.name == "Offset" && parse_int64(sub.text, v)) {
                    entry.offsets.push_back(static_cast<Address>(v));
                }
            }
        }
        const XmlNode* type_node = find_child(variable, "Type");
        std::string type_text = type_node ? type_node->text : std::string{};
        const XmlNode* type_node2 = find_child(child, "VariableType");
        if (type_node2 && !type_node2->text.empty()) type_text = type_node2->text;
        const XmlNode* display = find_child(variable, "DisplayAs");
        if (display) type_text = display->text;
        bool is_signed = false;
        auto type_result = parse_variable_type(type_text, is_signed);
        if (!type_result) {
            entry.failure_reason = type_result.error().message;
            table.unsupported_entries.push_back(entry);
            table.diagnostics.push_back(
                "entry " + entry.id.value + ": " + type_result.error().message);
            continue;
        }
        entry.type = type_result.value();
        entry.is_signed = is_signed;
        const XmlNode* value_node = find_child(variable, "Value");
        if (value_node) entry.default_value_text = value_node->text;
        const XmlNode* last_state = find_child(child, "LastState");
        if (last_state) {
            if (entry.default_value_text.empty()) {
                auto it = last_state->attrs.find("Value");
                if (it != last_state->attrs.end()) entry.default_value_text = it->second;
            }
        }

        if (find_child(child, "AssemblerScript")
            || find_child(child, "AutoAssembler")
            || find_child(child, "AA")) {
            entry.auto_assembler = true;
            entry.failure_reason =
                "Auto Assembler entries are not supported in this build";
            table.unsupported_entries.push_back(entry);
            table.diagnostics.push_back(
                "entry " + entry.id.value + ": Auto Assembler skipped");
            continue;
        }
        if (find_child(child, "Inject")
            || find_child(child, "LoadLibrary")
            || find_child(child, "ShellExecute")) {
            entry.shell_command = true;
            entry.failure_reason = "DLL/shell injection is not supported";
            table.unsupported_entries.push_back(entry);
            table.diagnostics.push_back(
                "entry " + entry.id.value + ": shell/inject skipped");
            continue;
        }
        const XmlNode* hotkeys_node = find_child(child, "Hotkeys");
        if (hotkeys_node) {
            for (const auto& hk : hotkeys_node->children) {
                if (hk.name == "Hotkey") {
                    auto codes = parse_hotkey_text(hk.text);
                    for (int c : codes) entry.hotkeys.push_back(c);
                    if (const auto* code_node = find_child(hk, "Code")) {
                        std::int64_t v = 0;
                        if (parse_int64(code_node->text, v)) {
                            entry.hotkeys.push_back(static_cast<int>(v));
                        }
                    }
                    if (auto act = hk.attrs.find("Action");
                        act != hk.attrs.end()) {
                        entry.hotkey_action = act->second;
                    } else if (const auto* act_node = find_child(hk, "Action")) {
                        entry.hotkey_action = act_node->text;
                    }
                }
            }
        }
        table.entries.push_back(std::move(entry));
    }
    table.diagnostics.push_back(
        "parsed " + std::to_string(table.entries.size())
            + " supported entries, "
            + std::to_string(table.unsupported_entries.size())
            + " unsupported entries");
    return Result<CheatTable>::success(std::move(table));
}

Result<CheatTable> CheatTableParser::parse_file(const std::string& path) const {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return Result<CheatTable>::failure(make_error(
            ErrorCode::parse_failed,
            "could not open " + path,
            "CheatTableParser::parse_file"));
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_string(ss.str());
}

} // namespace gtlibcpp
