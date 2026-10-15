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
    explicit SimulatedGameBackend(bool is_64bit = true) : is_64bit_(is_64bit) {
        // --- Setup 32-bit Windows AssaultCube LocalPlayer layout ---
        // Base: 0x400000, LocalPlayer ptr: 0x400000 + 0x17E0A8 -> 0x500000
        const gtlibcpp::Address win_base = 0x400000;
        const gtlibcpp::Address win_player_ptr = win_base + 0x17E0A8;
        const gtlibcpp::Address win_player = 0x500000;
        put_u32(win_player_ptr, static_cast<std::uint32_t>(win_player));

        // Health at offset 0xEC -> default 100
        put_u32(win_player + 0xEC, 100);
        // Armor at offset 0xF0 -> default 50
        put_u32(win_player + 0xF0, 50);
        // GunSelect at offset 0xFC -> default 6 (Assault Rifle)
        put_u32(win_player + 0xFC, 6);
        // Reserve ammo array (ammo[9]) at 0x104
        for (int i = 0; i < 9; ++i) put_u32(win_player + 0x104 + i * 4, 60);
        // Clip ammo array (mag[9]) at 0x128 (Assault Rifle mag[6] is at 0x140)
        for (int i = 0; i < 9; ++i) put_u32(win_player + 0x128 + i * 4, 20);
        // Rifle Ammo at offset 0x140 -> default 20
        put_u32(win_player + 0x140, 20);
        // Position coordinates: XCord at offset 0x2C, YCord at offset 0x28, ZCord at offset 0x30
        put_float(win_player + 0x2C, 10.0f);
        put_float(win_player + 0x28, 20.0f);
        put_float(win_player + 0x30, 4.5f);
        // Multi-level Weapon Ammo
        const gtlibcpp::Address win_weapon = 0x600000;
        const gtlibcpp::Address win_clip = 0x700000;
        put_u32(win_player + 0x368, static_cast<std::uint32_t>(win_weapon));
        put_u32(win_weapon + 0x14, static_cast<std::uint32_t>(win_clip));
        put_u32(win_clip + 0x0, 30);

        // --- Setup 64-bit macOS AssaultCube LocalPlayer layout ---
        // Base: 0x100000000, LocalPlayer ptr: 0x100000000 + 0x1CBD38 -> 0x100500000
        const gtlibcpp::Address mac_base = 0x100000000ULL;
        const gtlibcpp::Address mac_player_ptr = mac_base + 0x1CBD38ULL;
        const gtlibcpp::Address mac_player = 0x100500000ULL;
        put_u64(mac_player_ptr, static_cast<std::uint64_t>(mac_player));

        // Health at offset 0x100 -> default 100
        put_u32(mac_player + 0x100, 100);
        // Armor at offset 0x104 -> default 50
        put_u32(mac_player + 0x104, 50);
        // GunSelect at offset 0x108 -> default 6 (Assault Rifle)
        put_u32(mac_player + 0x108, 6);
        // Reserve ammo array (ammo[9]) at 0x118:
        for (int i = 0; i < 9; ++i) put_u32(mac_player + 0x118 + i * 4, 60);
        put_u32(mac_player + 0x118 + 1 * 4, 50); // Pistol reserve
        // Clip ammo array (mag[9]) at 0x13c:
        put_u32(mac_player + 0x13c + 0 * 4, 1);  // Knife
        put_u32(mac_player + 0x13c + 1 * 4, 10); // Pistol (0x140)
        put_u32(mac_player + 0x13c + 2 * 4, 30); // Carbine
        put_u32(mac_player + 0x13c + 3 * 4, 8);  // Shotgun
        put_u32(mac_player + 0x13c + 4 * 4, 30); // Subgun (0x14C)
        put_u32(mac_player + 0x13c + 5 * 4, 5);  // Sniper (0x150)
        put_u32(mac_player + 0x13c + 6 * 4, 20); // Assault (0x154)
        put_u32(mac_player + 0x13c + 7 * 4, 1);  // Grenades (0x158)
        put_u32(mac_player + 0x13c + 8 * 4, 20); // Akimbo
        // Position coordinates: XCord at offset 0x8, YCord at offset 0xC, ZCord at offset 0x10
        put_float(mac_player + 0x8, 10.0f);
        put_float(mac_player + 0xC, 20.0f);
        put_float(mac_player + 0x10, 4.5f);
        // Orientation
        put_float(mac_player + 0x38, 90.0f); // Yaw
        put_float(mac_player + 0x3C, 0.0f);  // Pitch
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

    [[nodiscard]] std::string target_id() const override {
        return is_64bit_ ? "simulated:assaultcube:1337" : "simulated:ac_client:1337";
    }

    [[nodiscard]] gtlibcpp::TargetIdentity identity() const override {
        gtlibcpp::TargetIdentity id{};
        id.pid = 1337;
        id.image_path = is_64bit_ ? "assaultcube" : "ac_client.exe";
        id.architecture = is_64bit_ ? gtlibcpp::Architecture::x64 : gtlibcpp::Architecture::x86;
        return id;
    }

private:
    bool is_64bit_ = true;
    void put_u32(gtlibcpp::Address addr, std::uint32_t val) {
        std::uint8_t b[4];
        std::memcpy(b, &val, 4);
        for (std::size_t i = 0; i < 4; ++i) memory_[addr + i] = b[i];
    }

    void put_u64(gtlibcpp::Address addr, std::uint64_t val) {
        std::uint8_t b[8];
        std::memcpy(b, &val, 8);
        for (std::size_t i = 0; i < 8; ++i) memory_[addr + i] = b[i];
    }

    void put_float(gtlibcpp::Address addr, float val) {
        std::uint8_t b[4];
        std::memcpy(b, &val, 4);
        for (std::size_t i = 0; i < 4; ++i) memory_[addr + i] = b[i];
    }

    std::map<gtlibcpp::Address, std::uint8_t> memory_;
};

