// Cheat-table parser regression tests.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "gtlibcpp/parser.hpp"
#include "gtlibcpp_test.hpp"

using gtlibcpp::CheatTableParser;
using gtlibcpp::Result;
using gtlibcpp::VariableType;

GTLIBCPP_TEST(parses_simple_uint_entry) {
    CheatTableParser parser;
    const std::string xml = R"(<?xml version="1.0" encoding="utf-8"?>
<CheatTable>
  <CheatEntries>
    <CheatEntry>
      <ID>1</ID>
      <Description>Health</Description>
      <Variable>
        <Type>4 Bytes</Type>
        <Address>0x1000</Address>
      </Variable>
      <Hotkeys>
        <Hotkey>
          <Action>Set Value</Action>
          <Code>112</Code>
        </Hotkey>
      </Hotkeys>
    </CheatEntry>
  </CheatEntries>
</CheatTable>)";
    auto r = parser.parse_string(xml);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE_EQ(r.value().entries.size(), 1u);
    const auto& e = r.value().entries[0];
    GTLIBCPP_REQUIRE_EQ(e.id.value, std::string("id:1"));
    GTLIBCPP_REQUIRE_EQ(e.description, std::string("Health"));
    GTLIBCPP_REQUIRE_EQ(e.address, 0x1000u);
    GTLIBCPP_REQUIRE(e.type == VariableType::uint32);
    GTLIBCPP_REQUIRE_EQ(e.hotkeys.size(), 1u);
    GTLIBCPP_REQUIRE_EQ(e.hotkeys[0], 112);
    GTLIBCPP_REQUIRE(r.value().unsupported_entries.empty());
}

GTLIBCPP_TEST(preserves_multiple_hotkeys) {
    CheatTableParser parser;
    const std::string xml = R"(<?xml version="1.0"?>
<CheatTable>
  <CheatEntries>
    <CheatEntry>
      <ID>42</ID>
      <Description>Ammo</Description>
      <Variable>
        <Type>2 Bytes</Type>
        <Address>0x2000</Address>
      </Variable>
      <Hotkeys>
        <Hotkey>
          <Action>Set Value</Action>
          <Code>112</Code>
        </Hotkey>
        <Hotkey>
          <Action>Freeze</Action>
          <Code>113</Code>
        </Hotkey>
      </Hotkeys>
    </CheatEntry>
  </CheatEntries>
</CheatTable>)";
    auto r = parser.parse_string(xml);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE_EQ(r.value().entries.size(), 1u);
    const auto& e = r.value().entries[0];
    GTLIBCPP_REQUIRE_EQ(e.hotkeys.size(), 2u);
    GTLIBCPP_REQUIRE_EQ(e.hotkeys[0], 112);
    GTLIBCPP_REQUIRE_EQ(e.hotkeys[1], 113);
}

GTLIBCPP_TEST(distinguishes_byte_from_char) {
    bool signed_byte = false;
    bool signed_word = false;
    bool signed_qword = false;
    auto t_byte   = CheatTableParser::parse_variable_type("Byte", signed_byte);
    auto t_char   = CheatTableParser::parse_variable_type("Char", signed_byte);
    auto t_word   = CheatTableParser::parse_variable_type("2 Bytes", signed_word);
    auto t_qword  = CheatTableParser::parse_variable_type("8 Bytes", signed_qword);
    GTLIBCPP_REQUIRE(t_byte);
    GTLIBCPP_REQUIRE(t_byte.value() == VariableType::uint8);
    GTLIBCPP_REQUIRE(t_char);
    GTLIBCPP_REQUIRE(t_char.value() == VariableType::int8);
    GTLIBCPP_REQUIRE(t_word);
    GTLIBCPP_REQUIRE(t_word.value() == VariableType::uint16);
    GTLIBCPP_REQUIRE(t_qword);
    GTLIBCPP_REQUIRE(t_qword.value() == VariableType::uint64);
}

GTLIBCPP_TEST(fails_closed_on_auto_assembler) {
    CheatTableParser parser;
    const std::string xml = R"(<?xml version="1.0"?>
<CheatTable>
  <CheatEntries>
    <CheatEntry>
      <ID>9</ID>
      <Description>Bad</Description>
      <Variable><Type>4 Bytes</Type><Address>0x100</Address></Variable>
      <AssemblerScript>[ENABLE]
alloc(newmem,2048)
label(returnhere)
</AssemblerScript>
    </CheatEntry>
  </CheatEntries>
</CheatTable>)";
    auto r = parser.parse_string(xml);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE(r.value().entries.empty());
    GTLIBCPP_REQUIRE_EQ(r.value().unsupported_entries.size(), 1u);
    GTLIBCPP_REQUIRE(!r.value().unsupported_entries[0].failure_reason.empty());
}

