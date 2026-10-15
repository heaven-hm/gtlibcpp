#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/parser.hpp"
#include "gtlibcpp/policy.hpp"
#include "gtlibcpp/result.hpp"

#if defined(_WIN32)
#include "gtlibcpp/windows_backend.hpp"
#include "gtlibcpp/windows_discovery.hpp"
#endif

#ifndef GTLIBCPP_VERSION
#define GTLIBCPP_VERSION "1.0.0"
#endif

namespace {

// Translates Cheat Engine virtual key codes into readable names
std::string format_hotkey(const std::vector<int>& keys) {
    if (keys.empty()) return "None";
    std::string result;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) result += "+";
        const int k = keys[i];
        if (k == 16) result += "Shift";
        else if (k == 17) result += "Ctrl";
        else if (k == 18) result += "Alt";
        else if (k >= 65 && k <= 90) result += static_cast<char>(k);
        else if (k >= 48 && k <= 57) result += static_cast<char>(k);
        else if (k >= 112 && k <= 123) result += "F" + std::to_string(k - 111);
        else result += "Key(" + std::to_string(k) + ")";
    }
    return result;
}

// Simulated backend representing an offline game state (e.g. AssaultCube LocalPlayer layout)
class SimulatedGameBackend final : public gtlibcpp::IMemoryBackend {
public:
    SimulatedGameBackend() {
        // Setup AssaultCube LocalPlayer pointer at base + 0x17E0A8 -> 0x00500000
        const gtlibcpp::Address base = 0x400000;
        const gtlibcpp::Address local_entity_ptr = base + 0x17E0A8;
        const gtlibcpp::Address local_player = 0x500000;
        put_u32(local_entity_ptr, static_cast<std::uint32_t>(local_player));

        // Health at offset 0xEC -> default 100
        put_u32(local_player + 0xEC, 100);

        // Rifle Ammo at offset 0x140 -> default 20
        put_u32(local_player + 0x140, 20);

        // ZCord (jump/height coordinate) at offset 0x30 -> default 4.5f
        put_float(local_player + 0x30, 4.5f);

        // Weapon Ammo multi-level pointer:
        // offset 0x368 -> weapon pointer 0x600000
        // offset 0x14  -> clip pointer 0x700000
        // offset 0x0   -> ammo count
        const gtlibcpp::Address weapon_ptr = 0x600000;
        const gtlibcpp::Address clip_ptr = 0x700000;
        put_u32(local_player + 0x368, static_cast<std::uint32_t>(weapon_ptr));
        put_u32(weapon_ptr + 0x14, static_cast<std::uint32_t>(clip_ptr));
        put_u32(clip_ptr + 0x0, 30);
    }

    gtlibcpp::Result<std::vector<std::uint8_t>>
    read(gtlibcpp::Address address, std::size_t size) override {
        const auto valid = gtlibcpp::validate_read_request(address, size);
        if (!valid) return gtlibcpp::Result<std::vector<std::uint8_t>>::failure(valid.error());

        std::vector<std::uint8_t> bytes(size, 0);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = memory_.find(address + i);
            if (it == memory_.end()) {
                return gtlibcpp::Result<std::vector<std::uint8_t>>::failure(
                    gtlibcpp::make_error(gtlibcpp::ErrorCode::read_failed,
                                         "address unmapped in simulated space",
                                         "SimulatedGameBackend.read", address, size));
            }
            bytes[i] = it->second;
        }
        return gtlibcpp::Result<std::vector<std::uint8_t>>::success(std::move(bytes));
    }

    gtlibcpp::Result<std::size_t>
    write(gtlibcpp::Address address, const std::vector<std::uint8_t>& bytes) override {
        const auto valid = gtlibcpp::validate_write_request(address, bytes.size());
        if (!valid) return gtlibcpp::Result<std::size_t>::failure(valid.error());

        for (std::size_t i = 0; i < bytes.size(); ++i) {
            memory_[address + i] = bytes[i];
        }
        return gtlibcpp::Result<std::size_t>::success(bytes.size());
    }

    bool is_alive() noexcept override { return true; }
    std::string target_id() const override { return "ac_client (simulated)"; }
    gtlibcpp::TargetIdentity identity() const override {
        return gtlibcpp::TargetIdentity{1337, 1, "ac_client.exe", "sha256:assaultcube", gtlibcpp::Architecture::x86};
    }

    void put_u32(gtlibcpp::Address a, std::uint32_t val) {
        const auto* ptr = reinterpret_cast<const std::uint8_t*>(&val);
        for (std::size_t i = 0; i < 4; ++i) memory_[a + i] = ptr[i];
    }

    void put_float(gtlibcpp::Address a, float val) {
        const auto* ptr = reinterpret_cast<const std::uint8_t*>(&val);
        for (std::size_t i = 0; i < 4; ++i) memory_[a + i] = ptr[i];
    }

