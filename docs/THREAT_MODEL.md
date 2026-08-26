# Threat model

This document describes the threats the production baseline
explicitly addresses, the assumptions we rely on, and the threats
that remain out of scope and must be addressed in a later issue.

## In scope

* A bug in the legacy library causes the agent to report success
  for a failed read or a partial write. The new code never turns
  a failed read into a default value; every operation returns
  structured success or failure.
* A freeze worker outlives the `MemorySession` that started it
  and uses a process handle that has already been closed. The
  new workers are owned by the `FreezeManager` and join before
  destruction. Process liveness is polled, and the worker exits
  when the target is dead.
* The agent is allowed to mutate a target it was not authorised
  for. The policy layer matches every mutation request against a
  declared `TargetManifest` (image path, image hash, architecture)
  and refuses anything else. The kill switch is honoured even
  after a successful preview.
* A caller tampers with a Cheat Engine `.ct` file to inject Auto
  Assembler, LoadLibrary, or ShellExecute entries. The parser
  fails closed on those entry kinds; the agent refuses to
  execute them, and they are reported in the diagnostics so the
  operator can audit the rejection.
* A test suite silently regresses the freeze lifecycle. The
  `FreezeManager` is exercised in `tests/core_tests.cpp` for
  both happy path and process-exit cancellation.

## Assumptions

* The caller is running on a host that is not hostile. The
  library does not defend against a kernel-level attacker, a
  compromised OS, or a debugger attached to its own process.
* The deployment environment blocks unauthorised access to the
  process. `OpenProcess` is called with the minimum rights the
  operation needs; production should not grant
  `PROCESS_CREATE_THREAD` or `PROCESS_VM_WRITE` unless the
  manifest explicitly requires it.
* The audit log is durable. The library writes through an
  injected sink; the production deployment is responsible for
  forwarding events to a tamper-evident store.

## Out of scope (tracked separately)

* Cross-platform backends. The Windows backend ships in
  `gtlibcpp::win32`; a Linux backend (process_vm_readv /
  process_vm_writev) would require its own review.
* Remote / network control. The agent is local-only. Remote
  control is gated behind a separate review and a separate
  threat model.
* Auto Assembler execution. Entries that contain
  `<AssemblerScript>` are rejected with `parse_unsupported`; a
  future, separately reviewed subsystem would be required to
  support them.
* Pattern / signature scanning and versioned target profiles.
  These are tracked under the "deferred enhancements" issue.
* Tamper resistance against a malicious co-resident process.
  Driver-level mitigations (e.g. Hyperguard, PatchGuard) are
  outside the library's threat surface.
