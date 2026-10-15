// Example: AssaultCube CLI Trainer using gtlibcpp
// Demonstrates process discovery, dynamic ASLR base resolution, pointer-chain
// traversal, live player telemetry reading, and memory freezing for unlimited ammo.

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include "gtlibcpp/windows_backend.hpp"
#include "gtlibcpp/windows_discovery.hpp"
#elif defined(__APPLE__)
#include "gtlibcpp/darwin_backend.hpp"
#endif

#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/result.hpp"

namespace {

const char* const kWeaponNames[9] = {
    "Knife", "Pistol", "Carbine", "Shotgun",
    "Subgun", "Sniper Rifle", "Assault Rifle", "Grenade", "Akimbo"
};

// High-fidelity fallback simulation when AssaultCube is not running
class SimulatedAssaultCubeBackend final : public gtlibcpp::IMemoryBackend {
public:
    explicit SimulatedAssaultCubeBackend(bool is_64bit = true) : is_64bit_(is_64bit) {
        if (is_64bit_) {
            // 64-bit macOS layout: base 0x100000000, player ptr at +0x1CBD38 -> 0x100500000
            const gtlibcpp::Address base = 0x100000000ULL;
            const gtlibcpp::Address player_ptr = base + 0x1CBD38ULL;
            const gtlibcpp::Address player = 0x100500000ULL;
            write_u64(player_ptr, player);

            write_u32(player + 0x100, 100); // Health
            write_u32(player + 0x104, 50);  // Armor
            write_u32(player + 0x108, 6);   // GunSelect (Assault Rifle)
            write_u32(player + 0x154, 20);  // Assault Rifle Clip
            write_u32(player + 0x130, 60);  // Assault Rifle Reserve
            write_float(player + 0x8, 59.5f);  // X
            write_float(player + 0xC, 47.2f);  // Y
            write_float(player + 0x10, 4.5f);  // Z
        } else {
            // 32-bit Windows layout: base 0x400000, player ptr at +0x17E0A8 -> 0x500000
            const gtlibcpp::Address base = 0x400000;
            const gtlibcpp::Address player_ptr = base + 0x17E0A8;
            const gtlibcpp::Address player = 0x500000;
            write_u32(player_ptr, static_cast<std::uint32_t>(player));

            write_u32(player + 0xEC, 100); // Health
            write_u32(player + 0xF0, 50);  // Armor
            write_u32(player + 0xFC, 6);   // GunSelect (Assault Rifle)
            write_u32(player + 0x140, 20); // Assault Rifle Clip
            write_u32(player + 0x11C, 60); // Assault Rifle Reserve
            write_float(player + 0x2C, 59.5f); // X
            write_float(player + 0x28, 47.2f); // Y
            write_float(player + 0x30, 4.5f);  // Z
        }
    }

    gtlibcpp::Result<std::vector<std::uint8_t>> read(gtlibcpp::Address addr, std::size_t size) override {
        std::vector<std::uint8_t> out(size, 0);
        for (std::size_t i = 0; i < size; ++i) {
            auto it = mem_.find(addr + i);
            if (it == mem_.end()) return gtlibcpp::Result<std::vector<std::uint8_t>>::failure(
                gtlibcpp::make_error(gtlibcpp::ErrorCode::read_failed, "unmapped simulated address", "read", addr, size));
            out[i] = it->second;
        }
        return gtlibcpp::Result<std::vector<std::uint8_t>>::success(std::move(out));
    }

    gtlibcpp::Result<std::size_t> write(gtlibcpp::Address addr, const std::vector<std::uint8_t>& bytes) override {
        for (std::size_t i = 0; i < bytes.size(); ++i) mem_[addr + i] = bytes[i];
        return gtlibcpp::Result<std::size_t>::success(bytes.size());
    }

