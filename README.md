# GTLibCpp

A modern C++17 memory operations library and local agent control plane for **authorised offline** game trainers. Supports both 32-bit and 64-bit Windows targets, exposes a typed result/error surface for every public operation, runs structured logging on every call, and ships with a JSON-RPC over named pipes interface suitable for an offline agent.

The legacy `GTLibc` Win32 trainer surface is preserved in `GTLibc.hpp` / `GTLibc.cpp` / `GTLibc.tpp` for backwards compatibility. The new `gtlibcpp::` core in `include/gtlibcpp/` is the recommended, production-hardened path.

| Status      | Specification                                                    |
|-------------|------------------------------------------------------------------|
| **Standard**| C++17 (no C++20 experimental requirements)                       |
| **Platforms**| Windows x64, Windows x86 (WOW64 supported), macOS / Linux (core) |
| **Build**   | CMake ≥ 3.16, MSVC 14.30+, clang-cl 18+, AppleClang, Clang, GCC  |
| **Tests**   | 42 automated regression & integration tests (100% pass)          |
| **CI**      | GitHub Actions (MSVC + clang-cl × x86 + x64)                     |
| **License** | Heaven-HM Community Modding License (see [LICENSE](LICENSE))     |

---

## What It Gives You

* **Type-Safe `Result<T>` & `Result<void>`**: Comprehensive error surface carrying operation names, target addresses, byte counts, and Win32 / POSIX error codes. Failed reads are never disguised as valid zero values.
* **Architecture-Safe Addresses**: 64-bit `Address` type (`std::uint64_t`) safely addressing both 32-bit and 64-bit targets. Pointer-chain resolution automatically adapts dereference width based on the target `Architecture`.
* **RAII Memory Sessions**: `MemorySession` owns the backend target connection, guarantees strict RAII lifetime, forbids unsafe copying and global state, and ensures deterministic cleanup.
* **Safe Strings & Buffers**: `BoundedString` enforces explicit capacity, string encoding, bounds validation, and NUL-termination.
* **Synchronized Freeze Workers**: `FreezeManager` coordinates background freeze loops via cancellable, joinable threads with atomic flags, explicit restoration with `restore()` after unfreeze, and automatic cancellation upon target termination.
* **Manifest & Policy Engine**: `Policy` enforces target manifests, cryptographic process identity checks, operation capabilities, time-to-live (TTL) tokens, burst rate limiting, and an atomic kill switch.
* **Hardened Cheat Engine Parser**: `CheatTableParser` safely parses Cheat Engine 7.x XML subsets while preserving nested entries, multiple hotkeys, and signed/hex types. It fails closed on unsafe constructs (Auto Assembler, ShellExecute, LoadLibrary, raw binary injections).
* **Agent JSON-RPC 2.0 Service**: Local control plane communicating over named pipes (production) or in-process queue pairs (testing and headless automation).
* **Injectable Structured Logging**: Centralized `Logger` supporting custom structured log sinks and granular severity levels.
* **Wine & Hardened Host Support**: Fallback privilege negotiation gracefully connects under Wine and non-elevated user modes where `PROCESS_ALL_ACCESS` is restricted.

---

## Comprehensive Method & API Directory

### 1. Memory Operations & Session (`gtlibcpp::MemorySession`)
Header: [`include/gtlibcpp/memory_session.hpp`](include/gtlibcpp/memory_session.hpp)

