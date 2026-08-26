# Changelog

## [Unreleased] — Issue #1 production baseline

### Added
- New cross-platform `gtlibcpp::` namespace (Result, IMemoryBackend,
  MemorySession, Policy, FreezeManager, CheatTableParser, AgentService)
  in `include/gtlibcpp/` and `src/`.
- Architecture-safe address type (`std::uint64_t`) with transparent
  support for both **32-bit and 64-bit games** (legacy `GTLibc`'s
  `DWORD` truncation is gone; the high 32 bits are zero for x86
  targets and the type still round-trips through the JSON-RPC
  surface). `Architecture` is recorded in `TargetIdentity` and used
  by the policy gates and pointer-chain resolver.
- `BoundedString` for safe, capacity-checked string reads/writes.
- `FreezeManager` with cancellable / joinable workers and explicit
  restore. The interval knob honours sub-50 ms requests correctly
  (the prior integer-division off-by-50 is fixed).
- `Policy` class with target manifests, capability gating, approval
  tokens, TTL, rate limiter, kill switch, and an injected audit sink.
- `CheatTableParser` with a versioned supported subset, stable IDs,
  preserved multiple hotkeys, malformed-XML rejection, and fail-closed
  behaviour for Auto Assembler / LoadLibrary / ShellExecute.
- `AgentService` JSON-RPC 2.0 surface over `IAgentTransport`.
- `InProcTransport` and `NamedPipeTransport` (Windows production).
  The named-pipe server is multi-shot and the client half is
  implemented (was a stub returning `nullptr` in the first cut).
- `WindowsBackend` with a real `GetProcessTimes` start time and a
  real SHA-256 image hash via `BCrypt` (the prior version
  short-circuited the manifest-match path with empty / bogus
  identity fields).
- CMake build with `gtlibcpp::core` and `gtlibcpp::win32` library
  targets, install + export, and `ctest` integration.
- GitHub Actions Windows CI matrix covering MSVC and clang-cl on
  x86 and x64 (4 configurations).
- Real per-test harness (`tests/gtlibcpp_test.hpp`) so ctest sees
  per-test pass / fail instead of one exit code.
- 32+ regression tests across `core_tests`, `parser_tests`, and
  `agent_tests`.

### Fixed (review of PR #4)
- `Result<T>::value()` now aborts on error (no more silent
  default-value fallback). Added `value_or` and `expect` for
  explicit opt-ins. The previous behaviour directly violated the
  core invariant.
- Freeze interval: a 1 ms request no longer silently becomes 50 ms.
- Windows identity: real `GetProcessTimes` start time and real
  SHA-256 image hash via BCrypt.
- Named-pipe client: implemented via `CreateFileA`.