GTLIBCPP_TEST(fails_closed_on_loadlibrary) {
    CheatTableParser parser;
    const std::string xml = R"(<?xml version="1.0"?>
<CheatTable>
  <CheatEntries>
    <CheatEntry>
      <ID>10</ID>
      <Description>Inject</Description>
      <Variable><Type>4 Bytes</Type><Address>0x100</Address></Variable>
      <LoadLibrary>evil.dll</LoadLibrary>
    </CheatEntry>
  </CheatEntries>
</CheatTable>)";
    auto r = parser.parse_string(xml);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE(r.value().entries.empty());
    GTLIBCPP_REQUIRE_EQ(r.value().unsupported_entries.size(), 1u);
}

GTLIBCPP_TEST(fails_closed_on_lua_script) {
    CheatTableParser parser;
    const std::string xml = R"(<?xml version="1.0"?>
<CheatTable>
  <CheatEntries>
    <CheatEntry>
      <ID>11</ID>
      <Description>Lua</Description>
      <Variable><Type>4 Bytes</Type><Address>0x100</Address></Variable>
      <LuaScript>openProcess("game.exe")</LuaScript>
    </CheatEntry>
  </CheatEntries>
</CheatTable>)";
    auto r = parser.parse_string(xml);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE(r.value().entries.empty());
    GTLIBCPP_REQUIRE_EQ(r.value().unsupported_entries.size(), 1u);
    GTLIBCPP_REQUIRE(r.value().unsupported_entries[0].auto_assembler);
}

GTLIBCPP_TEST(fails_closed_on_raw_byte_array) {
    CheatTableParser parser;
    const std::string xml = R"(<?xml version="1.0"?>
<CheatTable>
  <CheatEntries>
    <CheatEntry>
      <ID>12</ID>
      <Description>Bytes</Description>
      <Variable><Type>Byte</Type><Address>0x100</Address></Variable>
      <Bytes>9090909090</Bytes>
    </CheatEntry>
  </CheatEntries>
</CheatTable>)";
    auto r = parser.parse_string(xml);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE(r.value().entries.empty());
    GTLIBCPP_REQUIRE_EQ(r.value().unsupported_entries.size(), 1u);
    GTLIBCPP_REQUIRE(r.value().unsupported_entries[0].raw_byte_write);
}

GTLIBCPP_TEST(handles_nested_entries) {
    CheatTableParser parser;
    const std::string xml = R"(<?xml version="1.0"?>
<CheatTable>
  <CheatEntries>
    <CheatEntry>
      <ID>1</ID>
      <Description>Parent</Description>
      <Variable><Type>4 Bytes</Type><Address>0x1000</Address></Variable>
      <CheatEntries>
        <CheatEntry>
          <ID>2</ID>
          <Description>Child</Description>
          <Variable><Type>Float</Type><Address>0x2000</Address></Variable>
        </CheatEntry>
      </CheatEntries>
    </CheatEntry>
  </CheatEntries>
</CheatTable>)";
    auto r = parser.parse_string(xml);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE_EQ(r.value().entries.size(), 1u);
    GTLIBCPP_REQUIRE_EQ(r.value().entries[0].description, std::string("Parent"));
}

GTLIBCPP_TEST(malformed_xml_returns_error) {
    CheatTableParser parser;
    auto r = parser.parse_string("<CheatTable><CheatEntries><CheatEntry>");
    GTLIBCPP_REQUIRE(!r);
}

GTLIBCPP_TEST(hex_and_signed_values_preserved) {
    CheatTableParser parser;
    const std::string xml = R"(<?xml version="1.0"?>
<CheatTable>
  <CheatEntries>
    <CheatEntry>
      <ID>1</ID>
      <Description>Pointer</Description>
      <Variable>
        <Type>4 Bytes</Type>
        <Address>0x401000</Address>
        <Offsets>
          <Offset>0x10</Offset>
          <Offset>0x20</Offset>
        </Offsets>
      </Variable>
    </CheatEntry>
  </CheatEntries>
</CheatTable>)";
    auto r = parser.parse_string(xml);
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE_EQ(r.value().entries.size(), 1u);
    const auto& e = r.value().entries[0];
    GTLIBCPP_REQUIRE_EQ(e.offsets.size(), 2u);
    GTLIBCPP_REQUIRE_EQ(e.offsets[0], 0x10u);
    GTLIBCPP_REQUIRE_EQ(e.offsets[1], 0x20u);
}

GTLIBCPP_TEST(igi_fixture_parses) {
    std::ifstream f("CheatTable/IGI.CT");
    if (!f) {
        std::printf("  (skipped: CheatTable/IGI.CT not present)\n");
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    CheatTableParser parser;
    auto r = parser.parse_string(ss.str());
    GTLIBCPP_REQUIRE(r);
    GTLIBCPP_REQUIRE(!r.value().entries.empty()
                     || !r.value().unsupported_entries.empty());
}

GTLIBCPP_TEST(assaultcube_fixture_parses) {
    std::ifstream f("CheatTable/assaultcube.ct");
    if (!f) {
        std::printf("  (skipped: CheatTable/assaultcube.ct not present)\n");
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    CheatTableParser parser;
    auto r = parser.parse_string(ss.str());
    GTLIBCPP_REQUIRE(r);
}

int main() {
    return GTLIBCPP_RUN_ALL();
}
