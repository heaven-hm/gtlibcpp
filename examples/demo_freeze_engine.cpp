#include <iostream>
#include <chrono>
#include <thread>
#include <cassert>
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "simulated_game.hpp"

int main() {
    std::cout << "========================================================\n";
    std::cout << "  E2E DEMO 2: Real-time Memory Freeze & Restore Engine  \n";
    std::cout << "========================================================\n\n";

    auto game = std::make_shared<gtlibcpp::demo::SimulatedGameProcess>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(game);
    auto freeze_mgr = std::make_shared<gtlibcpp::FreezeManager>(session);

    const gtlibcpp::Address ammo_addr = gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kAmmoOffset;

    // 1. Initial Ammo Check
    auto ammo_before = session->read<std::uint32_t>(ammo_addr);
    assert(ammo_before && ammo_before.value() == 30);
    std::cout << "[+] Initial Ammo in game: " << ammo_before.value() << " rounds\n";

    // 2. Lock & Freeze Ammo at 99 rounds
    gtlibcpp::FreezeRequest req{};
    req.id = "infinite_ammo";
    req.address = ammo_addr;
    req.type_name = "uint32";
    req.value_u64 = 99;
    req.interval = std::chrono::milliseconds(10); // 10ms interval

    auto freeze_res = freeze_mgr->freeze(req);
    assert(freeze_res.ok());
    std::cout << "[+] Background Freeze Worker started for 'infinite_ammo' at 10ms interval\n";

    // Wait a brief moment for worker to engage
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    // Verify ammo is now frozen at 99
    auto ammo_frozen = session->read<std::uint32_t>(ammo_addr);
    assert(ammo_frozen && ammo_frozen.value() == 99);
    std::cout << "[+] Verified Ammo locked at: " << ammo_frozen.value() << "\n";

    // 3. Simulate Weapon Firing (External game decreases ammo to 10)
    std::cout << "[*] Simulating high-rate automatic weapon fire in game (decrementing to 10)...\n";
    assert(session->write<std::uint32_t>(ammo_addr, 10).ok());

    // Wait 30ms (worker will detect and overwrite back to 99)
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    auto ammo_relocked = session->read<std::uint32_t>(ammo_addr);
    assert(ammo_relocked && ammo_relocked.value() == 99);
    std::cout << "[+] Freeze Worker successfully intercepted change and restored locked 99 rounds!\n";

    // 4. Query Freeze Metrics
    auto status = freeze_mgr->status("infinite_ammo");
    assert(status && status.value().active);
    std::cout << "[+] Worker Status: Active=" << std::boolalpha << status.value().active
              << ", Total Lock Rewrites=" << status.value().successful_rewrites 
              << ", Captured Original Value=" << status.value().original_value_u64 << "\n";

    // 5. Restore Original Value and Unfreeze
    std::cout << "[*] Restoring original value and stopping worker...\n";
    auto restore_res = freeze_mgr->restore("infinite_ammo");
    assert(restore_res.ok());

    auto unfreeze_res = freeze_mgr->unfreeze("infinite_ammo");
    assert(unfreeze_res.ok());

    auto ammo_restored = session->read<std::uint32_t>(ammo_addr);
    assert(ammo_restored && ammo_restored.value() == 30);
    std::cout << "[+] Post-Unfreeze Ammo automatically restored to pre-freeze original: " 
              << ammo_restored.value() << " rounds!\n";

    // 6. Test Process Exit Cancellation
    req.id = "die-fast-test";
    req.address = 0xA000;
    req.interval = std::chrono::milliseconds(20);
    assert(freeze_mgr->freeze(req).ok());
    std::cout << "[*] Simulating unexpected game process crash/termination...\n";
    game->terminate_process();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto crash_status = freeze_mgr->status("die-fast-test");
    assert(crash_status && !crash_status.value().active);
    std::cout << "[+] Freeze worker detected dead target and cleanly shut down without leaking threads!\n";

    freeze_mgr->cancel_all();
    std::cout << "\n[SUCCESS] E2E Freeze Engine Demo completed flawlessly!\n\n";
    return 0;
}