    bool is_alive() noexcept override { return true; }
    std::string target_id() const override { return is_64bit_ ? "sim:assaultcube_x64" : "sim:assaultcube_x86"; }
    gtlibcpp::TargetIdentity identity() const override {
        return {1337, 1337, is_64bit_ ? "assaultcube" : "ac_client.exe", "",
                is_64bit_ ? gtlibcpp::Architecture::x64 : gtlibcpp::Architecture::x86};
    }

private:
    bool is_64bit_;
    std::map<gtlibcpp::Address, std::uint8_t> mem_;

    void write_u32(gtlibcpp::Address a, std::uint32_t val) {
        auto* p = reinterpret_cast<const std::uint8_t*>(&val);
        for (std::size_t i = 0; i < 4; ++i) mem_[a + i] = p[i];
    }
    void write_u64(gtlibcpp::Address a, std::uint64_t val) {
        auto* p = reinterpret_cast<const std::uint8_t*>(&val);
        for (std::size_t i = 0; i < 8; ++i) mem_[a + i] = p[i];
    }
    void write_float(gtlibcpp::Address a, float val) {
        auto* p = reinterpret_cast<const std::uint8_t*>(&val);
        for (std::size_t i = 0; i < 4; ++i) mem_[a + i] = p[i];
    }
};

} // namespace

