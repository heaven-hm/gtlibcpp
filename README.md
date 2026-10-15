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
| License | Heaven-HM Community Modding License (see [LICENSE](LICENSE)) |

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

## Find a game process, scan memory, read and write (Windows)

These examples are for **authorized offline games and your own test programs**.
The supported `gtlibcpp::MemorySession` API can read/write known addresses
and resolve pointer chains. **GTLibCpp does not currently expose a general
value-scanning API**; the scanner example below uses Windows APIs to discover
candidate addresses, then uses GTLibCpp to read them.

### 1. Locate the running game

Open PowerShell while the offline game or test program is running:

```powershell
Get-Process | Where-Object ProcessName -Like '*YourGame*' |
    Select-Object Id, ProcessName, Path
```

Record the PID, full executable path and architecture (x86 vs x64).
Addresses below are examples only. They are **not** known offsets for any game.

### 2. Attach and read a typed value

```cpp
#include "gtlibcpp/windows_backend.hpp"
#include "gtlibcpp/memory_session.hpp"
#include <cstdint>
#include <iostream>

int main() {
    gtlibcpp::WindowsBackendOptions opts{};
    opts.pid = 1234; // Your actual game/test-process PID
    opts.image_path = R"(C:\Games\YourGame\game.exe)";
    opts.architecture = gtlibcpp::Architecture::x64; // Use x86 for 32-bit games
    opts.read_only = true; // Least privilege

    auto backend = gtlibcpp::make_windows_backend(opts);
    if (!backend) {
        std::cerr << "Cannot attach: check PID and OS permissions\n";
        return 1;
    }
    gtlibcpp::MemorySession session(backend);

    constexpr gtlibcpp::Address address = 0x12345678; // Replace after discovery
    auto ammo = session.read<std::uint32_t>(address);
    if (!ammo) {
        std::cerr << "Read failed: " << ammo.error().message << '\n';
        return 2;
    }
    std::cout << "Value: " << ammo.value() << '\n';
}
```

A successful memory read does **not** prove the address contains ammunition.
Validate candidate addresses against changes in actual game state.

### 3. Scan for ammunition (unknown address)

Suppose the game displays **100** rounds. Scan readable committed pages
for 4-byte values equal to 100, fire three rounds, then recheck only the
matching addresses for **97**. Repeat with further values to reduce false
positives.

This compact Windows-only helper uses `VirtualQueryEx` to enumerate pages,
and `MemorySession::read_bytes` to read them. Call it after creating a
read-only `session` as above. A separate process handle is needed for
`VirtualQueryEx`; close that handle after scanning.

```cpp
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// Supply a process handle opened with PROCESS_QUERY_INFORMATION | PROCESS_VM_READ.
std::vector<gtlibcpp::Address> scan_u32(
    gtlibcpp::MemorySession& session, HANDLE process,
    std::uint32_t target) {
    std::vector<gtlibcpp::Address> matches;
    MEMORY_BASIC_INFORMATION mbi{};
    std::uintptr_t cursor = 0;
    std::size_t budget = 64u * 1024u * 1024u; // Bound the demo scan
    while (budget >= 4 &&
           VirtualQueryEx(process, reinterpret_cast<LPCVOID>(cursor),
                          &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        if (!mbi.RegionSize || mbi.RegionSize > UINTPTR_MAX - base) break;
        const auto end = base + mbi.RegionSize;
        const DWORD p = mbi.Protect;
        const bool readable = mbi.State == MEM_COMMIT &&
            !(p & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                  PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                  PAGE_EXECUTE_WRITECOPY));
        if (readable) {
            for (auto addr = base; addr <= end - std::min<std::size_t>(4, mbi.RegionSize)
                 && budget >= 4;) {
                const auto size = std::min<std::size_t>(
                    {4096, end - addr, budget});
                if (size < 4) break;
                auto bytes = session.read_bytes(addr, size);
                if (bytes && bytes.value().size() == size) {
                    for (std::size_t i = 0; i + 4 <= size; i += 4) {
                        std::uint32_t value;
                        std::memcpy(&value, bytes.value().data() + i, 4);
                        if (value == target) matches.push_back(addr + i);
                    }
                }
                budget -= size;
                addr += size;
            }
        }
        if (end <= cursor) break;
        cursor = end;
    }
    return matches;
}

std::vector<gtlibcpp::Address> rescan_u32(
    gtlibcpp::MemorySession& session,
    const std::vector<gtlibcpp::Address>& candidates,
    std::uint32_t expected) {
    std::vector<gtlibcpp::Address> results;
    for (auto address : candidates) {
        auto value = session.read<std::uint32_t>(address);
        if (value && value.value() == expected)
            results.push_back(address);
    }
    return results;
}

// Usage in main(), after creating session:
// HANDLE query = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
//                            FALSE, opts.pid);
// if (!query) return 3;
// auto candidates = scan_u32(session, query, 100);
// CloseHandle(query);
// // Fire three rounds in-game (100 -> 97), then:
// candidates = rescan_u32(session, candidates, 97);
```

