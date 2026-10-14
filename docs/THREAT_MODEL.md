# Threat model

GTLibCpp is a memory operations library intended for **authorised
offline use only**. The library does not, on its own, gain access
to any process; the caller must explicitly open a target with
`OpenProcess` (or the equivalent on another platform) and bind a
`MemorySession` to that handle. The agent service is local-only by
default and never opens a network listener.

This document describes the threats the production baseline
explicitly addresses, the assumptions we rely on, and the threats
that remain out of scope and must be addressed in a later issue.

## In scope

* **A bug in the legacy library causes the agent to report
  success for a failed read or a partial write.** The new code
  never turns a failed read into a default value. `Result<T>::value()`
  aborts on error; the intended pattern is `if (auto r = op(); r) {} else {}`.
* **A freeze worker outlives the `MemorySession` that started
  it.** The new workers are owned by the `FreezeManager` and
  join before destruction. Process liveness is polled, and the
  worker exits when the target is dead.
* **The agent is allowed to mutate a target it was not
  authorised for.** The policy layer matches every mutation
  request against a declared `TargetManifest` (image path, image
  hash, architecture) and refuses anything else. The `WindowsBackend`
  computes the SHA-256 image hash via `BCrypt` and the start
  time via `GetProcessTimes`, so the manifest match is real.
* **A caller tampers with a Cheat Engine `.ct` file to inject
  Auto Assembler, LoadLibrary, ShellExecute, LuaScript, or raw
  byte-array entries.** The parser fails closed on those entry
  kinds and reports them in `unsupported_entries` with a
  `failure_reason`. The agent refuses to act on them.
* **A caller sends a malformed JSON request.** The JSON decoder
  returns `-32700` with a `parse error` message; the agent
  surfaces this to the client without crashing.
* **An unauthorised user pulls the kill switch.** Once
  engaged, every `authorize` call returns
  `ErrorCode::policy_denied`; the operator must explicitly
  release the switch to resume work.

## Assumptions

* The caller is running on a host that is not hostile. The
  library does not defend against a kernel-level attacker, a
  compromised OS, or a debugger attached to its own process.
* The deployment environment blocks unauthorised access to
  the process. Production should not grant
  `PROCESS_CREATE_THREAD` or `PROCESS_VM_WRITE` unless the
  manifest explicitly requires it.
* The audit log is durable. The library writes through an
  injected sink; the production deployment is responsible for
  forwarding events to a tamper-evident store.
* The user is the owner of the target process, or has explicit
  permission to debug or mod the target. See `SECURITY.md` for
  the authorised-use policy.

## Out of scope (tracked separately)

* **Cross-platform backends.** The Windows backend ships in
  `gtlibcpp::gtlibcpp_win32`; a Linux backend
  (`process_vm_readv` / `process_vm_writev`) would require its
  own review.
* **Remote / network control.** The agent is local-only.
  Remote control is gated behind a separate review and a
  separate threat model.
* **Auto Assembler execution.** Entries that contain
  `<AssemblerScript>` are rejected with `parse_unsupported`; a
  future, separately reviewed subsystem would be required to
  support them.
* **Pattern / signature scanning and versioned target
  profiles.** Tracked under the deferred enhancements issue.
* **Tamper resistance against a malicious co-resident
  process.** Driver-level mitigations (e.g. Hyperguard,
  PatchGuard) are outside the library's threat surface.
* **Online / multiplayer cheating.** The library refuses to
  load or execute Auto Assembler, LoadLibrary, ShellExecute,
  LuaScript, and raw byte-array entries. The remaining
  read-only / single-player use cases are not the threat the
  library is designed to prevent; see `SECURITY.md` for the
  authorised-use policy.
