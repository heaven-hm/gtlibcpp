# REVIEW_CRITIC.md — PR #4 (feat/issue-1-production-baseline)

This document collects the reviewer's findings. The diff for this PR is
the entire new tree under `include/gtlibcpp/`, `src/`, `tests/`,
`.github/workflows/`, `docs/`, `examples/`, and `CMakeLists.txt`. Line
numbers reference the on-disk files in the feature branch.

---

## Finding 1 — MUST FIX — `WindowsBackend::query_image_sha256` is a stub and silently disables identity-based authorization

**File:** `src/windows_backend.cpp` (around lines 110–140)
**Labels:** correctness, security, threat-model gap

The constructor captures the target's `TargetIdentity` and the entire
`Policy::authorize_with_token` flow depends on `image_sha256` and
`start_time` being populated (see `src/policy.cpp`,
`TargetManifest::matches`):

```cpp
if (!image_sha256.empty() && !id.image_sha256.empty()
    && id.image_sha256 != image_sha256) {
    return false;
}
```

But on Windows the two helpers are no-ops:

```cpp
static std::uint64_t query_start_time(std::uint32_t pid) {
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    ...
    start = static_cast<std::uint64_t>(entry.dwFlags) << 32
          | static_cast<std::uint64_t>(entry.pcPriClassBase);   // <-- nonsense
    ...
}

static std::string query_image_sha256(std::uint32_t /*pid*/) {
    return {};                                                  // <-- always empty
}
```

Consequences:

1. The SHA-256 anchor that `THREAT_MODEL.md` lists as the primary
   defence against "agent mutates a target it was not authorised for"
   is never actually checked, because the field is always empty and
   `matches` short-circuits on the empty-check. Any process whose
   `image_path` happens to match the manifest path will be accepted.
2. `start_time` is reconstructed from PROCESSENTRY32 fields that are
   not a start time. `TargetIdentity::operator==` compares
   `start_time`, so two unrelated processes at the same `image_path`
   (e.g. relaunched game) will be treated as the same identity and
   the rate limiter / approval-token correlation will silently allow
   a fresh process to reuse a previously issued token.

**Suggested fix:**

- For `image_sha256`: open the target's `image_path` with
  `CreateFileW(..., FILE_SHARE_READ)`, hash the executable with
  the SHA-256 implementation already required by the project
  (or vendor a small public-domain SHA-256), and return the lower-case
  hex digest. If the file is unreadable, fail the backend constructor
  (the user is allowed to pass an expected hash, but the live value
  must be real).
- For `start_time`: use
  `GetProcessTimes(handle_, &creation, &exit, &kernel, &user)` and
  return `creation.dwLowDateTime | (creation.dwHighDateTime << 32)`
  as Unix-style epoch-100ns (or document the units and zero on
  failure). Better: switch `TargetIdentity::start_time` to a
  well-defined unit and store the real creation time.
- Add a regression test that opens a `WindowsBackend` against a
  known fixture and asserts `identity().image_sha256` is non-empty
  and matches a pre-computed hash. Until that test exists, the
  "manifest match" path in `Policy` is effectively untested.

---

## Finding 2 — MUST FIX — `make_named_pipe_client` is a stub returning `nullptr`

**File:** `src/named_pipe_transport.cpp` (lines 153–157)
**Labels:** public-API correctness, scope mismatch with CHANGELOG

The CHANGELOG advertises:

> `InProcTransport` and `NamedPipeTransport` (Windows production).

But the client half of the named-pipe transport is a stub:

```cpp
std::shared_ptr<IAgentTransport>
make_named_pipe_client(const std::string& /*pipe_name*/) {
    return nullptr;
}
```

A trainer or operator UI that links against `gtlibcpp::win32` and calls
`make_named_pipe_client("gtlibcpp.agent")` will get back a null
`shared_ptr`, dereference it, and crash — or, if they `if (!client)`
check, they will silently have no transport.

This is also a latent memory-safety issue: every call site of
`IAgentTransport` via the named-pipe client path is currently
unreachable, so there is no test coverage. The whole "transport
pluggability" story is half-implemented.

**Suggested fix:**

- Implement the client using `CreateFileA` against
  `\\\\.\\pipe\\<pipe_name>` with `GENERIC_READ | GENERIC_WRITE`
  and the same framed protocol (`kProtocolMagic` + 4-byte length +
  payload) used by the server.
- Add a round-trip regression test that uses both halves of the
  named-pipe transport (the test currently only exercises
  `InProcTransport`).
- Until the client is implemented, either delete the symbol or
  `#error` in the header so consumers don't link against a known-broken
  API.

---

## Finding 3 — SHOULD FIX — `Result<T>::value()` returns a static `T{}` on error, which violates the core invariant

