#include <iostream>
#include <iomanip>
#include <cassert>
#include "gtlibcpp/memory_session.hpp"
#include "simulated_game.hpp"

int main() {
    std::cout << "========================================================\n";
    std::cout << "  E2E DEMO 1: Game Trainer Memory Operations Pipeline   \n";
    std::cout << "========================================================\n\n";

    auto game = std::make_shared<gtlibcpp::demo::SimulatedGameProcess>();
    gtlibcpp::MemorySession session(game);

    // 1. Verify Target Identity
    const auto& id = session.identity();
    std::cout << "[+] Attached to PID: " << id.pid << " (" << id.image_path << ")\n";
    std::cout << "[+] Architecture: " << (id.architecture == gtlibcpp::Architecture::x64 ? "x64" : "x86") << "\n";
    assert(session.is_alive());

    // 2. Resolve Multi-Level Pointer Chain to Player Stats
    // Chain: kPlayerPtrAddr -> +0x50 (stats pointer)
    std::vector<gtlibcpp::Address> offsets = {0, 0x50};
    auto resolved_stats_addr = session.resolve_pointer_chain(gtlibcpp::demo::SimulatedGameProcess::kPlayerPtrAddr, offsets);
    if (!resolved_stats_addr) {
        std::cerr << "[-] Failed to resolve health pointer: " << resolved_stats_addr.error().message << "\n";
        return 1;
    }
    const auto stats_addr = resolved_stats_addr.value();
    const auto health_addr = stats_addr + gtlibcpp::demo::SimulatedGameProcess::kHealthOffset;
    std::cout << "[+] Resolved Player Stats Base: 0x" << std::hex << stats_addr << std::dec << "\n";
    std::cout << "[+] Resolved Player Health Address: 0x" << std::hex << health_addr << std::dec << "\n";

    // 3. Read Initial Stats via Resolved Address
    auto initial_health = session.read<std::uint32_t>(health_addr);
    assert(initial_health && initial_health.value() == 100);
    std::cout << "[+] Initial Health: " << initial_health.value() << "\n";

    // 4. Mutate Health to Godmode (999)
    auto write_health = session.write<std::uint32_t>(health_addr, 999);
    assert(write_health.ok());

    auto updated_health = session.read<std::uint32_t>(health_addr);
    assert(updated_health && updated_health.value() == 999);
    std::cout << "[+] Updated Health (Godmode): " << updated_health.value() << "\n";

    // 5. Read and Write String via BoundedString API
    const gtlibcpp::Address name_addr = gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kNameOffset;
    auto initial_name = session.read_string(name_addr, 32, false);
    assert(initial_name);
    std::string name_str(reinterpret_cast<const char*>(initial_name.value().bytes().data()));
    std::cout << "[+] Initial Character Name: " << name_str << "\n";

    std::string new_name = "Spectre_Commander";
    std::vector<std::uint8_t> new_name_bytes(new_name.begin(), new_name.end());
    new_name_bytes.push_back(0); // NUL terminator
    auto bounded_new_name = gtlibcpp::BoundedString::from_utf8(new_name_bytes, 32, true);
    assert(bounded_new_name.ok());

    auto write_name = session.write_string(name_addr, bounded_new_name.value(), 32);
    assert(write_name.ok());

    auto updated_name = session.read_string(name_addr, 32, true);
    assert(updated_name.ok());
    std::string updated_str(reinterpret_cast<const char*>(updated_name.value().bytes().data()));
    std::cout << "[+] Updated Character Name: " << updated_str << "\n";
    assert(updated_str == "Spectre_Commander");

    // 6. Transactional Compare-and-Write-Verify (Armor Upgrade)
    const gtlibcpp::Address armor_addr = gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kArmorOffset;
    auto cas_result = session.compare_write_verify<float>(armor_addr, 100.0f);
    assert(cas_result.ok());

    auto current_armor = session.read<float>(armor_addr);
    assert(current_armor && current_armor.value() == 100.0f);
    std::cout << "[+] Atomic Compare-Write-Verify Armor: " << current_armor.value() << "%\n";

    std::cout << "\n[SUCCESS] E2E Game Trainer Demo completed flawlessly! (Reads=" 
              << game->total_reads() << ", Writes=" << game->total_writes() << ")\n\n";
    return 0;
}
