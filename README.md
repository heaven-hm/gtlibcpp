# GTLibCpp

C++17 memory operations and a local JSON-RPC agent service for **authorized offline** game trainers and test processes. The legacy `GTLibc.*` Win32 trainer surface is preserved under `libs/gtlibc/` for backwards compatibility; `gtlibcpp::` in `include/gtlibcpp/` and `src/` is the supported API.

[Windows CI](https://github.com/heaven-hm/gtlibcpp/actions) · [API](docs/API.md) · [Examples](docs/EXAMPLES.md) · [Security](SECURITY.md) · [License](LICENSE)

## Capabilities

| Component | Purpose |
| --- | --- |
| `MemorySession` | Typed reads, writes and pointer-chain resolution |
| `FreezeManager` | Cancellable value freezing and restoration |
| `Policy` | Capability, identity, approval and audit checks for agent use |
| `CheatTableParser` | Strict, limited Cheat Engine `.CT` parsing |
| `AgentService` | Local JSON-RPC 2.0 transport |
| `gtctl` | Read-only CLI version and health checks |

The cross-platform core builds on major desktop platforms; the real process backend and named-pipe service are Windows-specific. No general-purpose memory value scanner is exposed by the core API.

## Build and test

```bash
cmake -S . -B build -DGTLIBCPP_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

## Minimal C++ read example (Windows)

```cpp
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/windows_backend.hpp"
#include <cstdint>
#include <iostream>

int main() {
    gtlibcpp::WindowsBackendOptions options{};
    options.pid = 1234; // Your authorized offline test process
    options.image_path = "C:/test/game.exe";
    options.architecture = gtlibcpp::Architecture::x64;
    options.read_only = true;

    auto backend = gtlibcpp::make_windows_backend(options);
    if (!backend) return 1;

    gtlibcpp::MemorySession session(backend);
    auto result = session.read<std::uint32_t>(0x12345678); // Example address
    if (!result) return 2;
    std::cout << result.value() << '\n';
}
```

For scanning and rescanning examples, see [game-memory guide](docs/GAME_MEMORY_GUIDE.md). For write/restore, freeze, agent authorization and CMake consumers, see [examples](docs/EXAMPLES.md).

## Screenshots

This library has no graphical user interface. Authentic CLI/CI demonstration captures can be added using [the capture checklist](docs/SCREENSHOTS.md); no simulated runtime output is shown.

The source is provided under the [Heaven-HM Community Modding License](LICENSE), **not an unrestricted open-source license**. See [license scope](LICENSE_SCOPE.md).
