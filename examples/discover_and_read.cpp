#include "gtlibcpp/windows_discovery.hpp"
#include "gtlibcpp/windows_backend.hpp"
#include "gtlibcpp/memory_session.hpp"
#include <iostream>
#include <string>
#include <cstdint>

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: discover_and_read process|window NAME x86|x64 ADDRESS_HEX\n";
        return 2;
    }
    const std::string method = argv[1], name = argv[2], arch = argv[3];
    if ((method != "process" && method != "window") || (arch != "x86" && arch != "x64")) return 2;
    std::uint64_t address = 0;
    try {
        std::size_t consumed = 0;
        address = std::stoull(argv[4], &consumed, 16);
        if (!address || consumed != std::string(argv[4]).size()) return 2;
    } catch (...) { return 2; }
    const auto pid = method == "process" ? gtlibcpp::find_game_process(name)
        : gtlibcpp::get_process_id_from_window(gtlibcpp::find_game_window(name));
    if (!pid) { std::cerr << "No matching game process/window\n"; return 1; }
    auto path = gtlibcpp::get_process_image_path(pid);
    if (path.empty()) { std::cerr << "Could not identify executable path\n"; return 1; }
    gtlibcpp::WindowsBackendOptions options{};
    options.pid = pid;
    options.image_path = path;
    options.architecture = arch == "x86" ? gtlibcpp::Architecture::x86 : gtlibcpp::Architecture::x64;
    options.read_only = true;
    auto backend = gtlibcpp::make_windows_backend(options);
    if (!backend) { std::cerr << "OpenProcess failed\n"; return 1; }
    gtlibcpp::MemorySession session(backend);
    auto result = session.read<std::uint32_t>(address);
    if (!result) { std::cerr << "Read failed: " << result.error().message << '\n'; return 1; }
    std::cout << "PID " << pid << " (" << path << ") value: " << result.value() << '\n';
}