**File:** `include/gtlibcpp/result.hpp` (lines 130–155)
**Labels:** API design, issue #1 invariant

`Result<T>::value()` looks like this:

```cpp
[[nodiscard]] const T& value() const& {
    if (!has_value_) {
        static const T empty{};
        return empty;          // <-- silently returns a default
    }
    return std::get<T>(storage_);
}
```

`Result<void>::value()` does not exist, but the lvalue/rvalue `value()`
overloads on `Result<T>` will return a zero-initialized `T` whenever a
caller forgets to check `ok()`. That **directly contradicts** the
invariant called out at the top of `memory_session.hpp` and in
`docs/ARCHITECTURE.md`:

> Read failures cannot be confused with valid zero/empty values.

A caller who writes `auto v = session.read<std::uint32_t>(addr).value();`
without an `ok()` check will get `0u` and happily proceed as if the
read succeeded. The whole point of the new `Result` type is to make
this impossible; the current `value()` accessor makes it *quietly
possible*.

This is also the single most likely place a future contributor will
introduce a regression that issue #1 was meant to prevent.

**Suggested fix:**

- Replace the silent default with an abort/throw. Recommended
  pattern (mirrors the standard library's `std::optional`/
  `std::expected`):
  ```cpp
  [[nodiscard]] const T& value() const& {
      if (!has_value_) {
          std::fputs("Result::value() called on error\n", stderr);
          std::abort();
      }
      return std::get<T>(storage_);
  }
  ```
- Add a `Result<T>::value_or(T fallback)` for callers who genuinely
  want a default — this makes the opt-in to "treat as zero" explicit.
- Add a `Result<T>::expect(const char* msg)` for the success-only
  case with a better diagnostic than `abort()`.
- Add a unit test that confirms calling `value()` on a failure
  Result aborts (e.g. fork-and-check, or under a death-test
  framework).

---

## Finding 4 — SHOULD FIX — Freeze worker interval is broken for any value < 50 ms; tests use raw `assert` so a single failure silently aborts the rest of the suite

**Files:**
- `src/freeze.cpp` lines 60–62 and 122–131 (interval clamp + sleep loop)
- `tests/core_tests.cpp`, `tests/parser_tests.cpp`,
  `tests/agent_tests.cpp` (every test)

### 4a. Freeze interval off-by-50

```cpp
if (request.interval < std::chrono::milliseconds(1)) {
    request.interval = std::chrono::milliseconds(50);
}
...
for (int slept = 0; slept < 50; ++slept) {
    if (entry->cancel->load() || !entry->active.load()) break;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            std::max<std::chrono::milliseconds::rep>(
                1, entry->request.interval.count() / 50)));
}
```

`request.interval.count() / 50` is integer division; for any
`interval_ms < 50` this yields 0, which is then clamped to 1 ms.
So:

- `interval_ms = 1` → sleep loop runs 50 × 1 ms = 50 ms (50× slower)
- `interval_ms = 49` → sleep loop runs 50 × 0 ms (clamped to 1 ms) = 50 ms
- `interval_ms = 50` → sleep loop runs 50 × 1 ms = 50 ms (correct)
- `interval_ms = 100` → sleep loop runs 50 × 2 ms = 100 ms (correct)
- `interval_ms = 1000` → sleep loop runs 50 × 20 ms = 1000 ms (correct)

So the freeze worker honours the requested interval for `interval_ms ≥
50` and silently rounds anything smaller up to 50 ms total cycle time.
The `interval_ms` knob exposed via the JSON-RPC `freeze` method
(`src/agent.cpp:on_freeze`, defaulting to 50) is therefore not
controllable below 50 ms, contrary to the JSON contract.

**Suggested fix:** compute the total sleep as a single duration and
either sleep it once with cancellation-via-`condition_variable`, or
use a smaller slice (e.g. 1 ms) and poll the cancel flag. The current
50-iteration loop adds 50× the wakeup overhead and silently
mis-computes the cadence.

### 4b. Tests use `assert` + `std::cout`

Every test prints its own "passed" line and then `assert()`s. There
is no test framework. Consequences:

- If `test_partial_write_is_reported` fails on the 12th assertion
  inside `test_freeze_restores_original`, the process aborts and
  the print at the bottom of `main` (`"gtlibcpp core tests: 14 passed"`)
  is never reached — but a *later* `ctest` re-run after a fix will
  still report the same "14 passed" message, which is the same string
  regardless of how many ran. There is no per-test exit code.
- ctest only sees the executable's exit code. If 13 of 14 pass and
  1 asserts, ctest reports the whole suite as failed, with no
  per-test granularity. There is no way to tell *which* test failed
  from CI logs unless the log shows the `assert` message.
- A regression that *skips* an assertion (e.g. future refactor makes
  the test silently no-op) will not be caught — the test will print
  "passed" anyway.