| Method Signature | Return Type | Description |
|------------------|-------------|-------------|
| `MemorySession(std::shared_ptr<IMemoryBackend> backend)` | Constructor | Creates a memory session bound to an explicit memory backend. |
| `identity() const noexcept` | `const TargetIdentity&` | Retrieves target metadata (PID, path, start time, architecture, SHA-256). |
| `is_alive() const noexcept` | `bool` | Checks if the session backend is currently attached and responsive. |
| `read<T>(Address address)` | `Result<T>` | Reads a typed primitive value (`uint32_t`, `float`, etc.) from target address. |
| `write<T>(Address address, const T& value)` | `Result<std::size_t>` | Writes a typed value directly to the specified target address. |
| `read_bytes(Address address, std::size_t size)` | `Result<std::vector<uint8_t>>` | Reads an exact byte buffer of arbitrary length from target memory. |
| `write_bytes(Address address, const std::vector<uint8_t>& data)` | `Result<std::size_t>` | Writes a sequence of bytes directly into target process memory. |
| `resolve_pointer_chain(Address base_address, const std::vector<Address>& offsets)` | `Result<Address>` | Traverses multi-level pointer chains according to target architecture. |
| `read_offsets<T>(Address base, const std::vector<Address>& offsets)` | `Result<BatchResult>` | Reads typed values across multiple structured offsets in a single batch. |
| `write_offsets<T>(Address base, const std::vector<Address>& offsets, const T& val)` | `Result<BatchResult>` | Writes typed values across offsets, stopping at the first error and reporting partial completion; earlier writes are not rolled back. |
| `read_string(Address address, std::size_t capacity, bool require_nul = true)` | `Result<BoundedString>` | Reads a bounded string safely validating UTF-8 and capacity. |
| `write_string(Address address, const BoundedString& str, std::size_t capacity)` | `Result<std::size_t>` | Writes a BoundedString with null-terminator into target memory. |
| `compare_write_verify<T>(Address address, const T& value, std::uint32_t max_attempts = 1)` | `Result<T>` | Writes a value, reads it back, and retries while verification mismatches, bounded by `max_attempts` (default: one attempt, no retry). Returns the requested value on match; exhausting the budget on mismatch returns `verification_mismatch`. Not an atomic compare-and-swap. |

### 2. Freeze & Worker Management (`gtlibcpp::FreezeManager`)
Header: [`include/gtlibcpp/freeze.hpp`](include/gtlibcpp/freeze.hpp)

| Method Signature | Return Type | Description |
|------------------|-------------|-------------|
| `FreezeManager(std::shared_ptr<MemorySession> session)` | Constructor | Initializes the freeze manager bound to an active memory session. |
| `freeze(FreezeRequest request)` | `Result<void>` | Starts a background worker loop periodically rewriting a target address. |
| `unfreeze(const std::string& id)` | `Result<void>` | Stops and joins the freeze worker without restoring memory. |
| `restore(const std::string& id)` | `Result<void>` | Restores captured pre-freeze bytes after the worker has stopped. |
| `status(const std::string& id) const` | `Result<FreezeStatus>` | Queries execution status, write count, interval, and state of a worker. |
| `all_status() const` | `std::vector<FreezeStatus>` | Returns snapshots of all active and inactive freeze workers. |
| `cancel_all()` | `void` | Signals cancellation and cleanly joins all running freeze threads. |

### 3. Safety, Policy & Gatekeeping (`gtlibcpp::Policy`)
Header: [`include/gtlibcpp/policy.hpp`](include/gtlibcpp/policy.hpp)

| Method Signature | Return Type | Description |
|------------------|-------------|-------------|
| `Policy(std::vector<TargetManifest> manifests, AuditSink sink, void* user)` | Constructor | Configures authorized targets, capability definitions, and audit sink. |
| `authorize(const TargetIdentity& target, const MutationRequest& request) const` | `Result<void>` | Validates target authorization, bounds, kill switch, and rate limits. |
| `authorize_with_token(const TargetIdentity&, const MutationRequest&, const ApprovalToken*) const` | `Result<void>` | Authorizes mutations requiring cryptographic approval tokens and TTL. |
| `set_kill_switch_reason(std::string reason) noexcept` | `void` | Sets a descriptive explanation for the kill switch state. |
| `emit_audit(AuditEvent event) const noexcept` | `void` | Dispatches structured security audit events to the registered sink. |
| `manifests() const noexcept` | `const std::vector<TargetManifest>&` | Inspects all registered authorized application manifests. |
| `rate_limiter()` | `RateLimiter&` | Accesses the per-target operation rate limiter. |
| `kill_switch()` | `const KillSwitch&` | Accesses the atomic global kill switch monitor. |
| `KillSwitch::engage(std::string reason) noexcept` | `void` | Instantly halts all mutation authorizations across all sessions. |
| `KillSwitch::release() noexcept` | `void` | Re-enables policy authorizations after inspection. |
| `KillSwitch::engaged() const noexcept` | `bool` | Checks whether the kill switch is currently active. |
| `RateLimiter::allow(const std::string& alias)` | `Result<void>` | Consumes an operation slot or returns `ErrorCode::rate_limited`. |

### 4. Cheat Table Parsing (`gtlibcpp::CheatTableParser`)
Header: [`include/gtlibcpp/parser.hpp`](include/gtlibcpp/parser.hpp)

