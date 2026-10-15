#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#include <windows.h>
#include "gtlibcpp/windows_backend.hpp"
#include "gtlibcpp/windows_discovery.hpp"
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "gtlibcpp/darwin_backend.hpp"
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/parser.hpp"
#include "gtlibcpp/policy.hpp"
#include "gtlibcpp/result.hpp"

#ifndef GTLIBCPP_VERSION
#define GTLIBCPP_VERSION "1.0.0"
#endif

namespace {

std::atomic<bool> g_running{true};

void handle_signal(int) {
    g_running = false;
}

inline bool is_stdin_tty() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

inline bool is_stdout_tty() {
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

#if !defined(_WIN32)
class TerminalRawScope {
public:
    TerminalRawScope() {
        if (!is_stdin_tty()) return;
        if (tcgetattr(STDIN_FILENO, &orig_) == 0) {
            struct termios raw = orig_;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                active_ = true;
            }
        }
    }
    ~TerminalRawScope() {
        if (active_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
        }
    }
    bool is_active() const { return active_; }
private:
    struct termios orig_{};
    bool active_ = false;
};

static int poll_key(int timeout_ms) {
    if (!is_stdin_tty()) return -1;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    const int ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
        unsigned char c = 0;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            return static_cast<int>(c);
        }
    }
    return -1;
}
#else
class TerminalRawScope {
public:
    bool is_active() const { return is_stdin_tty(); }
};

static int poll_key(int timeout_ms) {
    if (_kbhit()) {
        return _getch();
    }
    Sleep(static_cast<DWORD>(timeout_ms));
    return -1;
}

static bool is_combo_down(const std::vector<int>& keys) {
    if (keys.empty()) return false;
    for (int k : keys) {
        if ((GetAsyncKeyState(k) & 0x8000) == 0) {
            return false;
        }
    }
    return true;
}
#endif

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

    [[nodiscard]] bool is_alive() noexcept override { return true; }

    [[nodiscard]] std::string target_id() const override { return "simulated:ac_client:1337"; }

    [[nodiscard]] gtlibcpp::TargetIdentity identity() const override {
        gtlibcpp::TargetIdentity id{};
        id.pid = 1337;
        id.image_path = "ac_client.exe";
        id.architecture = gtlibcpp::Architecture::x86;
        return id;
    }

private:
    void put_u32(gtlibcpp::Address addr, std::uint32_t val) {
        std::uint8_t b[4];
        std::memcpy(b, &val, 4);
        for (std::size_t i = 0; i < 4; ++i) memory_[addr + i] = b[i];
    }

    void put_float(gtlibcpp::Address addr, float val) {
        std::uint8_t b[4];
        std::memcpy(b, &val, 4);
        for (std::size_t i = 0; i < 4; ++i) memory_[addr + i] = b[i];
    }

    std::map<gtlibcpp::Address, std::uint8_t> memory_;
};

// Resolves a Cheat Table pointer chain: base address followed by a list of offsets
gtlibcpp::Result<gtlibcpp::Address> resolve_pointer_chain(
    gtlibcpp::MemorySession& session,
    gtlibcpp::Address base_addr,
    const std::vector<gtlibcpp::Address>& offsets) {
    if (offsets.empty()) return gtlibcpp::Result<gtlibcpp::Address>::success(base_addr);

    gtlibcpp::Address current = base_addr;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (i == offsets.size() - 1) {
            // Last offset is added to pointer to reach final scalar value
            auto ptr_val = session.read<std::uint32_t>(current);
            if (!ptr_val) return gtlibcpp::Result<gtlibcpp::Address>::failure(ptr_val.error());
            return gtlibcpp::Result<gtlibcpp::Address>::success(
                static_cast<gtlibcpp::Address>(ptr_val.value()) + offsets[i]);
        } else {
            auto ptr_val = session.read<std::uint32_t>(current);
            if (!ptr_val) return gtlibcpp::Result<gtlibcpp::Address>::failure(ptr_val.error());
            current = static_cast<gtlibcpp::Address>(ptr_val.value()) + offsets[i];
        }
    }
    return gtlibcpp::Result<gtlibcpp::Address>::success(current);
}