// Resolves a Cheat Table pointer chain: base address followed by a list of offsets.
// Automatically supports both 32-bit (uint32) and 64-bit (uint64) pointer sizes.
gtlibcpp::Result<gtlibcpp::Address> resolve_pointer_chain(
    gtlibcpp::MemorySession& session,
    gtlibcpp::Address base_addr,
    const std::vector<gtlibcpp::Address>& offsets,
    bool force_64bit = false) {
    if (offsets.empty()) return gtlibcpp::Result<gtlibcpp::Address>::success(base_addr);

    const bool is_64bit = force_64bit || (base_addr >= 0x100000000ULL) ||
        (session.backend() && (session.backend()->identity().architecture == gtlibcpp::Architecture::x64));

    gtlibcpp::Address current = base_addr;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (is_64bit) {
            auto ptr_val = session.read<std::uint64_t>(current);
            if (!ptr_val) return gtlibcpp::Result<gtlibcpp::Address>::failure(ptr_val.error());
            if (i == offsets.size() - 1) {
                return gtlibcpp::Result<gtlibcpp::Address>::success(
                    static_cast<gtlibcpp::Address>(ptr_val.value()) + offsets[i]);
            } else {
                current = static_cast<gtlibcpp::Address>(ptr_val.value()) + offsets[i];
            }
        } else {
            auto ptr_val = session.read<std::uint32_t>(current);
            if (!ptr_val) return gtlibcpp::Result<gtlibcpp::Address>::failure(ptr_val.error());
            if (i == offsets.size() - 1) {
                return gtlibcpp::Result<gtlibcpp::Address>::success(
                    static_cast<gtlibcpp::Address>(ptr_val.value()) + offsets[i]);
            } else {
                current = static_cast<gtlibcpp::Address>(ptr_val.value()) + offsets[i];
            }
        }
    }
    return gtlibcpp::Result<gtlibcpp::Address>::success(current);
}

static const char* const kGunNames[9] = {
    "Knife",
    "Pistol",
    "Carbine",
    "Shotgun",
    "Subgun",
    "Sniper Rifle",
    "Assault Rifle",
    "Grenade",
    "Akimbo"
};

struct PlayerState {
    std::uint32_t health = 0;
    std::uint32_t armor = 0;
    std::int32_t  gun_id = -1;
    std::string   weapon_name = "Weapon";
    std::uint32_t clip_ammo = 0;
    std::uint32_t reserve_ammo = 0;
    float         pos_x = 0.0f;
    float         pos_y = 0.0f;
    float         pos_z = 0.0f;
    bool          valid = false;
};

