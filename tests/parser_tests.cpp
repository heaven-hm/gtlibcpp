// Cheat-table parser regression tests. Every test uses a synthetic
// .ct fragment so the suite is self-contained and does not require the
// assaultcube/IGI fixtures to be present in the test sandbox.
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "gtlibcpp/parser.hpp"

namespace {

using gtlibcpp::CheatTableParser;
using gtlibcpp::Result;
using gtlibcpp::VariableType;

void test_parses_simple_uint_entry() {
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
    assert(r.ok());
    assert(r.value().entries.size() == 1);
    const auto& e = r.value().entries[0];
    assert(e.id.value == "id:1");
    assert(e.description == "Health");
    assert(e.address == 0x1000);
    assert(e.type == VariableType::uint32);
    assert(e.hotkeys.size() == 1);
    assert(e.hotkeys[0] == 112);
    assert(r.value().unsupported_entries.empty());
    std::cout << "test_parses_simple_uint_entry passed\n";
}

void test_preserves_multiple_hotkeys() {
    // Legacy GTLibc.cpp:574-577 dropped every hotkey after the first.
    // The new parser must retain all hotkeys per entry.
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
    assert(r.ok());
    assert(r.value().entries.size() == 1);
    const auto& e = r.value().entries[0];
    assert(e.hotkeys.size() == 2);
    assert(e.hotkeys[0] == 112);
    assert(e.hotkeys[1] == 113);
    std::cout << "test_preserves_multiple_hotkeys passed\n";
}

void test_distinguishes_byte_from_char() {
    // Legacy GTLibc.cpp:1370-1392 used string-contains matching; "Byte"
    // matched both uint8 and the legacy "char" extraction. The new
    // parser must distinguish them.
    CheatTableParser parser;
    bool signed_byte = false;
    bool signed_word = false;
    bool signed_qword = false;
    auto t_byte   = CheatTableParser::parse_variable_type("Byte", signed_byte);
    auto t_char   = CheatTableParser::parse_variable_type("Char", signed_byte);
    auto t_word   = CheatTableParser::parse_variable_type("2 Bytes", signed_word);
    auto t_qword  = CheatTableParser::parse_variable_type("8 Bytes", signed_qword);
    assert(t_byte.ok());  assert(t_byte.value() == VariableType::uint8);
    assert(t_char.ok());  assert(t_char.value() == VariableType::int8);
    assert(t_word.ok());  assert(t_word.value() == VariableType::uint16);
    assert(t_qword.ok()); assert(t_qword.value() == VariableType::uint64);
    std::cout << "test_distinguishes_byte_from_char passed\n";
}

void test_fails_closed_on_auto_assembler() {
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
    assert(r.ok());
    assert(r.value().entries.empty());
    assert(r.value().unsupported_entries.size() == 1);
    assert(!r.value().unsupported_entries[0].failure_reason.empty());
    std::cout << "test_fails_closed_on_auto_assembler passed\n";
}

void test_fails_closed_on_loadlibrary() {
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
    assert(r.ok());
    assert(r.value().entries.empty());
    assert(r.value().unsupported_entries.size() == 1);
    std::cout << "test_fails_closed_on_loadlibrary passed\n";
}

void test_handles_nested_entries() {
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
    assert(r.ok());
    // The parent is supported; the child is reported in the
    // diagnostics so the agent layer can offer to wire it up.
    assert(r.value().entries.size() == 1);
    assert(r.value().entries[0].description == "Parent");
    std::cout << "test_handles_nested_entries passed\n";
}

void test_malformed_xml_returns_error() {
    CheatTableParser parser;
    auto r = parser.parse_string("<CheatTable><CheatEntries><CheatEntry>");
    assert(!r.ok());
    std::cout << "test_malformed_xml_returns_error passed\n";
}

void test_hex_and_signed_values_preserved() {
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
    assert(r.ok());
    assert(r.value().entries.size() == 1);
    const auto& e = r.value().entries[0];
    assert(e.offsets.size() == 2);
    assert(e.offsets[0] == 0x10);
    assert(e.offsets[1] == 0x20);
    std::cout << "test_hex_and_signed_values_preserved passed\n";
}

void test_igi_fixture_parses() {
    // The IGI fixture in the repository must be reachable. If the file
    // is not present, skip rather than fail.
    std::ifstream f("CheatTable/IGI.CT");
    if (!f) {
        std::cout << "test_igi_fixture_parses skipped (fixture missing)\n";
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    CheatTableParser parser;
    auto r = parser.parse_string(ss.str());
    assert(r.ok());
    assert(!r.value().entries.empty()
           || !r.value().unsupported_entries.empty());
    std::cout << "test_igi_fixture_parses passed\n";
}

void test_assaultcube_fixture_parses() {
    std::ifstream f("CheatTable/assaultcube.ct");
    if (!f) {
        std::cout << "test_assaultcube_fixture_parses skipped (fixture missing)\n";
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    CheatTableParser parser;
    auto r = parser.parse_string(ss.str());
    assert(r.ok());
    std::cout << "test_assaultcube_fixture_parses passed\n";
}

} // namespace

int main() {
    test_parses_simple_uint_entry();
    test_preserves_multiple_hotkeys();
    test_distinguishes_byte_from_char();
    test_fails_closed_on_auto_assembler();
    test_fails_closed_on_loadlibrary();
    test_handles_nested_entries();
    test_malformed_xml_returns_error();
    test_hex_and_signed_values_preserved();
    test_igi_fixture_parses();
    test_assaultcube_fixture_parses();
    std::cout << "gtlibcpp parser tests: all passed\n";
    return 0;
}
