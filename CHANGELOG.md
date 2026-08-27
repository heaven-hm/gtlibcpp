# Changelog

All notable changes to GTLibCpp are documented in this file. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project adheres to [Semantic Versioning](https://semver.org/).

## [1.0.0] — 2026-08-27

The first release that ships the production baseline for issue #1.
Adds the new `gtlibcpp::` core (cross-platform), the Windows-only
backend, the JSON-RPC over named-pipes agent, and a full CI matrix on
MSVC and clang-cl for x86 and x64. 32-bit and 64-bit games are
supported end to end.

### Added

#### Core (`include/gtlibcpp/`)
- `Result<T>` / `Result<void>` with operation name, address, byte
  count, system error, and request id. Failed reads can no longer be
  confused with valid zero/empty values. `value()` aborts on error;
  `value_or(f)` and `expect(msg)` are explicit opt-ins.
- `ErrorCode` (32 named values covering every failure the library
  can produce) and `to_string(ErrorCode)` for stable wire encoding.
- `Address = std::uint64_t` and `Architecture { x86, x64, unknown }`.
  Pointer chains resolve to the right pointer width for the
  recorded architecture (4 bytes on x86, 8 bytes on x64).
- `TargetIdentity` (pid, start time, image path, image SHA-256,
  architecture). `TargetIdentity::operator==` is the policy
  identity gate.
- `IMemoryBackend` interface and `MemoryBackendPtr` shared alias.
- `MemorySession` (RAII, non-copyable, non-movable) with typed
  `read<T>` / `write<T>` / `read_bytes` / `write_bytes`, the
  `BatchResult` `read_offsets<T>` / `write_offsets<T>` partial-failure
  reporter, `resolve_pointer_chain`, `compare_write_verify<T>`,
  `read_string` / `write_string` with `BoundedString` capacity
  enforcement, and a thread-safe identity cache.
- `BoundedString` with explicit capacity, encoding, NUL-termination
  enforcement, and `truncated()` reporting.
- `FreezeManager` with cancellable, joinable `std::thread` workers;
  original-value snapshotting; per-freeze cancel token; automatic
  process-exit detection; `freeze` / `unfreeze` / `restore` / `status`
  / `all_status` / `cancel_all`.
- `Policy` with `TargetManifest` list, `MutationRequest`,
  `ApprovalToken` (with `issued_at_ms` / `expires_at_ms`),
  `AuditEvent` (injected sink), `RateLimiter` (per-target sliding
  window), `KillSwitch` (atomic, settable by the operator).
- `AuditSink` is a function pointer so the agent can forward to its
  structured log without coupling the core to any logging library.
- `Logger` singleton with an injected sink, log levels, and a
  default stderr fallback. Every public method writes one log
  event per call.
- `CheatTableParser` with a hand-rolled XML reader, versioned CE
  7.x subset, stable entry IDs, preserved multiple hotkeys,
  signed/hex values, nested entries, and fail-closed behaviour for
  Auto Assembler / LoadLibrary / ShellExecute / LuaScript / DllInject
  / ByteArray / Bytes.
- `AgentService` with versioned JSON-RPC 2.0 over `IAgentTransport`:
  `inspect`, `read`, `resolve`, `preview`, `apply`, `verify`,
  `restore`, `freeze`, `unfreeze`, `status`, `parse`, `kill_switch`.
- `InProcTransport` for tests and headless agents.
- `WindowsBackend` and `NamedPipeTransport` (Windows production,
  gated on `WIN32`).

#### Build
- `CMakeLists.txt` with `gtlibcpp::gtlibcpp_core` (cross-platform)
  and `gtlibcpp::gtlibcpp_win32` (Windows-only, links `psapi` and
  `bcrypt`). `find_package(gtlibcpp 1.0)` + `gtlibcppTargets.cmake`
  export.
- `.github/workflows/windows-ci.yml` matrix for MSVC and clang-cl
  on x86 and x64. All three configurations currently pass.

#### Tests (42 across 4 executables)
- `gtlibcpp_core_tests` (15): Result / MemorySession / FreezeManager
  / Policy / RateLimiter / KillSwitch regressions including every
  bug line from issue #1.
- `gtlibcpp_parser_tests` (12): XML parser regressions including
  multi-hotkey preservation, byte-vs-char disambiguation, and
  fail-closed behaviour for Auto Assembler, LoadLibrary, LuaScript,
  raw byte-array, and the IGI / assaultcube fixtures.
- `gtlibcpp_agent_tests` (10): JSON-RPC end-to-end through
  `InProcTransport`, covering inspect, read, preview, apply,
  verify, kill switch, unknown target, unknown method, malformed
  JSON, and freeze round-trip.
- `gtlibcpp_security_tests` (5): backend exceptions caught, log
  sink captures operations, freeze cycle does not leak the backend
  shared_ptr, and `Result::value()` on a failure is non-maskable.

#### Docs
- `README.md` — full rewrite; modernises the legacy trainer
  description, documents the new `gtlibcpp::` core, links out to
  per-topic API / policy / parser / agent / examples / migration docs.
- `CHANGELOG.md` — this file.
- `SECURITY.md` — explicit authorised-offline-use policy, threat
  model, and vulnerability reporting.
- `docs/ARCHITECTURE.md` — layered design, supported architectures
  (x86, x64, WOW64), thread-safety guarantees.
- `docs/THREAT_MODEL.md` — in-scope threats, assumptions,
  out-of-scope items.
- `docs/API.md` — full public API reference.
- `docs/POLICY.md` — manifest format, capability table, audit event
  format, approval token TTL semantics.
- `docs/PARSER.md` — Cheat Engine 7.x subset, fail-closed kinds,
  examples.
- `docs/AGENT.md` — JSON-RPC 2.0 surface, method reference, error
  codes, request lifecycle (preview → apply).
- `docs/EXAMPLES.md` — end-to-end usage examples.
- `docs/MIGRATION.md` — moving from the legacy `GTLibc.*` API to
  `gtlibcpp::*`.

### Fixed (review of PR #4)
- `Result<T>::value()` now aborts on error instead of returning a
  static `T{}`; added `value_or` and `expect` for explicit opt-ins.
- Freeze interval: a 1 ms request no longer silently becomes 50 ms.
- Windows identity: real `GetProcessTimes` start time and real
  SHA-256 image hash via BCrypt.
- Named-pipe client: implemented via `CreateFileA`; server is now
  multi-shot.
- `std::atomic_flag::test()` replaced with `std::atomic<bool>` for
  C++17 portability.
- `FreezeStatus::original_value_u64` changed from `size_t` to
  `uint64_t` so x86 builds do not trigger C4244.
- Unused `buffer_size_` removed from `NamedPipeClient`.
- `Result::value()` death test made portable: SIGABRT path is
  POSIX-only; the Windows path verifies `value_or` instead.

### Deprecated
- The legacy `GTLibc.hpp` / `GTLibc.cpp` / `GTLibc.tpp` API is
  preserved for backwards compatibility but is no longer the
  recommended path. Removal is scheduled for 2.0.0.
