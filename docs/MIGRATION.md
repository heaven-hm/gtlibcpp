# Migration from the legacy GTLibc API

The legacy `GTLibc.hpp` / `GTLibc.cpp` / `GTLibc.tpp` trainer surface
is still in the tree and is built when `GTLIBCPP_BUILD_DEMOS=ON`.
It is not the supported path and will be removed in 2.0.0. This
document maps every legacy entry point to its `gtlibcpp::*`
replacement and points out behavioural differences.

## Top-level summary

| Legacy                                           | `gtlibcpp::` replacement                                     |
|--------------------------------------------------|-------------------------------------------------------------|
| `bool` return, no error                          | `Result<T>` / `Result<void>` with structured `Error`        |
| `DWORD` addresses                                | `Address = std::uint64_t`                                    |
| `GTLibc::ReadAddress<T>(DWORD)` returning `T`     | `MemorySession::read<T>(Address)` returning `Result<T>`      |
| `GTLibc::WriteAddress<T>(DWORD, T)` returning `bool` | `MemorySession::write<T>(Address, T)` returning `Result<size_t>` |
| `GTLibc::ReadAddressOffsets<T>(DWORD, offsets)`  | `MemorySession::read_offsets` / `write_offsets` / `resolve_pointer_chain` |
| `GTLibc::ReadString(DWORD, size)`                | `MemorySession::read_string(address, capacity, require_nul)` |
| `GTLibc::HotKeysDown(vector<int>)` (UB on empty) | Caller validates; no library method that can deref empty.     |
| `GTLibc::ReadCheatTable(file, n)`                | `CheatTableParser::parse_file(file)` / `parse_string(xml)`   |
| `GTLibc::ActivateCheatTableEntries(indices)`     | `AgentService::handle("apply", ...)` per entry               |
| `g_GTLibc` global                                | Construct a `MemorySession`; pass into the helpers.          |
| `g_CheatTable` global                            | Returned by `CheatTableParser::parse_*`; caller owns it.    |
| Detached `std::thread` freeze                    | `FreezeManager` with joinable, cancellable workers           |
| `ShellExec` / `popen`                            | (gone) — no shell execution in the core                       |
| `DWORD` `gameBaseAddress`                        | `TargetIdentity::image_path` + `MemorySession::identity()`    |
| `ShowError` / `ShowInfo` blocking MessageBox     | Structured `Error` returned to caller; no blocking dialogs   |
| `CheatEngineTableParser` regex                   | `CheatTableParser` XML, versioned CE 7.x subset               |

## Mapping every public symbol

### `GTLibc::ReadAddress<T>(DWORD)` → `MemorySession::read<T>(Address)`

```cpp
// legacy
auto v = trainer.ReadAddress<uint32_t>(0x1000);
if (v == 0) { /* failure or zero? indistinguishable */ }

// new
auto r = session.read<std::uint32_t>(0x1000);
if (!r) {
    log_failure(r.error());          // r.error() has code, address, message
} else {
    use(r.value());
}
```

`read<T>` requires `T` to be trivially copyable and not a pointer
type. For raw pointers, use `read<std::uintptr_t>` (the value is
the same width as a pointer on the recorded architecture).

### `GTLibc::WriteAddress<T>(DWORD, T)` → `MemorySession::write<T>(Address, T)`

```cpp
// legacy
bool ok = trainer.WriteAddress<uint32_t>(0x1000, 0xCAFEF00D);

// new
auto w = session.write<std::uint32_t>(0x1000, 0xCAFEF00D);
if (!w) {
    // w.error().code is write_failed, partial_write, or
    // invalid_address / invalid_size. The Error carries the
    // operation name "MemorySession::write".
}
```

The new write returns the actual number of bytes written on
success, not just `true`/`false`. To detect partial writes, inspect
`w.value()` against the expected size (`sizeof(T)`).

### `GTLibc::ReadAddressOffsets<T>(DWORD, vector<DWORD>)` (always returns T{})

```cpp
// legacy — always returned T{}; you had no way to know it failed
auto v = trainer.ReadAddressOffsets<uint32_t>(0x1000, {0, 4, 8});

// new — returns a BatchResult naming completed and failed addresses
auto b = session.read_offsets(0x1000, {0, 4, 8}, sizeof(uint32_t));
if (!b) return 1;
if (!b.value().complete) {
    // b.value().failure has the failed address and the precise reason
}
```

`write_offsets<T>` is the corresponding write entry point:

```cpp
auto b = session.write_offsets<uint32_t>(0x1000, {0, 4, 8}, 0xAA);
if (!b.ok() || !b.value().complete) {
    // b.value().completed_addresses tells you which ones succeeded
}
```

### `GTLibc::ReadString(DWORD, size)` / `WriteString(DWORD, string)`

The legacy `ReadString` allocated a `std::string` of `size` bytes
and copied into it; an over-long string would be silently
truncated or, worse, cause a buffer overrun on the write side.

The new API uses `BoundedString` with explicit capacity:

```cpp
auto r = session.read_string(0x5000, 256, /*require_nul=*/true);
if (!r) {
    // r.error().code is invalid_string if the data is not NUL-terminated
}
std::string_view text(reinterpret_cast<const char*>(r.value().bytes().data()),
                      r.value().size());

auto w = session.write_string(0x5000, r.value(), /*target_capacity=*/256);
if (!w || w.value() != r.value().size() + 1) {
    // partial write or capacity violation
}
```

### `GTLibc::HotKeysDown(vector<int>)` (UB on empty list)

The legacy `HotKeysDown` dereferenced `keys[0]` without checking
size, so a filtered entry with no hotkeys crashed. The new
freezing pipeline (preview → freeze) refuses to start a freeze
without a non-empty `id`, and the trainer / UI layer is
responsible for validating the hotkey list before calling the
agent. `FreezeManager::freeze` returns
`ErrorCode::invalid_entry_id` for an empty id, never undefined
behaviour.

### `GTLibc::ReadCheatTable` (regex) → `CheatTableParser::parse_*` (XML)

The legacy `ReadCheatTable` matched patterns with `<regex>`. The
new parser is XML-driven, accepts both the wrapped and direct
forms (see [PARSER.md](PARSER.md)), and refuses to execute Auto
Assembler / LoadLibrary / ShellExecute / LuaScript / raw byte
entries.

```cpp
// legacy
auto table = trainer.ReadCheatTable("IGI.CT", -1);
for (size_t i = 0; i < table.entries().size(); ++i) {
    trainer.ActivateCheatTableEntries({static_cast<int>(i)});
}

// new
gtlibcpp::CheatTableParser parser;
auto r = parser.parse_file("IGI.CT");
if (!r) {
    // r.error().code is parse_failed (malformed XML) or
    // nothing — unsupported entries are reported inside
    // r.value().unsupported_entries, not as a top-level error.
}
for (const auto& e : r.value().entries) {
    // preview + apply via AgentService (see [AGENT.md](AGENT.md))
}
for (const auto& e : r.value().unsupported_entries) {
    log("skipped: " + e.id.value + " — " + e.failure_reason);
}
```

### `g_GTLibc` global → `MemorySession` per target

The legacy `g_GTLibc` global made the library non-reentrant. The
new core has no globals; every operation goes through a
`MemorySession` that owns the backend shared_ptr. Two sessions
can be alive at once (e.g. one for a game, one for a debugger
attach), and they do not share state. `g_GTLibc` is not defined.

### `ShellExec` / `popen` (gone)

The new core has no `ShellExec` or `popen`. The agent service
intentionally refuses to start external processes, load
libraries, or execute shell commands. Anything that needed that
in the legacy trainer (e.g. downloading updated `.ct` files,
auto-updating) is out of scope and must be handled by a
**separate** process that talks to the agent over the JSON-RPC
surface.

### Blocking `MessageBox` (gone)

`ShowError` / `ShowInfo` in the legacy code called
`MessageBoxA` from any thread, including the freeze worker. The
new core emits a structured `Error` and (optionally) a log event
via the injected sink; the UI layer is responsible for showing
the error to the user.

## Behavioural differences

| Topic                  | Legacy                                | `gtlibcpp::*`                                    |
|------------------------|---------------------------------------|--------------------------------------------------|
| Read failure           | Returned `T{}` (looks like 0)         | Returns `Result<T>::failure(Error)`              |
| Write failure          | Returned `false`                       | Returns `Result<size_t>::failure(Error)`        |
| Multi-offset read      | Returned `T{}` on any failure         | Returns `BatchResult` with the failed address    |
| Hotkey list            | UB on empty list                      | Validated by the trainer before calling the API |
| Pointer chain          | `DWORD` arithmetic, truncates on x64  | `std::uintptr_t` per the target architecture    |
| Identity               | `DWORD` start time, no SHA-256        | `FILETIME` start time + BCrypt SHA-256 image    |
| Freeze worker          | `std::thread::detach`                  | `FreezeManager` joins on destruction             |
| Freeze success check   | `!WriteProcessMemory && bytesWritten == size` | `success && bytesWritten == size`; checks `success` first |
| Auto Assembler         | Executed (regex) or skipped silently  | Refused; entry goes to `unsupported_entries`    |
| Shell exec / DLL load  | Possible via `ShellExec`              | Not available; entries fail closed              |
| Blocking dialogs       | `ShowError` MessageBox                | Structured `Error` returned                      |
| Globals                | `g_GTLibc`, `g_CheatTable`            | None                                              |

## Removal timeline

The legacy `GTLibc.*` files are scheduled for removal in 2.0.0.
Until then, demos and trainers built against the legacy API
continue to compile when `GTLIBCPP_BUILD_DEMOS=ON`. New code
should target the `gtlibcpp::*` core and link against
`gtlibcpp::gtlibcpp_core` (cross-platform) and
`gtlibcpp::gtlibcpp_win32` (Windows-only).
