# gtlibcpp architecture

The production-baseline library ships under `include/gtlibcpp/` and
`src/`. The legacy `GTLibc.*` files are retained for backwards
compatibility but are no longer the recommended path.

## Goals

1. Every public operation returns a structured result. Read
   failures cannot be confused with valid zero/empty values.
2. The Windows handle is owned by a single `IMemoryBackend`; the
   `MemorySession` that holds it is non-copyable and non-movable.
3. Addresses are 64-bit. The 32-bit `DWORD` truncation is gone.
4. Every multi-step operation returns a `BatchResult` that names
   the addresses that completed and the address that failed.
5. The freeze manager uses joinable, cancellable workers; the
   detached-thread pattern is gone.
6. The Cheat Engine parser is XML-driven, not regex-driven.
7. The agent is a separate local-only service.
8. The policy layer is read-only by default.

## Layers

```
                 +-----------------------------+
                 |       AgentService          |  <- JSON-RPC, IAgentTransport
                 +-----------------------------+
                 |  Policy (manifests, caps)  |  <- RateLimiter, KillSwitch, audit
                 +-----------------------------+
                 |  FreezeManager / Parser    |
                 +-----------------------------+
                 |       MemorySession        |  <- RAII, typed reads/writes
                 +-----------------------------+
                 |     IMemoryBackend (I/O)    |  <- Windows or Fake
                 +-----------------------------+
```

## Build

```
cmake -S . -B build -DGTLIBCPP_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Windows MSVC and clang-cl x86 and x64 are exercised in
`.github/workflows/windows-ci.yml`.
