// ============================================================================
// GTLibCpp Live Memory Freeze Trainer
// Production C++ Trainer utilizing GTLibCpp MemorySession and FreezeManager.
// Safe, capability-scoped, transactional memory operations.
// ============================================================================

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <atomic>
#include <csignal>
#include <memory>
#include <cstdint>

#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/freeze.hpp"
#include "simulated_game.hpp"

#ifdef _WIN32
#include "gtlibcpp/windows_backend.hpp"
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {
std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running.store(false);
}

#ifdef _WIN32
std::uint32_t find_process_by_name(const std::string& process_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, process_name.c_str()) == 0) {
                CloseHandle(snapshot);
                return entry.th32ProcessID;
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return 0;
}
#endif

void print_banner() {
    std::cout << "========================================================\n";
    std::cout << "       GTLibCpp Production Live Memory Trainer          \n";
    std::cout << "   Powered by MemorySession & Multi-Thread FreezeManager \n";
    std::cout << "========================================================\n\n";
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --pid=<pid>            Target process ID (auto-discovers if omitted)\n"
              << "  --process=<name>       Process name to search (default: igi.exe)\n"
              << "  --address=<hex_addr>   Memory address to freeze (e.g. 0x005be3c4)\n"
              << "  --value=<int_val>      Desired freeze value (default: 99)\n"
              << "  --type=<type>          Value type: uint32, int32, float (default: uint32)\n"
              << "  --interval=<ms>        Freeze loop interval in ms (default: 20ms)\n"
              << "  --help                 Display this help message\n";
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    print_banner();

    std::uint32_t target_pid = 0;
    std::string process_name = "igi.exe";
    gtlibcpp::Address target_address = 0;
    std::uint64_t desired_value = 99;
    std::string value_type = "uint32";
    std::chrono::milliseconds interval(20);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg.rfind("--pid=", 0) == 0) {
            target_pid = static_cast<std::uint32_t>(std::stoul(arg.substr(6)));
        } else if (arg.rfind("--process=", 0) == 0) {
            process_name = arg.substr(10);
        } else if (arg.rfind("--address=", 0) == 0) {
            target_address = static_cast<gtlibcpp::Address>(std::stoull(arg.substr(10), nullptr, 16));
        } else if (arg.rfind("--value=", 0) == 0) {
            desired_value = std::stoull(arg.substr(8));
        } else if (arg.rfind("--type=", 0) == 0) {
            value_type = arg.substr(7);
        } else if (arg.rfind("--interval=", 0) == 0) {
            interval = std::chrono::milliseconds(std::stoul(arg.substr(11)));
        }
    }

    gtlibcpp::MemoryBackendPtr backend = nullptr;

#ifdef _WIN32
    if (target_pid == 0) {
        std::cout << "[*] Searching for running process '" << process_name << "'...\n";
        target_pid = find_process_by_name(process_name);
        if (target_pid == 0 && process_name == "igi.exe") {
            target_pid = find_process_by_name("Project-IGI.exe");
        }
    }

    if (target_pid != 0) {
        std::cout << "[+] Found target process PID: " << target_pid << "\n";
        gtlibcpp::WindowsBackendOptions opts{};
        opts.pid = target_pid;
        opts.read_only = false;
        backend = gtlibcpp::make_windows_backend(opts);
    } else {
        std::cout << "[-] Process '" << process_name << "' not found. Running against simulated game backend.\n";
        backend = std::make_shared<gtlibcpp::demo::SimulatedGameProcess>();
    }
#else
    std::cout << "[*] POSIX Environment: Running against GTLibCpp Simulated Game Process\n";
    auto sim = std::make_shared<gtlibcpp::demo::SimulatedGameProcess>();
    backend = sim;
    target_address = gtlibcpp::demo::SimulatedGameProcess::kStatsObjAddr + gtlibcpp::demo::SimulatedGameProcess::kAmmoOffset;
#endif

    if (!backend) {
        std::cerr << "[-] Error: Failed to initialize memory backend for PID " << target_pid << "\n";
        return 1;
    }

    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    const auto& id = session->identity();
    std::cout << "[+] Attached to process: PID " << id.pid << " (" << id.image_path << ")\n";
    std::cout << "[+] Target Architecture: " << (id.architecture == gtlibcpp::Architecture::x64 ? "x64" : "x86") << "\n";

    if (!session->is_alive()) {
        std::cerr << "[-] Error: Target process is not responding or dead.\n";
        return 1;
    }

    // Auto-resolve demo address if none supplied
    if (target_address == 0) {
        target_address = 0x005be3c4; // Default base address candidate
        std::cout << "[*] No address supplied. Using 0x" << std::hex << target_address << std::dec << "\n";
    }

    // Read initial value before freeze
    std::cout << "[*] Probing target memory at 0x" << std::hex << target_address << std::dec << "...\n";
    auto read_probe = session->read<std::uint32_t>(target_address);
    if (read_probe.ok()) {
        std::cout << "[+] Current Memory Value at 0x" << std::hex << target_address << std::dec 
                  << ": " << read_probe.value() << "\n";
    } else {
        std::cout << "[!] Notice: Read probe returned: " << read_probe.error().message 
                  << " (Code: " << static_cast<int>(read_probe.error().code) << ")\n";
    }

    // Initialize GTLibCpp FreezeManager
    auto freeze_mgr = std::make_shared<gtlibcpp::FreezeManager>(session);

    gtlibcpp::FreezeRequest req{};
    req.id = "gtlibcpp_live_freeze";
    req.address = target_address;
    req.type_name = value_type;
    req.value_u64 = desired_value;
    req.interval = interval;

    std::cout << "[*] Enrolling freeze request in FreezeManager...\n";
    auto freeze_res = freeze_mgr->freeze(req);
    if (!freeze_res.ok()) {
        std::cerr << "[-] FreezeManager failed to start: " << freeze_res.error().message << "\n";
        return 1;
    }

    std::cout << "\n========================================================\n";
    std::cout << "[+] GTLIBCPP TRAINER ACTIVE\n";
    std::cout << "    Target Address: 0x" << std::hex << target_address << std::dec << "\n";
    std::cout << "    Enforced Value: " << desired_value << " (" << value_type << ")\n";
    std::cout << "    Lock Interval:  " << interval.count() << " ms\n";
    std::cout << "    Press Ctrl+C to stop trainer and restore original memory.\n";
    std::cout << "========================================================\n\n";

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto status = freeze_mgr->status("gtlibcpp_live_freeze");
        if (status.ok()) {
            std::cout << "\r[*] [GTLibCpp] Active: " << (status.value().active ? "YES" : "NO")
                      << " | Rewrites: " << status.value().successful_rewrites
                      << " | Fails: " << status.value().failed_rewrites
                      << " | Captured Original: " << status.value().original_value_u64
                      << std::flush;
            if (!status.value().active) {
                std::cout << "\n[!] Target process closed or inaccessible.\n";
                break;
            }
        }
    }

    std::cout << "\n\n[*] Stopping GTLibCpp FreezeManager worker...\n";
    auto restore_res = freeze_mgr->restore("gtlibcpp_live_freeze");
    if (restore_res.ok()) {
        std::cout << "[+] Original memory successfully restored.\n";
    }

    (void)freeze_mgr->unfreeze("gtlibcpp_live_freeze");
    freeze_mgr->cancel_all();

    std::cout << "[+] GTLibCpp Trainer cleanly deactivated. Exiting.\n";
    return 0;
}
