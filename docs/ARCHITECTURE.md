# gtlibcpp architecture

This document describes the production-baseline library that ships
under `include/gtlibcpp/` and `src/`. It is the architecture the
agent service, the freeze workers, the parser, and the trainers go
through. The legacy `GTLibc.*` files are retained for backwards
compatibility but are no longer the recommended path.

## Goals

1. Every public operation returns a structured result. Read
   failures cannot be confused with valid zero/empty values.
2. The Windows handle is owned by a single `IMemoryBackend`; the
   `MemorySession` that holds it is non-copyable and non-movable.
   Two sessions never share state.
3. Addresses are 64-bit. The 32-bit `DWORD` truncation of the
   legacy API is gone.
4. Every multi-step operation returns a `BatchResult` that names
   the addresses that completed and the address that failed.
5. The freeze manager uses joinable, cancellable workers; the
   detached-thread pattern is gone. The original value is
   snapshotted at freeze time and restored on demand.
6. The Cheat Engine parser is XML-driven, not regex-driven. It
   preserves nested entries, multiple hotkeys, signed values,
   hex values, and string metadata. Auto Assembler, LoadLibrary,
   and ShellExecute entries fail closed.
7. The agent is a separate local-only service. The core library
   never opens a network listener and never auto-starts the
   agent. The transport is dependency-injected so tests use
   an in-process channel and production uses a Windows named
   pipe.
8. The policy layer is read-only by default. Every mutation
   requires a target manifest, a capability, an approval, a
   comparison hash, an audit record, and a within-TTL token.

## Layers

```
                 +-----------------------------+
                 |       AgentService          |  <- JSON-RPC, IAgentTransport
                 +-----------------------------+
                 |  Policy (manifests, caps)  |  <- RateLimiter, KillSwitch, audit
                 +-----------------------------+
                 |  FreezeManager / Parser    |  <- cancellable workers, XML
                 +-----------------------------+
                 |       MemorySession        |  <- RAII, typed reads/writes
                 +-----------------------------+
                 |     IMemoryBackend (I/O)    |  <- Windows or Fake
                 +-----------------------------+
```

The cross-platform core compiles and tests on macOS, Linux, and
Windows. The Windows-only backend (`gtlibcpp::win32`) ships as a
separate CMake target that pulls in `psapi` and is gated on
`WIN32`.

## Build

```
cmake -S . -B build -DGTLIBCPP_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Windows MSVC and clang-cl x86 and x64 are exercised in
`.github/workflows/windows-ci.yml`.

## Test surface

* `tests/core_tests.cpp` — Result / MemorySession / FreezeManager /
  Policy regressions, including every bug line from issue #1.
* `tests/parser_tests.cpp` — XML parser regressions, including
  multi-hotkey preservation, byte-vs-char disambiguation, and
  fail-closed behaviour for Auto Assembler / LoadLibrary.
* `tests/agent_tests.cpp` — JSON-RPC end-to-end through
  `InProcTransport`, covering inspect, read, preview, apply,
  verify, freeze/unfreeze, kill switch, and unknown-target
  rejection.
