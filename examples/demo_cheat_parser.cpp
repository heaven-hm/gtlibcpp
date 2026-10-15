#include <iostream>
#include <cassert>
#include "gtlibcpp/parser.hpp"

int main() {
    std::cout << "========================================================\n";
    std::cout << "  E2E DEMO 3: Cheat Engine 7.x XML Parser & Safety Gate \n";
    std::cout << "========================================================\n\n";

    gtlibcpp::CheatTableParser parser;

    // 1. Valid Cheat Engine 7.x Table XML
    const std::string valid_table_xml = R"(<?xml version="1.0" encoding="utf-8"?>
<CheatTable CheatEngineTableVersion="34">
  <CheatEntries>
    <CheatEntry>
      <ID>101</ID>
      <Description>"Player Health"</Description>
      <VariableType>4 Bytes</VariableType>
      <Address>0x140100000</Address>
      <Offsets>
        <Offset>0x10</Offset>
        <Offset>0x50</Offset>
      </Offsets>
      <Hotkeys>
        <Hotkey Action="Toggle Activation">17 72</Hotkey>
      </Hotkeys>
    </CheatEntry>
    <CheatEntry>
      <ID>102</ID>
      <Description>"Shield Armor"</Description>
      <VariableType>Float</VariableType>
      <Address>0x140200000</Address>
    </CheatEntry>
  </CheatEntries>
</CheatTable>
)";

    auto parse_res = parser.parse_string(valid_table_xml);
    assert(parse_res.ok());
    const auto& table = parse_res.value();

    std::cout << "[+] Parsed CE Table Version: " << table.version_ce << "\n";
    std::cout << "[+] Loaded Entries Count: " << table.entries.size() << "\n";
    assert(table.entries.size() == 2);

    // Verify Entry 1 (Health)
    const auto& health_entry = table.entries[0];
    std::cout << "  - ID: " << health_entry.id.value 
              << ", Desc: '" << health_entry.description << "'"
              << ", Type: " << gtlibcpp::CheatTableParser::to_string(health_entry.type)
              << ", Offsets: " << health_entry.offsets.size() << "\n";
    assert(health_entry.id.value == "id:101");
    assert(health_entry.type == gtlibcpp::VariableType::uint32);
    assert(health_entry.offsets.size() == 2);
    assert(health_entry.hotkeys.size() == 2); // Ctrl (17) + H (72)

    // 2. Test Security Boundary: Malicious / Unsupported Auto Assembler Injection
    std::cout << "\n[*] Testing Security Boundary against Auto Assembler / Shell Script injections...\n";
    const std::string malicious_table_xml = R"(<?xml version="1.0" encoding="utf-8"?>
<CheatTable CheatEngineTableVersion="34">
  <CheatEntries>
    <CheatEntry>
      <ID>666</ID>
      <Description>"RCE Auto Assembler Exploit"</Description>
      <VariableType>Auto Assembler Script</VariableType>
      <AssemblerScript>[ENABLE]
alloc(newmem,2048)
ShellExecute("calc.exe")
[DISABLE]
</AssemblerScript>
    </CheatEntry>
  </CheatEntries>
</CheatTable>
)";

    auto parse_malicious = parser.parse_string(malicious_table_xml);
    assert(parse_malicious.ok()); // Parser does not crash
    const auto& mal_table = parse_malicious.value();

    std::cout << "[+] Supported Valid Entries: " << mal_table.entries.size() << "\n";
    std::cout << "[+] Quarantined Unsupported Entries: " << mal_table.unsupported_entries.size() << "\n";
    assert(mal_table.entries.empty()); // No unsafe entries permitted into active set
    assert(mal_table.unsupported_entries.size() == 1);
    assert(mal_table.unsupported_entries[0].auto_assembler);
    std::cout << "[+] Quarantined Reason: " << mal_table.unsupported_entries[0].failure_reason << "\n";

    // 3. Test Corrupted / Malformed XML
    std::cout << "\n[*] Testing Malformed XML robustness...\n";
    auto parse_corrupt = parser.parse_string("<CheatTable><UnclosedTag>");
    assert(!parse_corrupt.ok());
    assert(parse_corrupt.error().code == gtlibcpp::ErrorCode::parse_failed);
    std::cout << "[+] Gracefully caught malformed XML: " << parse_corrupt.error().message << "\n";

    std::cout << "\n[SUCCESS] E2E Cheat Table Parser Demo completed flawlessly!\n\n";
    return 0;
}
