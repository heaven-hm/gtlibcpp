# gtlibcpp architecture

The production-baseline library ships under `include/gtlibcpp/` and
`src/`. The legacy `GTLibc.*` files are retained for backwards
compatibility but are no longer the recommended path.

## Goals

1. Every public operation returns a structured result. Read
   failures cannot be confused with valid zero/empty values.
2. The Windows handle is owned by a single `IMemoryBackend`; the
   `MemorySession` that holds it is non-copyable and non-movable.
3. Addresses are 64-bit (`std::uint64_t`) and the library
   transparently supports both 32-bit and 64-bit games. For a 32-bit
   target, the high 32 bits of every address are zero; the address
   type still round-trips through the JSON-RPC surface without
   truncation. Pointer chains use `std::uintptr_t` (4 bytes on x86,
   8 bytes on x64); `resolve_pointer_chain` always reads the
   correct width for the recorded architecture.
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

## Supported architectures

* **32-bit (x86) games**: the `WindowsBackend` opens the target
  with `PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION
  | PROCESS_QUERY_INFORMATION` on a 32-bit Windows host. `Address`
  is 64-bit but the upper 32 bits are zero; `read<std::uintptr_t>`
  reads 4 bytes and `read<std::uint64_t>` reads 8 bytes (the high
  4 bytes will be zero for a 32-bit pointer). The Windows CI
  matrix exercises the `Win32` (x86) target on MSVC and clang-cl.
* **64-bit (x64) games**: same as above, on a 64-bit Windows host.
  `read<std::uintptr_t>` reads 8 bytes. The Windows CI matrix
  exercises the `x64` target on MSVC and clang-cl.
* **WOW64 (32-bit game on 64-bit host)**: the `WindowsBackend`
  opens the target with the same access mask. `IsWow64Process`
  is not required to read or write memory; the high bits of
  addresses are simply zero. The library treats the target as
  `Architecture::x86` regardless of the host arch.

## Cross-architecture notes

* The legacy `GTLibc` API used `DWORD` for every address, which
  silently truncated on x64. The new `Address` type is
  `std::uint64_t` and there is no narrowing anywhere in the core.
* The policy layer's `TargetIdentity.architecture` is recorded at
  attach time and used by the policy gates; a manifest that
  declares `x86` will not match an x64 process, and vice versa.
* The JSON-RPC `read`/`write`/`apply` payloads carry addresses as
  JSON numbers, which JSON parses as signed 64-bit integers. For
  64-bit games this is fine; for 32-bit games the high 32 bits
  are zero, so a JSON number with the high bit set is not a
  valid 32-bit address and is rejected by the policy layer with
  `invalid_address`.

The Windows CI matrix (`.github/workflows/windows-ci.yml`) covers
all four combinations of MSVC and clang-cl on x86 and x64.
