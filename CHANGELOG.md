# Changelog

## [Unreleased] — Issue #1 production baseline

### Added
- New cross-platform `gtlibcpp::` namespace (Result, IMemoryBackend,
  MemorySession, Policy, FreezeManager, CheatTableParser, AgentService)
  in `include/gtlibcpp/` and `src/`.
- Architecture-safe address type (`std::uint64_t`), typed
  `TargetIdentity` (pid, start time, image path, image SHA-256,
  architecture), `IMemoryBackend` interface, and `MemorySession`.
- `BoundedString` for safe, capacity-checked string reads/writes.
- `FreezeManager` with cancellable / joinable workers and explicit
  restore.
- `Policy` class with target manifests, capability gating, approval
  tokens, TTL, rate limiter, kill switch, and an injected audit sink.
- `CheatTableParser` with a versioned supported subset, stable IDs,
  preserved multiple hotkeys, malformed-XML rejection, and fail-closed
  behaviour for Auto Assembler / LoadLibrary / ShellExecute.
- `AgentService` JSON-RPC 2.0 surface over `IAgentTransport`.
- `InProcTransport` and `NamedPipeTransport` (Windows production).
- CMake build with `gtlibcpp::core` and `gtlibcpp::win32` library
  targets, install + export, and `ctest` integration.
- GitHub Actions Windows CI matrix covering MSVC and clang-cl on
  x86 and x64.
- 32 regression tests across `core_tests`, `parser_tests`, and
  `agent_tests`.