| Method Signature | Return Type | Description |
|------------------|-------------|-------------|
| `parse_file(const std::string& path) const` | `Result<CheatTable>` | Parses Cheat Engine 7.x `.ct` file from filesystem with diagnostics. |
| `parse_string(const std::string& xml) const` | `Result<CheatTable>` | Parses Cheat Engine XML content string directly from memory. |
| `sanitize(const std::string& input)` | `std::string` | Sanitizes user inputs, removing invalid XML control sequences. |
| `parse_variable_type(const std::string& text, bool& is_signed)` | `Result<VariableType>` | Converts Cheat Engine type strings into strongly typed `VariableType`. |
| `to_string(VariableType t)` | `std::string` | Formats enum `VariableType` as a canonical text string. |

### 5. Autonomous Agent Control Plane (`gtlibcpp::AgentService`)
Header: [`include/gtlibcpp/agent.hpp`](include/gtlibcpp/agent.hpp)

| Method Signature | Return Type | Description |
|------------------|-------------|-------------|
| `AgentService(...)` | Constructor | Constructs the agent dispatch service injecting session, policy, and parser. |
| `handle(const std::string& request_json)` | `Result<std::string>` | Executes a single JSON-RPC 2.0 request/response cycle. |
| `serve(std::shared_ptr<IAgentTransport> transport)` | `void` | Starts a blocking message loop handling requests from a transport stream. |
| `stop()` | `void` | Terminates the active transport listening loop. |

#### JSON-RPC 2.0 Agent Wire Methods

Addresses and offsets are JSON **integers** (decimal notation), not quoted hexadecimal strings. `read` and `verify` accept a byte count in `size`; `read` does not accept a `type` parameter. All examples below show the `params` object; wrap it in `{"jsonrpc":"2.0","id":"1","method":"...","params":{...}}`.

| JSON-RPC Method | Request Payload Parameters | Description |
|-----------------|----------------------------|-------------|
| `inspect` | `{}` | Returns target identity and architecture. |
| `read` | `{"address":4198400,"size":4}` | Reads the requested number of bytes. |
| `resolve` | `{"base":4194304,"offsets":[16,4]}` | Resolves a pointer chain with numeric offsets. |
| `preview` | `{"request_id":"op-1","target_alias":"fixture","capability":"write","address":4198400,"size":4,"expected_current_hash":"<current-sha256>","value_hex":"64000000","value_repr":"uint32 100"}` | Checks an intended mutation without writing. |
| `apply` | `{"request_id":"op-1","target_alias":"fixture","capability":"write","address":4198400,"size":4,"expected_current_hash":"<current-sha256>","approved":true,"value_hex":"64000000","value_repr":"uint32 100"}` | Applies a policy-approved mutation. |
| `verify` | `{"address":4198400,"size":4,"expected_hash":"..."}` | Compares a content hash. |
| `restore` | `{"freeze_id":"ammo"}` | Disabled over RPC until authenticated rollback authorization is implemented. |
| `freeze` | `{"id":"ammo","address":4198400,"type":"uint32","value":100,"interval_ms":50}` | Starts periodic writes. |
| `unfreeze` | `{"id":"ammo"}` | Currently denied over JSON-RPC until scoped operator authorization is implemented; trusted C++ callers may use `FreezeManager::unfreeze`. |
| `status` | `{"id":"ammo"}` or `{}` | Queries freeze status. |
| `parse` | `{"xml":"<CheatTable/>"}` | Parses a Cheat Engine XML table. |
| `kill_switch` | `{"engage":true,"reason":"operator"}` | Engages the safety switch; RPC release is denied without trusted-host authorization. |

### 6. Observability & Logging (`gtlibcpp::Logger`)
Header: [`include/gtlibcpp/log.hpp`](include/gtlibcpp/log.hpp)

| Method Signature | Return Type | Description |
|------------------|-------------|-------------|
| `instance()` | `Logger&` | Returns the process-wide logger singleton. |
| `set_sink(Sink sink, void* user) noexcept` | `void` | Injects a custom callback sink for structured log events. |
| `reset_sink() noexcept` | `void` | Restores the default stderr console logging sink. |
| `set_min_level(LogLevel level) noexcept` | `void` | Sets minimum filtering severity (`debug`, `info`, `warn`, `error`). |
| `log(LogLevel, op, code, msg, addr, bytes)` | `void` | Emits a structured `LogEvent` with full context metadata. |
| `debug(...)` / `info(...)` / `warn(...)` / `error(...)` | `void` | Convenience helpers for standard severity logging. |