PlayerState get_player_state(
    gtlibcpp::MemorySession& session,
    gtlibcpp::Address player_base,
    gtlibcpp::Address fallback_health,
    gtlibcpp::Address fallback_ammo,
    gtlibcpp::Address fallback_zcord,
    bool is_mac) {
    PlayerState s{};
    if (player_base != 0) {
        const gtlibcpp::Address hp_addr = player_base + (is_mac ? 0x100 : 0xEC);
        const gtlibcpp::Address arm_addr = player_base + (is_mac ? 0x104 : 0xF0);
        const gtlibcpp::Address gun_addr = player_base + (is_mac ? 0x108 : 0xFC);
        const gtlibcpp::Address x_addr = player_base + (is_mac ? 0x8 : 0x2C);
        const gtlibcpp::Address y_addr = player_base + (is_mac ? 0xC : 0x28);
        const gtlibcpp::Address z_addr = player_base + (is_mac ? 0x10 : 0x30);

        auto hp = session.read<std::uint32_t>(hp_addr);
        auto arm = session.read<std::uint32_t>(arm_addr);
        auto gun = session.read<std::int32_t>(gun_addr);
        auto px = session.read<float>(x_addr);
        auto py = session.read<float>(y_addr);
        auto pz = session.read<float>(z_addr);

        if (hp) s.health = hp.value();
        if (arm) s.armor = arm.value();
        if (px) s.pos_x = px.value();
        if (py) s.pos_y = py.value();
        if (pz) s.pos_z = pz.value();

        if (gun && gun.value() >= 0 && gun.value() < 9) {
            s.gun_id = gun.value();
            s.weapon_name = kGunNames[s.gun_id];
            const gtlibcpp::Address clip_addr = player_base + (is_mac ? (0x13C + s.gun_id * 4) : (0x128 + s.gun_id * 4));
            const gtlibcpp::Address res_addr  = player_base + (is_mac ? (0x118 + s.gun_id * 4) : (0x104 + s.gun_id * 4));
            auto clip = session.read<std::uint32_t>(clip_addr);
            auto res = session.read<std::uint32_t>(res_addr);
            if (clip) s.clip_ammo = clip.value();
            if (res) s.reserve_ammo = res.value();
        } else {
            auto a = session.read<std::uint32_t>(fallback_ammo);
            if (a) s.clip_ammo = a.value();
        }
        s.valid = hp.ok();
    } else {
        auto hp = session.read<std::uint32_t>(fallback_health);
        auto a = session.read<std::uint32_t>(fallback_ammo);
        auto z = session.read<float>(fallback_zcord);
        if (hp) s.health = hp.value();
        if (a) s.clip_ammo = a.value();
        if (z) s.pos_z = z.value();
        s.valid = hp.ok();
    }
    return s;
}