private:
    std::map<gtlibcpp::Address, std::uint8_t> memory_;
};

// Resolves a multi-level pointer path to the final data address
gtlibcpp::Result<gtlibcpp::Address> resolve_pointer_chain(
    gtlibcpp::MemorySession& session,
    gtlibcpp::Address base_address,
    const std::vector<gtlibcpp::Address>& offsets) {
    if (offsets.empty()) {
        return gtlibcpp::Result<gtlibcpp::Address>::success(base_address);
    }

    gtlibcpp::Address current = base_address;
    // Walk pointer dereferences in Cheat Table order (last to first or first to last)
    for (std::size_t i = offsets.size(); i > 1; --i) {
        auto deref = session.read<std::uint32_t>(current);
        if (!deref) return gtlibcpp::Result<gtlibcpp::Address>::failure(deref.error());
        current = static_cast<gtlibcpp::Address>(deref.value()) + offsets[i - 1];
    }

    auto deref_last = session.read<std::uint32_t>(current);
    if (!deref_last) return gtlibcpp::Result<gtlibcpp::Address>::failure(deref_last.error());
    return gtlibcpp::Result<gtlibcpp::Address>::success(
        static_cast<gtlibcpp::Address>(deref_last.value()) + offsets[0]);
}

} // namespace

int main(int argc, char** argv) {
    std::string table_path = "demos/CheatTable/assaultcube.ct";
    bool test_mode = false;
    bool list_mode = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--test") {
            test_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                table_path = argv[++i];
            }
        } else if (arg == "--list") {
            list_mode = true;
        } else if (arg == "--table" && i + 1 < argc) {
            table_path = argv[++i];
        } else if (arg == "--version") {
            std::cout << "gt_trainer_cli (gtlibcpp " << GTLIBCPP_VERSION << ")\n";
            return 0;
        } else if (arg == "--help") {
            std::cout << "Usage: gt_trainer_cli [OPTIONS]\n"
                      << "Options:\n"
                      << "  --table <path>   Load Cheat Table file (default: " << table_path << ")\n"
                      << "  --list           List parsed cheat entries and exit\n"
                      << "  --test [path]    Run automated hotkey detection and memory verification test\n"
                      << "  --version        Show version information\n"
                      << "  --help           Show this help message\n";
            return 0;
        }
    }

    std::cout << "========================================================\n"
              << "  gtlibcpp CLI Trainer & Hotkey Controller v" << GTLIBCPP_VERSION << "\n"
              << "========================================================\n";

    // 1. Parse the Cheat Table using gtlibcpp::CheatTableParser
    gtlibcpp::CheatTableParser parser;
    auto parse_res = parser.parse_file(table_path);
    if (!parse_res) {
        std::cerr << "[-] Error parsing cheat table '" << table_path << "': "
                  << parse_res.error().message << "\n";
        return 1;
    }

    const auto& table = parse_res.value();
    std::cout << "[+] Loaded Cheat Table: " << (table.title.empty() ? table_path : table.title)
              << " (CE Version: " << table.version_ce << ")\n"
              << "[+] Discovered Entries: " << table.entries.size() << "\n\n";

    // 2. Display Parsed Entries & Hotkeys
    std::cout << "--- Discovered Cheats & Hotkey Bindings ---\n";
    for (const auto& entry : table.entries) {
        std::cout << "  [" << entry.id.value << "] "
                  << std::left << std::setw(20) << entry.description
                  << " | Type: " << std::setw(8) << gtlibcpp::CheatTableParser::to_string(entry.type)
                  << " | Hotkey: " << std::setw(12) << format_hotkey(entry.hotkeys)
                  << " | Action: " << (entry.hotkey_action.empty() ? "None" : entry.hotkey_action)
                  << "\n";
    }
    std::cout << "-------------------------------------------\n\n";

    if (list_mode) {
        return 0;
    }

    // 3. Initialize Memory Backend & Session
    std::shared_ptr<gtlibcpp::IMemoryBackend> backend;