The CHANGELOG says "32 regression tests". With the current harness
that's really "32 asserts, 32 prints" — ctest gets 1 signal.

**Suggested fix:**

- Switch to a real test framework (`Catch2` v3, `doctest`, or
  GoogleTest) and register each test with `TEST_CASE`. ctest then
  sees 32 individual results and the count is real.
- At minimum: replace the `std::cout << "… passed\n"` at the end
  of `main` with a counter that is only incremented when the
  test actually completed all its asserts, and a counter that
  counts the assertions that fired, and a non-zero exit if either
  counter is wrong. Then `ctest --output-on-failure` shows the
  partial summary.
- Add a test that explicitly exercises the freeze interval knob at
  5 ms and 1 ms to lock the cadence contract.

---

## Finding 5 — SHOULD FIX — `NamedPipeServer` is single-shot and the Windows CI matrix is missing x86 + clang-cl

**Files:**
- `src/named_pipe_transport.cpp` lines 70–95 (`NamedPipeServer::receive`)
- `.github/workflows/windows-ci.yml` (matrix)

### 5a. `NamedPipeServer` accepts exactly one client, then locks

`NamedPipeServer::receive()` calls `::CreateNamedPipeA` and
`::ConnectNamedPipe` under a held `std::mutex_`. After the first
message is read, the handle is *not* re-armed with another
`ConnectNamedPipe` (only `close()` releases it). The mutex is held
across the entire blocking read.

A second concurrent `receive()` call from the same server will block
on the mutex forever; a second client trying to connect after the
first message will see `ERROR_PIPE_BUSY` and have no way to retry.
The "agent" promise in the architecture doc is that this is a
service accepting requests; in practice it is a one-shot RPC.

**Suggested fix:** hold the mutex only around the handle state,
and call `CreateNamedPipeA` / `ConnectNamedPipe` in a dedicated
accept loop. Consider using overlapped I/O with `CreateNamedPipeA`'s
`FILE_FLAG_OVERLAPPED` and an event per instance. At minimum, after
each successful read, call `DisconnectNamedPipe` + `CloseHandle`
and re-`CreateNamedPipeA` so a second client can connect.

### 5b. Windows CI matrix is missing x86 + clang-cl

```yaml
matrix:
  include:
    - os: windows-latest
      arch: x64
      generator: "Visual Studio 17 2022"
      triplet: x64
      cxx: "cl"
    - os: windows-latest
      arch: Win32
      generator: "Visual Studio 17 2022"
      triplet: x86
      cxx: "cl"
    - os: windows-latest
      arch: x64
      generator: "Ninja"
      triplet: x64
      cxx: "clang-cl"
```

The PR description claims "MSVC + clang-cl × x86 + x64" — that's 4
configurations, but the matrix has 3. The 32-bit clang-cl build is
missing. x86 is a real target (the legacy `GTLibc` is 32-bit, and
`Architecture::x86` is modelled in `types.hpp`), so this is the
configuration most likely to regress on the 32-bit address-truncation
bug that issue #1 was meant to fix.

**Suggested fix:** add a fourth matrix entry:

```yaml
- os: windows-latest
  arch: Win32
  generator: "Ninja"
  triplet: x86
  cxx: "clang-cl"
```

The Ninja generator can target `Win32` via `-DCMAKE_SYSTEM_NAME=Windows
-DCMAKE_GENERATOR_PLATFORM=Win32`; alternatively use the multi-config
Visual Studio generator for the clang-cl x86 build. Until x86 +
clang-cl is exercised in CI, the "no 32-bit truncation" claim is
unverified for that compiler.

---

## Summary

| # | Severity    | File(s)                                | Title                                                              |
|---|-------------|----------------------------------------|--------------------------------------------------------------------|
| 1 | MUST FIX    | `src/windows_backend.cpp`              | Stub `query_image_sha256` / broken `query_start_time`              |
| 2 | MUST FIX    | `src/named_pipe_transport.cpp`         | `make_named_pipe_client` returns nullptr                           |
| 3 | SHOULD FIX  | `include/gtlibcpp/result.hpp`          | `Result::value()` returns default on error (violates invariant)    |
| 4 | SHOULD FIX  | `src/freeze.cpp` + all test files      | Freeze interval mis-computed below 50 ms; tests lack real harness  |
| 5 | SHOULD FIX  | `src/named_pipe_transport.cpp`, CI yml | Single-shot server; CI matrix missing x86 + clang-cl               |

The two MUST FIX items together undermine the threat model that the
PR is built on top of: identity matching is no-op (Finding 1) and the
production transport's client half is absent (Finding 2). The three
SHOULD FIX items are correctness/ergonomics regressions that, left in
place, will turn the next refactor into a silent failure.
