// Example: open a target process, attach a session, and read a
// 4-byte scalar at a known address. Smallest end-to-end snippet
// a trainer needs once the production baseline is in place.

#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/result.hpp"

#if defined(_WIN32)
#include "gtlibcpp/windows_backend.hpp"
#endif

#include <cstdio>
#include <cstdint>
#include <memory>

int main() {
#if defined(_WIN32)
    gtlibcpp::WindowsBackendOptions opts{};
    opts.pid = 1234;
    opts.image_path = "C:/path/to/authorised/target.exe";
    opts.architecture = gtlibcpp::Architecture::x64;

    auto backend = gtlibcpp::make_windows_backend(opts);
    if (!backend) {
        std::fprintf(stderr, "could not open process\n");
        return 1;
    }
    gtlibcpp::MemorySession session(backend);
    const auto read = session.read<std::uint32_t>(0x401000);
    if (!read) {
        std::fprintf(stderr, "read failed: %s (op=%s addr=0x%llx)\n",
                     read.error().message.c_str(),
                     read.error().operation.c_str(),
                     static_cast<unsigned long long>(read.error().address));
        return 2;
    }
    std::printf("value: 0x%08x\n", read.value());
    return 0;
#else
    std::fprintf(stderr,
        "this example requires the Windows backend; rebuild with -D_WIN32.\n");
    return 0;
#endif
}
