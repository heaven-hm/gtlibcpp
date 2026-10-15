# Windows game-process discovery, memory examples, and AoB search

The modern GTLibCpp core provides 64-bit `Address` values for both **x86**
and **x64** target processes. Windows game/window discovery lives in
`gtlibcpp/windows_discovery.hpp`, while typed reads and writes use
`gtlibcpp::MemorySession`.

## Build the examples

```powershell
cmake -S . -B build -DGTLIBCPP_BUILD_TESTS=ON
cmake --build build --config Release
cmake --install build --config Release --prefix installed
cmake -S examples -B build-examples -DCMAKE_PREFIX_PATH="$PWD/installed"
cmake --build build-examples --config Release
```

## FindGameProcess and FindGameWindow equivalents

The legacy class had `GTLibc::FindGameProcess`,
`GTLibc::FindGameWindow`, and `GetProcessID4mHWND`. The new free
functions are `gtlibcpp::find_game_process("igi.exe")`,
`gtlibcpp::find_game_window("Window title")`, and
`gtlibcpp::get_process_id_from_window(hwnd)`. They return a PID/window
and do not request unrestricted access.

```cpp
#include "gtlibcpp/windows_discovery.hpp"
auto pid = gtlibcpp::find_game_process("igi");  // .exe optional
HWND hwnd = gtlibcpp::find_game_window("Game window title");
auto window_pid = gtlibcpp::get_process_id_from_window(hwnd);
auto executable_path = gtlibcpp::get_process_image_path(pid);
```

Find windows by **exact title**. The process lookup chooses the first exact
executable-name match; for games running multiple instances use a known PID.
Neither lookup identifies a game version or proves authorization.

## End-to-end examples

```powershell
# Both 32-bit and 64-bit targets are supported on Windows, subject to OS access.
.\build-examples\Release\discover_and_read.exe process igi.exe x86 00401000
.\build-examples\Release\discover_and_read.exe window "Window title" x64 0000000140001000

# Explicitly authorized offline test processes only:
.\build-examples\Release\read_write_restore.exe --apply test-game.exe x64 0000000140001000 999
```

The read/write example reads the original uint32, writes the requested
value, verifies by readback, and attempts restoration. These commands use
**illustrative addresses**, not confirmed game addresses. Direct
`MemorySession` calls do not perform `Policy::authorize` automatically;
agent integrations must explicitly enforce approval, identity, capabilities,
and auditing before granting write access.

## AoB / wildcard pattern search

`gtlibcpp/aob_scan.hpp` contains a **read-only, bounded** AoB matcher
that scans a **caller-supplied, already readable address range**. It is not
a complete process-wide scan engine and does not change memory protection.

```cpp
#include "gtlibcpp/aob_scan.hpp"
// session is a read-only MemorySession attached to an authorized target.
// Use a verified readable region, not arbitrary process addresses.
auto hits = gtlibcpp::scan_aob(session, region_base, region_size,
                              "48 8B ?? 05", 32);
if (!hits) {
    // A failed or inaccessible page returns a Result error.
} else {
    for (auto address : hits.value()) {
        // Inspect each candidate and confirm it matches the target build.
    }
}
```

Patterns use two hex digits per byte, `?` or `??` for wildcards.
The matcher has a 16 MiB range cap, 4096-byte signature cap, and
a configurable maximum hit count.

## Logging and errors

`MemorySession` returns structured `Result<T>` errors; check
`result.error().message`, `operation`, `address`, and error code
instead of treating failed reads as zero. The current `Logger` supports an
injected sink, severity threshold, and structured diagnostic fields.
See [API.md](API.md) and [POLICY.md](POLICY.md).
Do not log secrets, approval tokens or full memory buffers by default.

## Shellcode injection

Legacy Win32 trainer code is not a supported, unrestricted remote
shellcode-injection API in the modern core. This PR does **not** introduce
remote code allocation, cross-process thread creation, or arbitrary payload
execution. A future opt-in patch/debug facility would require a separate
threat model, identity and consent checks, rollback tests, and review for
both x86/x64 targets. For memory edits today, use approved typed writes
on data pages and confirm the expected preimage before mutation.

## Cheat Engine design references (independent implementation)

Cheat Engine's [memory-scan API](https://github.com/cheat-engine/cheat-engine/blob/master/Cheat%20Engine/LuaMemscan.pas) separates a first scan from subsequent candidate filtering and supports variable types, readable-region/protection filtering, scan bounds and alignment. GTLibCpp's new AoB helper adopts **only these general design principles**: explicit scan ranges, wildcard bytes, bounded reads, deterministic match limits, and error propagation. It is not an implementation of Cheat Engine's complete scanner (no unknown-initial or changed-value state machine yet).

**Licensing:** The upstream Cheat Engine repository does not currently expose an unambiguous repository-wide redistribution license in its root. No Cheat Engine source was copied or bundled here. This implementation is original C++17; any future reuse of Cheat Engine code requires verifying the license and provenance first.

Potential follow-ups: region-aware scanning with guard/protection filters, configurable alignment, multi-pass changed/unchanged/increased/decreased scanning, and module-relative signatures with cross-restart testing.