This is a **basic teaching scanner**, not a complete Cheat Engine substitute.
It uses a limited 64 MiB budget, may miss unaligned or cross-chunk values,
and is not designed to handle every memory-protection edge case. Only trust
addresses you independently validate.

### 4. Write, read back, and restore (explicitly authorized targets)

Create a **separate** writable backend only after the target is approved.
Direct `MemorySession::write` does **not** automatically enforce the agent's
`Policy::authorize` checks. Apps exposing these operations to other users
or agents must enforce target identity, approval, limits and auditing.

```cpp
gtlibcpp::WindowsBackendOptions write_opts{};
write_opts.pid = 1234;
write_opts.image_path = R"(C:\Games\YourGame\game.exe)";
write_opts.architecture = gtlibcpp::Architecture::x64;
write_opts.read_only = false; // Only for a target you are authorized to modify

auto writable_backend = gtlibcpp::make_windows_backend(write_opts);
if (!writable_backend) return 1;
gtlibcpp::MemorySession writable(writable_backend);

constexpr gtlibcpp::Address confirmed_ammo = 0x12345678; // Example only
auto before = writable.read<std::uint32_t>(confirmed_ammo);
if (!before) return 2;

// Typed write + readback verification:
auto updated = writable.compare_write_verify<std::uint32_t>(
    confirmed_ammo, 999u);
if (!updated) {
    std::cerr << "Write failed: " << updated.error().message << '\n';
    return 3;
}
auto after = writable.read<std::uint32_t>(confirmed_ammo);
if (!after) return 4;
std::cout << "Before: " << before.value()
          << ", after: " << after.value() << '\n';

// Restore original bytes before exit.
auto restored = writable.compare_write_verify<std::uint32_t>(
    confirmed_ammo, before.value());
if (!restored) return 5;
```

`compare_write_verify` performs a write and readback, **not an atomic CAS**.
It does not prove in-game effects or that addresses survive a restart.

### 5. Dynamic addresses, pointers, freezing and other operations

```cpp
// Pointer-chain offsets need to be discovered and validated for your game.
// This API dereferences each (current + offset) step.
auto address = session.resolve_pointer_chain(module_base, {0x10, 0x20});
if (address) {
    auto health = session.read<float>(address.value());
}

// See the linked documentation for:
// read_bytes(address, length) — bounded raw read
// read_string(address, capacity, require_nul) — bounded string read
// read_offsets(base, offsets, size) — batch read at independent offsets
// write_offsets(base, offsets, value) — batch writes, not pointer traversal
// FreezeManager — periodically write and later restore original value
```

See [API reference](docs/API.md), [freeze and restore examples](docs/EXAMPLES.md),
[agent authorization](docs/POLICY.md) and [migration guidance](docs/MIGRATION.md).
On Linux and macOS the portable core builds, but this particular process
discovery and scanning example requires Windows.

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

Heaven-HM Community Modding License — see [LICENSE](LICENSE).

## License and responsible use

This project's Heaven-HM-owned material uses the
[Heaven-HM Community Modding License](LICENSE). You may modify the
source and create and share free, lawful, noncommercial mods. Selling the
covered software or resulting mods, or charging for access, requires separate
written permission. Illegal activity, infringement, malware, unauthorized
access, and intentional real-world harm are prohibited. Preserve attribution
and follow third-party licenses. Fictional events in games are not real-world harm.

This is a **source-available** project under a custom restrictive license.
[Prior permissions and separately licensed material](LICENSE_SCOPE.md) remain
valid; this change does not revoke rights granted for earlier versions.