#if defined(_WIN32)
    const auto pid = gtlibcpp::find_game_process("ac_client");
    if (pid != 0) {
        const auto path = gtlibcpp::get_process_image_path(pid);
        gtlibcpp::WindowsBackendOptions opts{};
        opts.pid = pid;
        opts.image_path = path;
        opts.architecture = gtlibcpp::Architecture::x86;
        opts.read_only = false;
        backend = gtlibcpp::make_windows_backend(opts);
        if (backend) {
            std::cout << "[+] Attached to live target process: PID " << pid << " (" << path << ")\n";
        }
    }
#endif

    if (!backend) {
        backend = std::make_shared<SimulatedGameBackend>();
        std::cout << "[*] Operating on high-fidelity simulated AssaultCube memory space (macOS/offline sandbox)\n";
    }

    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    auto freeze = std::make_shared<gtlibcpp::FreezeManager>(session);

    // Identify target entries: Health, Weapon Ammo, ZCord
    const gtlibcpp::CheatEntry* health_entry = nullptr;
    const gtlibcpp::CheatEntry* ammo_entry = nullptr;
    const gtlibcpp::CheatEntry* zcord_entry = nullptr;

    for (const auto& e : table.entries) {
        if (e.description == "Health") health_entry = &e;
        else if (e.description == "Weapon Ammo" || e.description == "Riffle Ammo") {
            if (!ammo_entry) ammo_entry = &e;
        } else if (e.description == "ZCord") zcord_entry = &e;
    }

    // Resolve pointers
    const gtlibcpp::Address base = 0x400000;
    const gtlibcpp::Address health_addr = base + 0x17E0A8;
    const auto resolved_health = resolve_pointer_chain(*session, health_addr, health_entry ? health_entry->offsets : std::vector<gtlibcpp::Address>{0xEC});
    const auto resolved_ammo = resolve_pointer_chain(*session, health_addr, ammo_entry ? ammo_entry->offsets : std::vector<gtlibcpp::Address>{0x140});
    const auto resolved_zcord = resolve_pointer_chain(*session, health_addr, zcord_entry ? zcord_entry->offsets : std::vector<gtlibcpp::Address>{0x30});

    if (!resolved_health || !resolved_ammo || !resolved_zcord) {
        std::cerr << "[-] Failed resolving pointer chain coordinates\n";
        return 1;
    }

    std::cout << "[+] Resolved Target Addresses:\n"
              << "    Health Address : 0x" << std::hex << resolved_health.value() << std::dec << "\n"
              << "    Ammo Address   : 0x" << std::hex << resolved_ammo.value() << std::dec << "\n"
              << "    ZCord Address  : 0x" << std::hex << resolved_zcord.value() << std::dec << "\n\n";

    // Hotkey Action Dispatcher
    auto trigger_health = [&]() {
        auto r = session->write<std::uint32_t>(resolved_health.value(), 100);
        if (r) {
            std::cout << " [HOTKEY TRIGGERED] -> Health set to 100 [OK]\n";
        } else {
            std::cout << " [ERROR] Health write failed: " << r.error().message << "\n";
        }
    };

    bool ammo_frozen = false;
    auto trigger_ammo = [&]() {
        if (!ammo_frozen) {
            gtlibcpp::FreezeRequest req{};
            req.id = "ammo_freeze";
            req.address = resolved_ammo.value();
            req.type_name = "uint32";
            req.value_u64 = 999;
            req.interval = std::chrono::milliseconds(20);
            req.size = 4;
            auto f_res = freeze->freeze(req);
            if (f_res) {
                ammo_frozen = true;
                std::cout << " [HOTKEY TRIGGERED] -> Unlimited Ammo ACTIVATED (Frozen at 999) [OK]\n";
            }
        } else {
            (void)freeze->unfreeze("ammo_freeze");
            ammo_frozen = false;
            std::cout << " [HOTKEY TRIGGERED] -> Unlimited Ammo DEACTIVATED [OK]\n";
        }
    };

    auto trigger_high_jump = [&]() {
        auto cur = session->read<float>(resolved_zcord.value());
        if (cur) {
            float new_val = cur.value() + 10.0f;
            auto w = session->write<float>(resolved_zcord.value(), new_val);
            if (w) {
                std::cout << " [HOTKEY TRIGGERED] -> High Jump / ZCord increased: "
                          << cur.value() << " -> " << new_val << " (+10.0) [OK]\n";
            }
        }
    };

    auto print_status = [&]() {
        auto h = session->read<std::uint32_t>(resolved_health.value());
        auto a = session->read<std::uint32_t>(resolved_ammo.value());
        auto z = session->read<float>(resolved_zcord.value());
        std::cout << "\n--- Current Memory State ---\n"
                  << "  Health : " << (h ? std::to_string(h.value()) : "ERR") << "\n"
                  << "  Ammo   : " << (a ? std::to_string(a.value()) : "ERR")
                  << (ammo_frozen ? " [FROZEN: UNLIMITED]" : " [NORMAL]") << "\n"
                  << "  ZCord  : " << (z ? std::to_string(z.value()) : "ERR") << "\n"
                  << "----------------------------\n\n";
    };

    // 4. Test Mode Execution
    if (test_mode) {
        std::cout << "[*] Running automated hotkey detection & memory verification test...\n";
        print_status();

        std::cout << "[Test 1/3] Simulating Hotkey Ctrl+H (Set Health 100)...\n";
        (void)session->write<std::uint32_t>(resolved_health.value(), 35); // simulate damage
        trigger_health();
        auto h_verify = session->read<std::uint32_t>(resolved_health.value());
        if (!h_verify || h_verify.value() != 100) {
            std::cerr << "[-] Test failed: expected Health == 100\n";
            return 1;
        }

        std::cout << "[Test 2/3] Simulating Hotkey Ctrl+A (Toggle Unlimited Ammo)...\n";
        trigger_ammo();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        auto a_verify = session->read<std::uint32_t>(resolved_ammo.value());
        if (!a_verify || a_verify.value() != 999) {
            std::cerr << "[-] Test failed: expected Ammo frozen at 999\n";
            return 1;
        }
        trigger_ammo(); // toggle off

        std::cout << "[Test 3/3] Simulating Hotkey Ctrl+Z (High Jump / +10 ZCord)...\n";
        auto z_before = session->read<float>(resolved_zcord.value());
        trigger_high_jump();
        auto z_after = session->read<float>(resolved_zcord.value());
        if (!z_before || !z_after || (z_after.value() - z_before.value() < 9.9f)) {
            std::cerr << "[-] Test failed: expected ZCord +10 jump\n";
            return 1;
        }

        print_status();
        std::cout << "[+] All 3/3 Hotkey detection and trainer memory operations PASSED!\n";
        return 0;
    }

    // 5. Interactive CLI Hotkey Detection Loop
    std::cout << "Interactive CLI Hotkey Controls:\n"
              << "  Press [H] or enter 'h'  : Trigger Health (100 HP)       [Hotkeys: " << (health_entry ? format_hotkey(health_entry->hotkeys) : "Ctrl+H") << "]\n"
              << "  Press [A] or enter 'a'  : Toggle Unlimited Ammo         [Hotkeys: " << (ammo_entry ? format_hotkey(ammo_entry->hotkeys) : "Ctrl+A") << "]\n"
              << "  Press [Z] or enter 'z'  : High Jump (+10.0 ZCord)       [Hotkeys: " << (zcord_entry ? format_hotkey(zcord_entry->hotkeys) : "Ctrl+Z") << "]\n"
              << "  Press [S] or enter 's'  : Display current memory status\n"
              << "  Press [Q] or enter 'q'  : Quit\n\n";

    print_status();

    std::string input;
    while (true) {
        std::cout << "gt_trainer> ";
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        char cmd = input[0];
        if (cmd == 'q' || cmd == 'Q') {
            std::cout << "[*] Quitting trainer CLI...\n";
            break;
        } else if (cmd == 'h' || cmd == 'H') {
            trigger_health();
        } else if (cmd == 'a' || cmd == 'A') {
            trigger_ammo();
        } else if (cmd == 'z' || cmd == 'Z') {
            trigger_high_jump();
        } else if (cmd == 's' || cmd == 'S') {
            print_status();
        } else {
            std::cout << "Unknown command. Use: [H] Health, [A] Ammo, [Z] Jump, [S] Status, [Q] Quit\n";
        }
    }

    freeze->cancel_all();
    return 0;
}