### 7. Memory Backends & Transports
Headers: [`include/gtlibcpp/windows_backend.hpp`](include/gtlibcpp/windows_backend.hpp), [`include/gtlibcpp/named_pipe_transport.hpp`](include/gtlibcpp/named_pipe_transport.hpp), [`include/gtlibcpp/inproc_transport.hpp`](include/gtlibcpp/inproc_transport.hpp)

| Method / Function | Return Type | Description |
|-------------------|-------------|-------------|
| `make_windows_backend(WindowsBackendOptions)` | `MemoryBackendPtr` | Creates a Win32 backend (`OpenProcess`, `ReadProcessMemory`, `WriteProcessMemory`). |
| `make_named_pipe_server(NamedPipeServerOptions)`| `std::shared_ptr<IAgentTransport>` | Creates a Windows named pipe server (`\\.\pipe\<name>`) with framing. |
| `make_named_pipe_client(std::string pipe_name)`| `std::shared_ptr<IAgentTransport>` | Creates a client connection for Windows named pipe RPCs. |
| `std::make_shared<InProcTransportPair>()` | `std::shared_ptr<InProcTransportPair>` | Creates a shared in-process queue pair; call `make_client()` and `make_server()` on the shared pair. |

### 8. Legacy Win32 Trainer Surface (`GTLibc`)
Header: [`GTLibc.hpp`](GTLibc.hpp) | Implementation: [`GTLibc.cpp`](GTLibc.cpp)

| Method Signature | Return Type | Category | Description |
|------------------|-------------|----------|-------------|
| `GTLibc()` | Constructor | Lifecycle | Default constructor; initializes Win32 process state. |
| `GTLibc(bool enableLogs)` | Constructor | Lifecycle | Constructor configuring verbose diagnostic logging. |
| `GTLibc(const std::string &gameName)` | Constructor | Lifecycle | Automatically attaches to target game process name. |
| `GTLibc(const std::string &gameName, bool enableLogs)` | Constructor | Lifecycle | Attaches to target game with diagnostic logging enabled. |
| `FindGameProcess(const std::string &gameName)` | `HANDLE` | Process | Finds process by exe name; falls back gracefully under Wine/hardened hosts. |
| `FindGameWindow(const std::string &windowName)` | `HWND` | Window | Locates main game window handle matching title. |
| `GetGameHandle()` | `HANDLE` | Process | Returns current opened target Win32 process handle. |
| `GetProcessId()` | `DWORD` | Process | Returns target process ID (PID). |
| `GetGameName()` | `std::string` | Process | Returns cached target process name. |
| `GetGameBaseAddress()` | `DWORD` | Process | Retrieves the base module address for the game executable. |
| `GetGameHandle4mHWND(HWND hwnd)` | `HANDLE` | Process | Resolves process handle from window handle with Wine fallback. |
| `GetProcessID4mHWND(HWND hwnd)` | `DWORD` | Process | Extracts process ID directly from window handle. |
| `ReadAddress<T>(DWORD address)` | `T` | Memory R/W | Reads a primitive value directly from target address. |
| `WriteAddress<T>(DWORD address, const T &value)` | `bool` | Memory R/W | Writes primitive value directly to target address. |
| `ReadAddressOffset<T>(DWORD address, DWORD offset)` | `T` | Offsets | Reads value at address + offset. |
| `WriteAddressOffset<T>(DWORD address, DWORD offset, const T &value)` | `bool` | Offsets | Writes value at address + offset. |
| `ReadAddressOffsets<T>(DWORD address, const std::vector<DWORD> &offsets)` | `T` | Offsets | Evaluates multi-level offset chain and reads value. |
| `WriteAddressOffsets<T>(DWORD address, const std::vector<DWORD> &offsets, const T &val)`| `bool` | Offsets | Evaluates multi-level offset chain and writes value. |
| `ReadPointer<T>(DWORD address)` | `T` | Pointers | Dereferences pointer address and reads value. |
| `WritePointer<T>(DWORD address, DWORD pointerOffset, const T &value)` | `bool` | Pointers | Writes value into dereferenced pointer address. |
| `ReadPointerOffset<T>(DWORD address, DWORD offset)` | `T` | Pointers | Dereferences pointer with a single offset and reads value. |
| `WritePointerOffset<T>(DWORD address, const std::vector<DWORD> &offsets, const T &val)` | `bool` | Pointers | Writes value to dereferenced pointer with offsets. |
| `ReadPointerOffsets<T>(DWORD address, const std::vector<DWORD> &offsets)` | `T` | Pointers | Traverses pointer offset list and reads terminal value. |
| `WritePointerOffsets<T>(DWORD address, const std::vector<DWORD> &offsets, const T &val)`| `bool` | Pointers | Traverses pointer offset list and writes terminal value. |
| `ReadString(DWORD address, size_t size)` | `std::string` | Strings | Reads raw string from process memory. |
| `WriteString(DWORD address, const std::string &str)` | `bool` | Strings | Writes null-terminated string to process memory. |
| `HotKeysDown(const std::vector<int> &keys)` | `bool` | Hotkeys | Checks if all specified virtual keycodes are held down. |
| `IsKeyPressed(int keycode)` | `bool` | Hotkeys | Checks if a single virtual keycode is currently pressed. |
| `IsKeyToggled(int keycode)` | `bool` | Hotkeys | Checks if a toggle key (e.g. CapsLock, NumLock) is active. |
| `EnableLogs(bool status)` | `void` | Diagnostics | Toggles verbose diagnostic logging output. |
| `ReadCheatTable(...)` *(under `GT_USE_CE_PARSER`)* | `CheatTable` | Tables | Parses Cheat Engine table file. |
| `AddCheatTableEntry(...)` *(under `GT_USE_CE_PARSER`)* | `void` | Tables | Manually registers an in-memory cheat table entry. |
| `DisplayCheatTable(...)` *(under `GT_USE_CE_PARSER`)* | `void` | Tables | Prints formatted table entries and hotkeys to stdout. |
| `ReadCheatTableEntries()` *(under `GT_USE_CE_PARSER`)* | `void` | Tables | Reads current target values for all loaded entries. |
| `ActivateCheatTableEntries(...)` *(under `GT_USE_CE_PARSER`)* | `void` | Tables | Toggles or freezes selected cheat entry indices. |
| `ExecuteCheatTable(...)` *(under `GT_USE_CE_PARSER`)* | `void` | Tables | Starts interactive trainer loop processing hotkeys. |

