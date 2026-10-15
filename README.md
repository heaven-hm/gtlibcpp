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
| `gt_trainer_cli` | Interactive CLI trainer with cheat table parsing, hotkey detection & memory freezing |

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

## CLI Trainer (`gt_trainer_cli`) & Examples

A complete, table-driven command-line game trainer utility leveraging [`CheatTableParser`](include/gtlibcpp/parser.hpp), [`MemorySession`](include/gtlibcpp/memory_session.hpp), and [`FreezeManager`](include/gtlibcpp/freeze.hpp):

### Running the Interactive Trainer

```bash
# On macOS (AssaultCube 64-bit with Mach task port attachment):
./build/gt_trainer_cli --table demos/CheatTable/assaultcube_mac.ct

# On Windows (AssaultCube 32-bit via Win32 backend):
./build/gt_trainer_cli --table demos/CheatTable/assaultcube.ct

# Run automated hotkey detection and non-destructive memory verification test:
./build/gt_trainer_cli --test

# List discovered cheat entries and hotkey bindings:
./build/gt_trainer_cli --list
```

### Live Interactive Hotkey Controls

| Hotkey | Action | Description |
| :--- | :--- | :--- |
| **`[H]`** / `Ctrl+H` | **Set Health & Armor** | Restores Health to 100 HP and Armor to 100 AP |
| **`[A]`** / `Ctrl+A` | **Unlimited Ammo** | Freezes active clip and maintains 999 ammo across all 9 weapon slots |
| **`[Z]`** / `Ctrl+Z` | **High Jump** | Increases player elevation by +10.0 units (`ZCord`) |
| **`[S]`** | **Display Status** | Prints full player telemetry (Health, Armor, Active Weapon, Ammo, 3D Pos) |
| **`[Q]`** | **Quit Trainer** | Cancels all memory freezes and exits cleanly |

### Real-Time Live HUD Telemetry Output

```text
========================================================
  gtlibcpp CLI Trainer & Hotkey Controller v1.0.0
========================================================
[+] Loaded Cheat Table: demos/CheatTable/assaultcube_mac.ct (CE Version: 0)
[+] Discovered Entries: 14
[+] Detected running game process: PID 66115 (/Applications/AssaultCube.app/.../assaultcube)
[+] Successfully attached to live macOS target process via Mach task port!
[+] Discovered live ASLR module base for assaultcube: 0x1024e1000
[+] Resolved Target Addresses:
    Player Base    : 0x7ff044014600
    Health Address : 0x7ff044014700
    Armor Address  : 0x7ff044014704
    Ammo Address   : 0x7ff044014754
    ZCord Address  : 0x7ff044014610

--- Current Memory State ---
  Health   : 100 HP
  Armor    : 50 AP
  Weapon   : Assault Rifle [Clip: 20 | Reserve: 60] [NORMAL]
  Position : (59.50, 47.17, 4.50)
----------------------------

[Loop Active] HP: 100 | Armor: 50 | Assault Rifle: 20/60 [NORMAL] | Pos: (59.5, 47.2, 4.5) | ([H], [A], [Z], [Q] to quit)
```

### Standalone C++ Trainer Example

The repository includes a self-contained, cross-platform trainer example in [`examples/assaultcube_trainer.cpp`](examples/assaultcube_trainer.cpp):

```cpp
#include "gtlibcpp/darwin_backend.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/freeze.hpp"

// 1. Discover game PID and live ASLR module base
const auto pid = gtlibcpp::find_darwin_process({"assaultcube"});
const auto base = gtlibcpp::get_darwin_module_base(pid, "assaultcube");

// 2. Attach to target process
gtlibcpp::DarwinBackendOptions opts{};
opts.pid = pid;
opts.architecture = gtlibcpp::Architecture::x64;
auto backend = gtlibcpp::make_darwin_backend(opts);
gtlibcpp::MemorySession session(backend);

// 3. Resolve local player pointer (base + 0x1CBD38)
auto player1 = session.read<std::uint64_t>(base + 0x1CBD38ULL).value();

// 4. Restore Health & Armor (100 HP / 100 AP)
session.write<std::uint32_t>(player1 + 0x100, 100);
session.write<std::uint32_t>(player1 + 0x104, 100);

// 5. Freeze Rifle Ammo at 999 using FreezeManager
gtlibcpp::FreezeManager freeze(std::make_shared<gtlibcpp::MemorySession>(backend));
gtlibcpp::FreezeRequest req{"ammo", player1 + 0x154, "uint32", 999, std::chrono::milliseconds(20), 4};
freeze.freeze(req);
```

Build and run the standalone example directly:
```bash
cmake --build build --target assaultcube_trainer_example
./build/assaultcube_trainer_example
```


## Screenshots

This library has no graphical user interface. Authentic CLI/CI demonstration captures can be added using [the capture checklist](docs/SCREENSHOTS.md); no simulated runtime output is shown.

The source is provided under the [Heaven-HM Community Modding License](LICENSE), **not an unrestricted open-source license**. See [license scope](LICENSE_SCOPE.md).