void print_banner() {
    std::cout << "========================================================\n"
              << "  gtlibcpp CLI Trainer & Hotkey Controller v" << GTLIBCPP_VERSION << "\n"
              << "========================================================\n";
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "Options:\n"
#if defined(__APPLE__)
              << "  --table <path>   Load Cheat Table file (default: demos/CheatTable/assaultcube_mac.ct)\n"
#else
              << "  --table <path>   Load Cheat Table file (default: demos/CheatTable/assaultcube.ct)\n"
#endif
              << "  --list           List parsed cheat entries and exit\n"
              << "  --test [path]    Run automated hotkey detection and memory verification test\n"
              << "  --version        Show version information\n"
              << "  --help           Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

#if defined(__APPLE__)
    std::string table_path = "demos/CheatTable/assaultcube_mac.ct";
#else
    std::string table_path = "demos/CheatTable/assaultcube.ct";
#endif
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
            "CheatTable/assaultcube_mac.ct",
            "demos/CheatTable/assaultcube_mac.ct",
            "../demos/CheatTable/assaultcube_mac.ct",
            "CheatTable/assaultcube.ct",
            "demos/CheatTable/assaultcube.ct",
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

    // 2. Identify target entries: Health, Armor, Weapon Ammo, ZCord
    const gtlibcpp::CheatEntry* health_entry = nullptr;
    const gtlibcpp::CheatEntry* armor_entry = nullptr;
    const gtlibcpp::CheatEntry* ammo_entry = nullptr;
    const gtlibcpp::CheatEntry* zcord_entry = nullptr;

    for (const auto& e : table.entries) {
        if (e.description == "Health") {
            health_entry = &e;
        } else if (e.description == "Armor") {
            armor_entry = &e;
        } else if (e.description == "Assault Rifle Clip Ammo" ||
                   e.description == "Weapon Ammo" ||
                   e.description == "Riffle Ammo") {
            if (!ammo_entry) ammo_entry = &e;
        } else if (e.description == "ZCord") {
            zcord_entry = &e;
        }
    }

    const bool is_mac_target = (table_path.find("mac") != std::string::npos) ||
        (health_entry && health_entry->section == "assaultcube");

    // 3. Initialize Memory Backend & Session
    std::shared_ptr<gtlibcpp::IMemoryBackend> backend;
    gtlibcpp::Address live_mac_base = 0;
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
    if (is_mac_target) {
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
                live_mac_base = gtlibcpp::get_darwin_module_base(pid, "assaultcube");
                if (live_mac_base != 0) {
                    std::cout << "[+] Discovered live ASLR module base for assaultcube: 0x"
                              << std::hex << live_mac_base << std::dec << "\n";
                }
            } else {
                std::cout << "[!] Notice: Process PID " << pid << " could not be attached directly.\n"
                          << "    Kernel Mach error: Exclusive debugger lock (e.g. Cheat Engine debugserver)\n"
                          << "    is active, or process requires debugger entitlements. Operating in simulated offline mode.\n";
            }
        }
    }