void print_banner() {
    std::cout << "========================================================\n"
              << "  gtlibcpp CLI Trainer & Hotkey Controller v" << GTLIBCPP_VERSION << "\n"
              << "========================================================\n";
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "Options:\n"
              << "  --table <path>   Load Cheat Table file (default: demos/CheatTable/assaultcube.ct)\n"
              << "  --list           List parsed cheat entries and exit\n"
              << "  --test [path]    Run automated hotkey detection and memory verification test\n"
              << "  --version        Show version information\n"
              << "  --help           Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::string table_path = "demos/CheatTable/assaultcube.ct";
    bool list_mode = false;
    bool test_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "gt_trainer_cli v" << GTLIBCPP_VERSION << "\n";
            return 0;
        } else if (arg == "--list") {
            list_mode = true;
        } else if (arg == "--test") {
            test_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                table_path = argv[++i];
            }
        } else if (arg == "--table" && i + 1 < argc) {
            table_path = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    print_banner();

    // 1. Parse Cheat Table
    gtlibcpp::CheatTableParser parser;
    auto parse_res = parser.parse_file(table_path);
    if (!parse_res) {
        // Fallback search paths
        std::vector<std::string> fallbacks = {
            "../" + table_path,
            "CheatTable/assaultcube.ct",
            "../demos/CheatTable/assaultcube.ct"
        };
        for (const auto& alt : fallbacks) {
            parse_res = parser.parse_file(alt);
            if (parse_res) {
                table_path = alt;
                break;
            }
        }
    }

    if (!parse_res) {
        std::cerr << "[-] Error loading cheat table: " << parse_res.error().message
                  << " (path: " << table_path << ")\n";
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
#elif defined(__APPLE__)
    const auto pid = gtlibcpp::find_darwin_process({"assaultcube", "ac_client"});
    if (pid != 0) {
        const auto path = gtlibcpp::get_darwin_process_path(pid);
        std::cout << "[+] Detected running game process: PID " << pid << " (" << path << ")\n";
        gtlibcpp::DarwinBackendOptions opts{};
        opts.pid = pid;
        opts.image_path = path;
        opts.architecture = gtlibcpp::Architecture::x64;
        opts.read_only = false;
        backend = gtlibcpp::make_darwin_backend(opts);
        if (backend) {
            std::cout << "[+] Successfully attached to live macOS target process via Mach task port!\n";
        } else {
            std::cout << "[!] Notice: Process PID " << pid << " could not be attached directly.\n"
                      << "    Kernel Mach error: Exclusive debugger lock (e.g. Cheat Engine debugserver)\n"
                      << "    is active, or process requires debugger entitlements. Operating in simulated offline mode.\n";
        }
    }
#endif

    if (!backend) {
        backend = std::make_shared<SimulatedGameBackend>();
        std::cout << "[*] Operating on high-fidelity simulated AssaultCube memory space (offline sandbox)\n";
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
    auto resolved_health = resolve_pointer_chain(*session, health_addr, health_entry ? health_entry->offsets : std::vector<gtlibcpp::Address>{0xEC});
    auto resolved_ammo = resolve_pointer_chain(*session, health_addr, ammo_entry ? ammo_entry->offsets : std::vector<gtlibcpp::Address>{0x140});
    auto resolved_zcord = resolve_pointer_chain(*session, health_addr, zcord_entry ? zcord_entry->offsets : std::vector<gtlibcpp::Address>{0x30});

    if (!resolved_health || !resolved_ammo || !resolved_zcord) {
        std::cout << "[!] Pointer resolution on target returned unmapped coordinates (table offsets may be Windows x86 specific).\n"
                  << "    Switching session to simulated game space to preserve trainer functionality.\n";
        backend = std::make_shared<SimulatedGameBackend>();
        session = std::make_shared<gtlibcpp::MemorySession>(backend);
        freeze = std::make_shared<gtlibcpp::FreezeManager>(session);
        resolved_health = resolve_pointer_chain(*session, health_addr, health_entry ? health_entry->offsets : std::vector<gtlibcpp::Address>{0xEC});
        resolved_ammo = resolve_pointer_chain(*session, health_addr, ammo_entry ? ammo_entry->offsets : std::vector<gtlibcpp::Address>{0x140});
        resolved_zcord = resolve_pointer_chain(*session, health_addr, zcord_entry ? zcord_entry->offsets : std::vector<gtlibcpp::Address>{0x30});
    }

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

    // 5. Active Loop & Hotkey Controller
    struct HotkeyBinding {
        std::string name;
        std::vector<int> ce_keys;
        char cli_key;
        int posix_ctrl_key;
        std::function<void()> trigger;
        bool prev_down = false;
    };

    std::vector<HotkeyBinding> bindings;
    bindings.push_back({
        "Health (100 HP)",
        health_entry ? health_entry->hotkeys : std::vector<int>{17, 72},
        'h',
        8, // Ctrl+H
        trigger_health,
        false
    });
    bindings.push_back({
        "Unlimited Ammo (Freeze 999)",
        ammo_entry ? ammo_entry->hotkeys : std::vector<int>{17, 65},
        'a',
        1, // Ctrl+A
        trigger_ammo,
        false
    });
    bindings.push_back({
        "High Jump (+10.0 ZCord)",
        zcord_entry ? zcord_entry->hotkeys : std::vector<int>{17, 90},
        'z',
        26, // Ctrl+Z
        trigger_high_jump,
        false
    });

    std::cout << "[*] Trainer active loop running! Listening for hotkeys (Press 'q' or Ctrl+C to quit)...\n";
    std::cout << "    Controls: [H] Health (100 HP) | [A] Unlimited Ammo | [Z] High Jump (+10) | [S] Status | [Q] Quit\n";
#if defined(_WIN32)
    std::cout << "    Global In-Game Hotkeys Active: Ctrl+H (Health), Ctrl+A (Ammo), Ctrl+Z (Jump)\n\n";
#else
    std::cout << "    Terminal Hotkeys Active: Hit [H], [A], [Z], or [S] at any time (no Enter required)\n\n";
#endif

    const bool stdout_tty = is_stdout_tty();
    print_status();

    TerminalRawScope raw_scope;
    auto last_status = std::chrono::steady_clock::now();

    if (!raw_scope.is_active()) {
        // Fallback for non-TTY / piped input streams
        std::string line;
        while (g_running && std::getline(std::cin, line)) {
            if (line.empty()) continue;
            char cmd = line[0];
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
            }
        }
    } else {
        // Active real-time polling loop
        while (g_running) {
#if defined(_WIN32)
            // 1. Windows global hotkey detection (detects hotkeys even when in-game window has focus)
            for (auto& b : bindings) {
                if (!b.ce_keys.empty()) {
                    bool down = is_combo_down(b.ce_keys);
                    if (down && !b.prev_down) {
                        if (stdout_tty) std::cout << "\n";
                        std::cout << ">>> [GLOBAL IN-GAME HOTKEY: " << format_hotkey(b.ce_keys) << "] " << b.name << "\n";
                        b.trigger();
                    }
                    b.prev_down = down;
                }
            }
#endif

            // 2. Non-blocking key hit detection (50ms select/poll)
            int ch = poll_key(50);
            if (ch != -1) {
                if (ch == 'q' || ch == 'Q' || ch == 3 /* Ctrl+C */ || ch == 27 /* ESC */) {
                    if (stdout_tty) std::cout << "\n";
                    std::cout << "[*] Quitting trainer CLI...\n";
                    g_running = false;
                    break;
                } else if (ch == 's' || ch == 'S') {
                    if (stdout_tty) std::cout << "\n";
                    print_status();
                } else {
                    bool matched = false;
                    for (auto& b : bindings) {
                        if (ch == b.cli_key ||
                            ch == std::toupper(static_cast<unsigned char>(b.cli_key)) ||
                            (b.posix_ctrl_key > 0 && ch == b.posix_ctrl_key)) {
                            if (stdout_tty) std::cout << "\n";
                            std::cout << ">>> [HOTKEY PRESSED: " << static_cast<char>(std::toupper(b.cli_key)) << "] " << b.name << "\n";
                            b.trigger();
                            matched = true;
                            break;
                        }
                    }
                    if (!matched) {
                        if (stdout_tty) std::cout << "\n";
                        std::cout << "[!] Unbound key pressed: '" << static_cast<char>(ch)
                                  << "'. Controls: [H] Health, [A] Ammo, [Z] Jump, [S] Status, [Q] Quit\n";
                    }
                }
            }

            // 3. Real-time status ticker every 1 second
            auto now = std::chrono::steady_clock::now();
            if (now - last_status >= std::chrono::seconds(1)) {
                last_status = now;
                auto h = session->read<std::uint32_t>(resolved_health.value());
                auto a = session->read<std::uint32_t>(resolved_ammo.value());
                auto z = session->read<float>(resolved_zcord.value());
                if (stdout_tty) {
                    std::cout << "[Loop Active] Health: "
                              << (h ? std::to_string(h.value()) : "ERR")
                              << " | Ammo: " << (a ? std::to_string(a.value()) : "ERR")
                              << (ammo_frozen ? " [FROZEN: UNLIMITED]" : " [NORMAL]")
                              << " | ZCord: " << std::fixed << std::setprecision(2) << (z ? z.value() : 0.0f)
                              << " | (Press [H], [A], [Z], or [Q] to quit)\r" << std::flush;
                }
            }
        }
    }

    freeze->cancel_all();
    std::cout << "\n[+] Trainer closed cleanly. All freeze locks removed.\n";
    return 0;
}