---

## Quick Start

### Modern C++17 Core

```cpp
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/windows_backend.hpp"
#include <iostream>

int main() {
    gtlibcpp::WindowsBackendOptions opts{};
    opts.pid          = 1234;                                // Authorised target PID
    opts.image_path   = "C:/Games/AuthorisedGame/game.exe";  // Validated image path
    opts.architecture = gtlibcpp::Architecture::x64;

    auto backend = gtlibcpp::make_windows_backend(opts);
    if (!backend) {
        std::cerr << "Failed to connect to process.\n";
        return 1;
    }

    gtlibcpp::MemorySession session(backend);

    // Read typed value safely without unchecked conversions
    auto result = session.read<std::uint32_t>(0x140401000);
    if (!result) {
        std::cerr << "Read failed: " << result.error().message 
                  << " (Operation: " << result.error().operation << ")\n";
        return 1;
    }

    std::cout << "Value: " << result.value() << "\n";
    return 0;
}
```

### JSON-RPC Agent Automation

The in-process example needs an active service on the other end of the transport. The following complete example uses the simulated backend from `examples/simulated_game.hpp` (build it inside this repository's examples directory):

```cpp
#include "gtlibcpp/agent.hpp"
#include "gtlibcpp/inproc_transport.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/policy.hpp"
#include "gtlibcpp/parser.hpp"
#include "simulated_game.hpp"
#include <iostream>
#include <memory>
#include <vector>

int main() {
    auto game = std::make_shared<gtlibcpp::demo::SimulatedGameProcess>();
    auto session = std::make_shared<gtlibcpp::MemorySession>(game);
    auto policy = std::make_shared<gtlibcpp::Policy>(
        std::vector<gtlibcpp::TargetManifest>{});
    auto freeze = std::make_shared<gtlibcpp::FreezeManager>(session);
    auto parser = std::make_shared<gtlibcpp::CheatTableParser>();
    auto agent = std::make_shared<gtlibcpp::AgentService>(
        session, policy, freeze, parser);

    auto channel = std::make_shared<gtlibcpp::InProcTransportPair>();
    auto client = channel->make_client();
    agent->serve(channel->make_server());  // Start handler before receiving.

    auto sent = client->send(
        R"({"jsonrpc":"2.0","id":"1","method":"inspect","params":{}})");
    if (!sent) {
        channel->close();
        agent->stop();
        return 1;
    }
    auto response = client->receive();
    if (response) std::cout << "Agent Response: " << response.value() << '\n';

    channel->close(); // Unblock server receive before joining worker.
    agent->stop();
    return response ? 0 : 1;
}
```

See [examples/demo_agent_service.cpp](examples/demo_agent_service.cpp) for the full working agent and policy flow.

---

## Native MCP and JSON-RPC AI Trainer

Build `gtlibcpp_agent_trainer` in `examples/` to connect AI coding agents using **MCP stdio** or direct GTLibCpp **JSON-RPC 2.0** (`--json-rpc`). The initial tool set is `gt_inspect`, `gt_read`, `gt_resolve`, and `gt_status`.

The example uses simulated game memory and is intentionally **read-only** until review blockers in mutation authorization and transport security are resolved. See [MCP Trainer Setup & Examples](docs/MCP_TRAINER.md) for build steps, agent configuration and example requests.

---

## Build & Test Instructions

Build with CMake:

```bash
# Configure with test suite enabled
cmake -S . -B build -DGTLIBCPP_BUILD_TESTS=ON

# Compile in Release mode
cmake --build build --config Release

# Run complete regression and security test suites
ctest --test-dir build --output-on-failure
```

### Build Options

| CMake Option | Default | Description |
|--------------|---------|-------------|
| `GTLIBCPP_BUILD_TESTS` | `ON` | Builds the 4 cross-platform regression test executables. |
| `GTLIBCPP_BUILD_DEMOS` | `OFF` | Builds demo trainers (`IGITrainer`, `GenericTrainer`). |
| `GTLIBCPP_ENABLE_WERROR` | `ON` | Treats compiler warnings as errors (`/WX` on MSVC, `-Werror` on Clang/GCC). |

### Test Suites

```
Test project build
      Start 1: gtlibcpp_core_tests ........ Passed (15/15 tests)
      Start 2: gtlibcpp_parser_tests ...... Passed (12/12 tests)
      Start 3: gtlibcpp_agent_tests ....... Passed (10/10 tests)
      Start 4: gtlibcpp_security_tests .... Passed (5/5 tests)

100% tests passed, 0 tests failed out of 4 (42 assertions verified).
```

---

## AI Agent Trainer (MCP + JSON-RPC)

Build `gtlibcpp_agent_trainer` with `GTLIBCPP_BUILD_EXAMPLES=ON` to expose the read-only trainer to AI coding agents via MCP over stdio. The same executable supports direct newline-delimited JSON-RPC with `--json-rpc`. It uses a simulated game by default and can attach read-only to an authorized Windows PID.

See [MCP setup and runnable C++ trainer client](docs/MCP.md) and [examples/agent_trainer_client.cpp](examples/agent_trainer_client.cpp). Mutating tools remain disabled until the outstanding security review findings are fixed.

---

## Documentation Index

| File | Description |
|------|-------------|
| [CHANGELOG.md](CHANGELOG.md) | Release history, version notes, and feature additions. |
| [SECURITY.md](SECURITY.md) | Authorised-use policy, threat model, and vulnerability reporting. |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layered design, thread safety, WOW64 compatibility, and CI. |
| [docs/API.md](docs/API.md) | Deep public API reference for every method, struct, and enum. |
| [docs/POLICY.md](docs/POLICY.md) | Manifest schemas, capability controls, rate limits, and audits. |
| [docs/PARSER.md](docs/PARSER.md) | Supported Cheat Engine XML subset and fail-closed rules. |
| [docs/AGENT.md](docs/AGENT.md) | JSON-RPC 2.0 wire protocol, methods, and error reference. |
| [docs/EXAMPLES.md](docs/EXAMPLES.md) | 8 comprehensive end-to-end examples (R/W, Freeze, Parse, Agent). |
| [docs/MIGRATION.md](docs/MIGRATION.md) | Guide for migrating legacy `GTLibc` code to `gtlibcpp::*`. |
| [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md) | Security boundaries, trust domains, and risk mitigations. |

---

## Authorised Use & Licensing

**Project policy:** GTLibCpp is intended for authorised offline development, modding, debugging, and single-player research. It is not intended for unauthorized access, unlawful activity, or anti-cheat bypass.

**License terms:** The [Heaven-HM Community Modding License](LICENSE) permits lawful, free, noncommercial use, modification, and redistribution of covered material, subject to attribution, notice, same-license, and third-party-rights conditions. Commercial use or permissions beyond the license require separate written permission. The license prohibits unlawful conduct and unauthorized access; it does not impose a blanket ban on networking. See [LICENSE_SCOPE.md](LICENSE_SCOPE.md) for previously granted rights.