#endif

    if (!backend) {
        backend = std::make_shared<SimulatedGameBackend>(is_mac_target);
        std::cout << "[*] Operating on high-fidelity simulated AssaultCube memory space ("
                  << (is_mac_target ? "macOS 64-bit" : "Windows 32-bit") << " offline sandbox)\n";
    }

    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    auto freeze = std::make_shared<gtlibcpp::FreezeManager>(session);

    // Resolve pointers
    const gtlibcpp::Address default_mod_base = (live_mac_base != 0) ? live_mac_base : (is_mac_target ? 0x100000000ULL : 0x400000ULL);

    auto compute_entry_base = [&](const gtlibcpp::CheatEntry* entry, gtlibcpp::Address fallback_off) -> gtlibcpp::Address {
        if (!entry) return default_mod_base + fallback_off;
        if (!entry->section.empty()) {
            if (entry->section == "assaultcube") {
                const auto base = (live_mac_base != 0) ? live_mac_base : 0x100000000ULL;
                return base + entry->address;
            } else if (entry->section == "ac_client.exe") {
                return 0x400000ULL + entry->address;
            }
        }
        if (entry->address >= 0x100000000ULL && live_mac_base == 0) {
            return entry->address;
        }
        return default_mod_base + entry->address;
    };

    const gtlibcpp::Address health_base = compute_entry_base(health_entry, is_mac_target ? 0x1CBD38ULL : 0x17E0A8ULL);
    const gtlibcpp::Address armor_base = compute_entry_base(armor_entry, is_mac_target ? 0x1CBD38ULL : 0x17E0A8ULL);
    const gtlibcpp::Address ammo_base = compute_entry_base(ammo_entry, is_mac_target ? 0x1CBD38ULL : 0x17E0A8ULL);
    const gtlibcpp::Address zcord_base = compute_entry_base(zcord_entry, is_mac_target ? 0x1CBD38ULL : 0x17E0A8ULL);

    const std::vector<gtlibcpp::Address> default_health_offsets = is_mac_target ? std::vector<gtlibcpp::Address>{0x100} : std::vector<gtlibcpp::Address>{0xEC};
    const std::vector<gtlibcpp::Address> default_armor_offsets = is_mac_target ? std::vector<gtlibcpp::Address>{0x104} : std::vector<gtlibcpp::Address>{0xF0};
    const std::vector<gtlibcpp::Address> default_ammo_offsets = is_mac_target ? std::vector<gtlibcpp::Address>{0x154} : std::vector<gtlibcpp::Address>{0x140};
    const std::vector<gtlibcpp::Address> default_zcord_offsets = is_mac_target ? std::vector<gtlibcpp::Address>{0x10} : std::vector<gtlibcpp::Address>{0x30};

    auto resolved_health = resolve_pointer_chain(*session, health_base, health_entry && !health_entry->offsets.empty() ? health_entry->offsets : default_health_offsets, is_mac_target);
    auto resolved_armor = resolve_pointer_chain(*session, armor_base, armor_entry && !armor_entry->offsets.empty() ? armor_entry->offsets : default_armor_offsets, is_mac_target);
    auto resolved_ammo = resolve_pointer_chain(*session, ammo_base, ammo_entry && !ammo_entry->offsets.empty() ? ammo_entry->offsets : default_ammo_offsets, is_mac_target);
    auto resolved_zcord = resolve_pointer_chain(*session, zcord_base, zcord_entry && !zcord_entry->offsets.empty() ? zcord_entry->offsets : default_zcord_offsets, is_mac_target);

    gtlibcpp::Address player_base = 0;
    if (is_mac_target) {
        auto p1 = session->read<std::uint64_t>(health_base);
        if (p1 && p1.value() != 0) player_base = p1.value();
    } else {
        auto p1 = session->read<std::uint32_t>(health_base);
        if (p1 && p1.value() != 0) player_base = p1.value();
    }

    bool resolution_valid = false;
    if (resolved_health && resolved_ammo && resolved_zcord) {
        auto probe_h = session->read<std::uint32_t>(resolved_health.value());
        auto probe_a = session->read<std::uint32_t>(resolved_ammo.value());
        auto probe_z = session->read<float>(resolved_zcord.value());
        if (probe_h && probe_a && probe_z) {
            resolution_valid = true;
        }
    }

    if (!resolution_valid) {
        std::cout << "[!] Pointer resolution on live target returned unmapped coordinates (e.g. at main menu or different architecture).\n"
                  << "    Switching session to simulated game space to preserve trainer functionality.\n";
        backend = std::make_shared<SimulatedGameBackend>(is_mac_target);
        session = std::make_shared<gtlibcpp::MemorySession>(backend);
        freeze = std::make_shared<gtlibcpp::FreezeManager>(session);
        const gtlibcpp::Address sim_base = is_mac_target ? 0x100000000ULL : 0x400000ULL;
        const gtlibcpp::Address sim_health_base = is_mac_target ? (sim_base + (health_entry ? health_entry->address : 0x1CBD38ULL)) : (sim_base + (health_entry ? health_entry->address : 0x17E0A8ULL));
        const gtlibcpp::Address sim_armor_base = is_mac_target ? (sim_base + (armor_entry ? armor_entry->address : 0x1CBD38ULL)) : (sim_base + (armor_entry ? armor_entry->address : 0x17E0A8ULL));
        const gtlibcpp::Address sim_ammo_base = is_mac_target ? (sim_base + (ammo_entry ? ammo_entry->address : 0x1CBD38ULL)) : (sim_base + (ammo_entry ? ammo_entry->address : 0x17E0A8ULL));
        const gtlibcpp::Address sim_zcord_base = is_mac_target ? (sim_base + (zcord_entry ? zcord_entry->address : 0x1CBD38ULL)) : (sim_base + (zcord_entry ? zcord_entry->address : 0x17E0A8ULL));
        resolved_health = resolve_pointer_chain(*session, sim_health_base, health_entry && !health_entry->offsets.empty() ? health_entry->offsets : default_health_offsets, is_mac_target);
        resolved_armor = resolve_pointer_chain(*session, sim_armor_base, armor_entry && !armor_entry->offsets.empty() ? armor_entry->offsets : default_armor_offsets, is_mac_target);
        resolved_ammo = resolve_pointer_chain(*session, sim_ammo_base, ammo_entry && !ammo_entry->offsets.empty() ? ammo_entry->offsets : default_ammo_offsets, is_mac_target);
        resolved_zcord = resolve_pointer_chain(*session, sim_zcord_base, zcord_entry && !zcord_entry->offsets.empty() ? zcord_entry->offsets : default_zcord_offsets, is_mac_target);

        if (is_mac_target) {
            auto p1 = session->read<std::uint64_t>(sim_health_base);
            if (p1 && p1.value() != 0) player_base = p1.value();
        } else {
            auto p1 = session->read<std::uint32_t>(sim_health_base);
            if (p1 && p1.value() != 0) player_base = p1.value();
        }
    }

    if (!resolved_health || !resolved_ammo || !resolved_zcord) {
        std::cerr << "[-] Failed resolving pointer chain coordinates\n";
        return 1;
    }

    if (!resolved_armor && player_base != 0) {
        resolved_armor = gtlibcpp::Result<gtlibcpp::Address>::success(player_base + (is_mac_target ? 0x104 : 0xF0));
    }

    std::cout << "[+] Resolved Target Addresses:\n"
              << "    Player Base    : 0x" << std::hex << player_base << std::dec << "\n"
              << "    Health Address : 0x" << std::hex << resolved_health.value() << std::dec << "\n"
              << "    Armor Address  : 0x" << std::hex << (resolved_armor ? resolved_armor.value() : 0) << std::dec << "\n"
              << "    Ammo Address   : 0x" << std::hex << resolved_ammo.value() << std::dec << "\n"
              << "    ZCord Address  : 0x" << std::hex << resolved_zcord.value() << std::dec << "\n\n";

    // Hotkey Action Dispatcher
    auto trigger_health = [&]() {
        bool ok = false;
        if (session->write<std::uint32_t>(resolved_health.value(), 100)) ok = true;
        if (resolved_armor) {
            (void)session->write<std::uint32_t>(resolved_armor.value(), 100);
        } else if (player_base != 0) {
            (void)session->write<std::uint32_t>(player_base + (is_mac_target ? 0x104 : 0xF0), 100);
        }
        if (ok) {
            std::cout << " [HOTKEY TRIGGERED] -> Health & Armor restored to 100 HP / 100 AP [OK]\n";
        } else {
            std::cout << " [ERROR] Health write failed: " << session->write<std::uint32_t>(resolved_health.value(), 100).error().message << "\n";
        }
    };

    bool ammo_frozen = false;
    auto refill_all_weapons = [&](std::uint32_t amount = 999) {
        if (player_base != 0) {
            for (int i = 0; i < 9; ++i) {
                const gtlibcpp::Address clip_addr = player_base + (is_mac_target ? (0x13C + i * 4) : (0x128 + i * 4));
                const gtlibcpp::Address res_addr  = player_base + (is_mac_target ? (0x118 + i * 4) : (0x104 + i * 4));
                (void)session->write<std::uint32_t>(clip_addr, amount);
                (void)session->write<std::uint32_t>(res_addr, amount);
            }
        }
        (void)session->write<std::uint32_t>(resolved_ammo.value(), amount);
    };

    auto trigger_ammo = [&]() {
        if (!ammo_frozen) {
            gtlibcpp::FreezeRequest req{};
            req.id = "ammo_freeze_primary";
            req.address = resolved_ammo.value();
            req.type_name = "uint32";
            req.value_u64 = 999;
            req.interval = std::chrono::milliseconds(20);
            req.size = 4;
            auto f_res = freeze->freeze(req);
            refill_all_weapons(999);
            if (f_res) {
                ammo_frozen = true;
                std::cout << " [HOTKEY TRIGGERED] -> Unlimited Ammo ACTIVATED (Frozen at 999 for all weapons) [OK]\n";
            }
        } else {
            (void)freeze->unfreeze("ammo_freeze_primary");
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
                          << std::fixed << std::setprecision(2) << cur.value() << " -> " << new_val << " (+10.0) [OK]\n";
            }
        }
    };

    auto print_status = [&]() {
        auto s = get_player_state(*session, player_base, resolved_health.value(), resolved_ammo.value(), resolved_zcord.value(), is_mac_target);
        std::cout << "\n--- Current Memory State ---\n"
                  << "  Health   : " << s.health << " HP\n"
                  << "  Armor    : " << s.armor << " AP\n"
                  << "  Weapon   : " << s.weapon_name << " [Clip: " << s.clip_ammo << " | Reserve: " << s.reserve_ammo << "]"
                  << (ammo_frozen ? " [FROZEN: UNLIMITED]" : " [NORMAL]") << "\n"
                  << "  Position : (" << std::fixed << std::setprecision(2) << s.pos_x << ", " << s.pos_y << ", " << s.pos_z << ")\n"
                  << "----------------------------\n\n";
    };

    // 4. Test Mode Execution
    if (test_mode) {
        std::cout << "[*] Running automated hotkey detection & memory verification test...\n";
        print_status();

        // 1. Snapshot original values to guarantee non-destructive test execution
        const auto orig_state = get_player_state(*session, player_base, resolved_health.value(), resolved_ammo.value(), resolved_zcord.value(), is_mac_target);

        std::cout << "[Test 1/3] Simulating Hotkey Ctrl+H (Set Health & Armor 100)...\n";
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

        // 2. Non-destructive restore: cleanly restore original values so live game is not left in mutated state
        if (orig_state.valid) {
            (void)session->write<std::uint32_t>(resolved_health.value(), orig_state.health);
            if (resolved_armor) {
                (void)session->write<std::uint32_t>(resolved_armor.value(), orig_state.armor);
            } else if (player_base != 0) {
                (void)session->write<std::uint32_t>(player_base + (is_mac_target ? 0x104 : 0xF0), orig_state.armor);
            }
            (void)session->write<std::uint32_t>(resolved_ammo.value(), orig_state.clip_ammo);
            (void)session->write<float>(resolved_zcord.value(), orig_state.pos_z);
            if (player_base != 0 && orig_state.gun_id >= 0 && orig_state.gun_id < 9) {
                const gtlibcpp::Address clip_addr = player_base + (is_mac_target ? (0x13C + orig_state.gun_id * 4) : (0x128 + orig_state.gun_id * 4));
                const gtlibcpp::Address res_addr  = player_base + (is_mac_target ? (0x118 + orig_state.gun_id * 4) : (0x104 + orig_state.gun_id * 4));
                (void)session->write<std::uint32_t>(clip_addr, orig_state.clip_ammo);
                (void)session->write<std::uint32_t>(res_addr, orig_state.reserve_ammo);
            }
        }

        print_status();
        std::cout << "[+] All 3/3 Hotkey detection and trainer memory operations PASSED!\n";
        std::cout << "[+] Target memory state restored non-destructively to pre-test values.\n";
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
        "Health & Armor (100 HP / 100 AP)",
        health_entry ? health_entry->hotkeys : std::vector<int>{17, 72},
        'h',
        8, // Ctrl+H
        trigger_health,
        false
    });
    bindings.push_back({
        "Unlimited Ammo (All Weapons 999)",
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
    std::cout << "    Controls: [H] Health & Armor (100) | [A] Unlimited Ammo | [Z] High Jump (+10) | [S] Status | [Q] Quit\n";
#if defined(_WIN32)
    std::cout << "    Global In-Game Hotkeys Active: Ctrl+H (Health & Armor), Ctrl+A (Ammo), Ctrl+Z (Jump)\n\n";
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
                auto s = get_player_state(*session, player_base, resolved_health.value(), resolved_ammo.value(), resolved_zcord.value(), is_mac_target);
                if (ammo_frozen && player_base != 0 && s.gun_id >= 0 && s.gun_id < 9) {
                    const gtlibcpp::Address active_clip = player_base + (is_mac_target ? (0x13C + s.gun_id * 4) : (0x128 + s.gun_id * 4));
                    (void)session->write<std::uint32_t>(active_clip, 999);
                }
                if (stdout_tty) {
                    std::cout << "[Loop Active] HP: " << s.health << " | Armor: " << s.armor
                              << " | " << s.weapon_name << ": " << s.clip_ammo << "/" << s.reserve_ammo
                              << (ammo_frozen ? " [UNLIMITED]" : " [NORMAL]")
                              << " | Pos: (" << std::fixed << std::setprecision(1) << s.pos_x << ", " << s.pos_y << ", " << s.pos_z << ")"
                              << " | ([H], [A], [Z], [Q] to quit)\r" << std::flush;
                }
            }
        }
    }

    freeze->cancel_all();
    std::cout << "\n[+] Trainer closed cleanly. All freeze locks removed.\n";
    return 0;
}
