#include "gtlibcpp/windows_discovery.hpp"
#include "gtlibcpp/windows_backend.hpp"
#include "gtlibcpp/memory_session.hpp"
#include <cstdint>
#include <iostream>
#include <string>
#include <limits>

int main(int argc, char** argv) {
    if (argc != 6 || std::string(argv[1]) != "--apply") {
        std::cerr << "Usage: read_write_restore --apply EXE_NAME x86|x64 ADDRESS_HEX VALUE_DEC\n";
        return 2;
    }
    const auto pid = gtlibcpp::find_game_process(argv[2]);
    if (!pid) { std::cerr << "Game not found\n"; return 1; }
    std::string arch = argv[3];
    if (arch != "x86" && arch != "x64") return 2;
    std::uint64_t address = 0;
    std::uint32_t desired = 0;
    try {
        std::size_t count = 0;
        auto address_text = std::string(argv[4]);
        address = std::stoull(address_text, &count, 16);
        if (count != address_text.size() || !address) return 2;
        count = 0;
        auto value_text = std::string(argv[5]);
        const auto parsed = std::stoull(value_text, &count, 10);
        if (count != value_text.size() || parsed > std::numeric_limits<std::uint32_t>::max()) return 2;
        desired = static_cast<std::uint32_t>(parsed);
    } catch (...) { return 2; }
    gtlibcpp::WindowsBackendOptions options{};
    options.pid = pid;
    options.image_path = gtlibcpp::get_process_image_path(pid);
    options.architecture = arch == "x86" ? gtlibcpp::Architecture::x86 : gtlibcpp::Architecture::x64;
    options.read_only = false;
    if (options.image_path.empty()) return 1;
    auto backend = gtlibcpp::make_windows_backend(options);
    if (!backend) return 1;
    gtlibcpp::MemorySession session(backend);
    auto previous = session.read<std::uint32_t>(address);
    if (!previous) { std::cerr << previous.error().message << '\n'; return 1; }
    auto written = session.compare_write_verify<std::uint32_t>(address, desired);
    if (!written) {
        std::cerr << "Write/readback failure: " << written.error().message << '\n';
        return 1;
    }
    std::cout << "Updated " << previous.value() << " to " << desired << '\n';
    auto restored = session.compare_write_verify<std::uint32_t>(address, previous.value());
    if (!restored) { std::cerr << "Restore failed\n"; return 3; }
    std::cout << "Original value restored\n";
}
