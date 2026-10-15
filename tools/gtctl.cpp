#include <iostream>
#include <string>
#include <string_view>

#ifndef GTLIBCPP_VERSION
#define GTLIBCPP_VERSION "unknown"
#endif

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "Usage: gtctl --version | --health [--json not supported yet]\n";
        return 2;
    }
    const std::string_view command(argv[1]);
    if (command == "--version") {
        std::cout << "gtctl " << GTLIBCPP_VERSION << '\n';
        return 0;
    }
    if (command == "--health") {
        // This is a CLI self-test, NOT a target attachment or privilege check.
        std::cout << "{\"component\":\"gtctl\",\"status\":\"ok\",\"target_attached\":false}\n";
        return 0;
    }
    std::cerr << "Unknown option: " << command << '\n';
    return 2;
}