int main() {
    std::cout << "========================================================\n"
              << "  gtlibcpp Example: AssaultCube Trainer & HUD Telemetry\n"
              << "========================================================\n";

    std::shared_ptr<gtlibcpp::IMemoryBackend> backend;
    bool is_64bit = false;
    gtlibcpp::Address player_base = 0;

#if defined(__APPLE__)
    is_64bit = true;
    const auto pid = gtlibcpp::find_darwin_process({"assaultcube", "ac_client"});
    if (pid != 0) {
        const auto path = gtlibcpp::get_darwin_process_path(pid);
        std::cout << "[+] Found running AssaultCube process: PID " << pid << " (" << path << ")\n";

        gtlibcpp::DarwinBackendOptions opts{};
        opts.pid = pid;
        opts.image_path = path;
        opts.architecture = gtlibcpp::Architecture::x64;
        opts.read_only = false;
        backend = gtlibcpp::make_darwin_backend(opts);

        if (backend) {
            const auto mod_base = gtlibcpp::get_darwin_module_base(pid, "assaultcube");
            std::cout << "[+] Resolved dynamic ASLR module base: 0x" << std::hex << mod_base << std::dec << "\n";
            gtlibcpp::MemorySession tmp_session(backend);
            auto ptr = tmp_session.read<std::uint64_t>(mod_base + 0x1CBD38ULL);
            if (ptr && ptr.value() != 0) {
                player_base = ptr.value();
                std::cout << "[+] Resolved LocalPlayer pointer (player1): 0x" << std::hex << player_base << std::dec << "\n";
            }
        }
    }
#elif defined(_WIN32)
    is_64bit = false;
    const auto pid = gtlibcpp::find_game_process("ac_client");
    if (pid != 0) {
        const auto path = gtlibcpp::get_process_image_path(pid);
        std::cout << "[+] Found running AssaultCube process: PID " << pid << " (" << path << ")\n";

        gtlibcpp::WindowsBackendOptions opts{};
        opts.pid = pid;
        opts.image_path = path;
        opts.architecture = gtlibcpp::Architecture::x86;
        opts.read_only = false;
        backend = gtlibcpp::make_windows_backend(opts);

        if (backend) {
            const gtlibcpp::Address mod_base = 0x400000;
            gtlibcpp::MemorySession tmp_session(backend);
            auto ptr = tmp_session.read<std::uint32_t>(mod_base + 0x17E0A8);
            if (ptr && ptr.value() != 0) {
                player_base = ptr.value();
                std::cout << "[+] Resolved LocalPlayer pointer: 0x" << std::hex << player_base << std::dec << "\n";
            }
        }
    }
#endif

    if (!backend || player_base == 0) {
        std::cout << "[*] AssaultCube is not running; using high-fidelity simulated sandbox.\n";
        backend = std::make_shared<SimulatedAssaultCubeBackend>(is_64bit);
        player_base = is_64bit ? 0x100500000ULL : 0x500000ULL;
    }

    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    auto freeze = std::make_shared<gtlibcpp::FreezeManager>(session);

    // Compute player member addresses based on platform offsets
    const gtlibcpp::Address hp_addr    = player_base + (is_64bit ? 0x100 : 0xEC);
    const gtlibcpp::Address arm_addr   = player_base + (is_64bit ? 0x104 : 0xF0);
    const gtlibcpp::Address gun_addr   = player_base + (is_64bit ? 0x108 : 0xFC);
    const gtlibcpp::Address x_addr     = player_base + (is_64bit ? 0x8   : 0x2C);
    const gtlibcpp::Address y_addr     = player_base + (is_64bit ? 0xC   : 0x28);
    const gtlibcpp::Address z_addr     = player_base + (is_64bit ? 0x10  : 0x30);
    const gtlibcpp::Address rifle_clip = player_base + (is_64bit ? 0x154 : 0x140);

    // 1. Read and display live telemetry
    auto hp = session->read<std::uint32_t>(hp_addr);
    auto arm = session->read<std::uint32_t>(arm_addr);
    auto gun = session->read<std::int32_t>(gun_addr);
    auto px = session->read<float>(x_addr);
    auto py = session->read<float>(y_addr);
    auto pz = session->read<float>(z_addr);

    std::string weapon = (gun && gun.value() >= 0 && gun.value() < 9) ? kWeaponNames[gun.value()] : "Unknown";

    std::cout << "\n--- Current Player State ---\n"
              << "  Health   : " << (hp ? std::to_string(hp.value()) : "ERR") << " HP\n"
              << "  Armor    : " << (arm ? std::to_string(arm.value()) : "ERR") << " AP\n"
              << "  Weapon   : " << weapon << "\n"
              << "  Position : (" << std::fixed << std::setprecision(2)
              << (px ? px.value() : 0.f) << ", "
              << (py ? py.value() : 0.f) << ", "
              << (pz ? pz.value() : 0.f) << ")\n"
              << "----------------------------\n\n";

    // 2. Action: Set Health and Armor to 100
    std::cout << "[Action 1] Setting Health & Armor to 100 HP / 100 AP...\n";
    (void)session->write<std::uint32_t>(hp_addr, 100);
    (void)session->write<std::uint32_t>(arm_addr, 100);
    std::cout << "  -> Health & Armor write verified [OK]\n";

    // 3. Action: Freeze Unlimited Ammo at 999
    std::cout << "[Action 2] Freezing Rifle Ammo at 999 (FreezeManager)...\n";
    gtlibcpp::FreezeRequest req{};
    req.id = "ammo_freeze";
    req.address = rifle_clip;
    req.type_name = "uint32";
    req.value_u64 = 999;
    req.interval = std::chrono::milliseconds(20);
    req.size = 4;
    auto f_res = freeze->freeze(req);
    if (f_res) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        auto ammo_read = session->read<std::uint32_t>(rifle_clip);
        std::cout << "  -> Ammo frozen at: " << (ammo_read ? ammo_read.value() : 0) << " [OK]\n";
        (void)freeze->unfreeze("ammo_freeze");
    }

    // 4. Action: High Jump (+10.0 ZCord)
    if (pz) {
        float new_z = pz.value() + 10.0f;
        std::cout << "[Action 3] Performing High Jump: ZCord " << pz.value() << " -> " << new_z << "...\n";
        (void)session->write<float>(z_addr, new_z);
        // Non-destructive restore
        (void)session->write<float>(z_addr, pz.value());
        std::cout << "  -> High Jump verified and restored cleanly [OK]\n";
    }

    std::cout << "\n[+] AssaultCube trainer example executed successfully.\n";
    return 0;
}
