# GTLibCpp

A modern C++17 memory operations library and local agent control plane
for **authorised offline** game trainers. Supports both 32-bit and 64-bit
Windows targets, exposes a typed result/error surface for every public
operation, runs structured logging on every call, and ships with a
JSON-RPC over named pipes interface suitable for an offline agent.

The legacy `GTLibc` Win32 trainer surface is preserved in
`GTLibc.hpp` / `GTLibc.cpp` / `GTLibc.tpp` for backwards compatibility.
The new `gtlibcpp::` core in `include/gtlibcpp/` is the supported path.

| Status      | Item                                             |
|-------------|--------------------------------------------------|
| Language    | C++17                                             |
| Platforms   | Windows x64, Windows x86 (WOW64 supported)         |
| Build       | CMake ≥ 3.16, MSVC 14.30+, clang-cl 18+           |
| Tests       | 42 regressions across 4 executables                |
| CI          | GitHub Actions (MSVC + clang-cl × x86 + x64)       |
| License     | MIT (see [LICENSE](LICENSE))                       |

## What it gives you

* `Result<T>` / `Result<void>` with operation, address, byte count, and
  Win32 error — failed reads are never confused with valid zero values.
* 64-bit `Address` type that transparently carries both 32-bit and 64-bit
  game addresses; the pointer-chain resolver picks the right pointer
  width from the recorded `Architecture`.
* `MemorySession` that owns the process handle via `IMemoryBackend`,
  non-copyable, non-movable, no globals.
* `BoundedString` with explicit capacity, encoding, termination, and
  bounds checks.
* `FreezeManager` with cancellable, joinable workers; original-value
  snapshotting; process-exit cancellation.
* `Policy` with target manifests, capabilities, approval tokens with
  TTL, per-target rate limiter, atomic kill switch, and an injected
  audit sink.
* `CheatTableParser` with an XML-driven versioned CE 7.x subset that
  preserves nested entries, multiple hotkeys, signed/hex values, and
  string metadata, and fails closed on Auto Assembler, LoadLibrary,
  ShellExecute, LuaScript, and raw byte-array entries.
* `AgentService` with a versioned JSON-RPC 2.0 surface over a
  dependency-injected transport (named pipes on Windows production,
  in-process channel in tests).
* Structured logging with an injected sink.
* Try/catch + `ErrorCode::internal` on every public method so a
  throwing backend never escapes into the caller.
* CMake install + export with a `find_package(gtlibcpp 1.0)` consumer
  path; Windows CI matrix on MSVC and clang-cl for x86 and x64.

## Quick start

```cpp
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/windows_backend.hpp"

int main() {
    gtlibcpp::WindowsBackendOptions opts{};
    opts.pid              = 1234;            // authorised target
    opts.image_path       = "C:/path/to/authorised/target.exe";
    opts.architecture     = gtlibcpp::Architecture::x64;

    auto backend  = gtlibcpp::make_windows_backend(opts);
    if (!backend) return 1;                 // OpenProcess failed

    gtlibcpp::MemorySession session(backend);
    auto read = session.read<std::uint32_t>(0x401000);
    if (!read) {
        std::fprintf(stderr, "read failed: %s (op=%s addr=0x%llx)\n",
                     read.error().message.c_str(),
                     read.error().operation.c_str(),
                     static_cast<unsigned long long>(read.error().address));
        return 2;
    }
    std::printf("value: 0x%08x\n", read.value());
    return 0;
}
```

## Build

```
cmake -S . -B build -DGTLIBCPP_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

On a fresh checkout the Windows CI matrix (`.github/workflows/windows-ci.yml`)
exercises x64 MSVC, x64 clang-cl, and x86 MSVC. Local builds work on macOS
and Linux too — the cross-platform core is `gtlibcpp::gtlibcpp_core`; the
Windows-only backend and named-pipe transport are `gtlibcpp::gtlibcpp_win32`.

## Documentation

| File                                            | Purpose                                                |
|-------------------------------------------------|--------------------------------------------------------|
| [CHANGELOG.md](CHANGELOG.md)                     | Release notes and what changed in each version.         |
| [SECURITY.md](SECURITY.md)                       | Authorised-use policy, threat model, vulnerability reporting. |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)     | Layered design, supported architectures, thread-safety. |
| [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md)     | In-scope threats, assumptions, out-of-scope items.     |
| [docs/API.md](docs/API.md)                       | Public API reference for every type, function, and method. |
| [docs/POLICY.md](docs/POLICY.md)                 | Manifest format, capability table, audit event format.  |
| [docs/PARSER.md](docs/PARSER.md)                 | Cheat Engine 7.x subset, fail-closed kinds, examples.   |
| [docs/AGENT.md](docs/AGENT.md)                   | JSON-RPC 2.0 surface, method reference, error codes.    |
| [docs/EXAMPLES.md](docs/EXAMPLES.md)             | End-to-end usage examples (read, freeze, parse, agent).  |
| [docs/MIGRATION.md](docs/MIGRATION.md)           | Migrating from the legacy `GTLibc.*` API to `gtlibcpp::*`. |

## Authorised use

GTLibCpp is for **authorised offline use only**. The library does not, on
its own, gain access to any process; the caller must explicitly open a
target with `OpenProcess` (or the equivalent on another platform) and
bind a `MemorySession` to that handle. The agent service is local-only
by default and never opens a network listener. See
[SECURITY.md](SECURITY.md) for the full threat model and
vulnerability-reporting policy.

## License

MIT — see [LICENSE](LICENSE).
