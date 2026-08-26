# Changelog

All notable changes to GTLibCpp are documented in this file. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased] — Issue #1 production baseline

### Added
- New cross-platform `gtlibcpp::` namespace (Result, IMemoryBackend,
  MemorySession, Policy, FreezeManager, CheatTableParser, AgentService)
  in `include/gtlibcpp/` and `src/`. The new core is the production
  baseline and is the only library target the agent service is allowed
  to link against.
- Architecture-safe address type (`std::uint64_t`), typed
  `TargetIdentity` (pid, start time, image path, image SHA-256,
  architecture), `IMemoryBackend` interface, and `MemorySession` that
  owns the backend shared_ptr.
- `BoundedString` for safe, capacity-checked string reads/writes with
  optional NUL-termination enforcement.
- `FreezeManager` with cancellable / joinable workers, original-value
  snapshotting, and explicit restore.
- `Policy` class with target manifests, capability gating, approval
  tokens, TTL, rate limiter, kill switch, and an injected audit sink.
- `CheatTableParser` with a versioned supported subset, stable IDs,
  preserved nested entries, preserved multiple hotkeys, signed/hex
  values, malformed-XML rejection, and fail-closed behaviour for
  Auto Assembler / LoadLibrary / ShellExecute.
- `AgentService` JSON-RPC 2.0 surface over `IAgentTransport` with
  methods: `inspect`, `read`, `resolve`, `preview`, `apply`,
  `verify`, `restore`, `freeze`, `unfreeze`, `status`, `parse`,
  `kill_switch`.
- `InProcTransport` for tests and headless agents and
  `NamedPipeTransport` for the Windows production build
  (`gtlibcpp::win32` target, Windows-only).
- CMake build with `gtlibcpp::core` (cross-platform) and
  `gtlibcpp::win32` (Windows-only) library targets, an
  `add_subdirectory` / `find_package` install + export, a
  `GTLIBCPP_BUILD_TESTS` option, and `ctest` integration.
- GitHub Actions Windows CI matrix covering MSVC and clang-cl on
  x86 and x64.
- 32 regression tests across `core_tests`, `parser_tests`, and
  `agent_tests` covering every bug line called out in issue #1 and
  every acceptance criterion the issue lists.

### Changed
- The legacy `GTLibc.hpp` / `GTLibc.cpp` / `GTLibc.tpp` / demo
  trainers are preserved for backwards compatibility and continue
  to build under the optional `GTLIBCPP_BUILD_DEMOS` flag, but they
  are no longer the recommended API. The README and issue tracker
  now point at the new core.

### Removed
- None. The legacy API is deprecated but still ships; removal is
  scheduled for a major version bump after one full release cycle
  of the new core.
