# Threat model

## In scope

* A bug in the legacy library causes the agent to report success
  for a failed read or a partial write. The new code never turns
  a failed read into a default value.
* A freeze worker outlives the `MemorySession` that started it.
  The new workers are owned by the `FreezeManager` and join before
  destruction. Process liveness is polled, and the worker exits
  when the target is dead.
* The agent is allowed to mutate a target it was not authorised
  for. The policy layer matches every mutation request against a
  declared `TargetManifest` (image path, image hash, architecture)
  and refuses anything else.
* A caller tampers with a Cheat Engine `.ct` file to inject Auto
  Assembler, LoadLibrary, or ShellExecute entries. The parser
  fails closed on those entry kinds.

## Assumptions

* The caller is running on a host that is not hostile.
* The deployment environment blocks unauthorised access to the
  process.
* The audit log is durable.

## Out of scope (tracked separately)

* Cross-platform backends.
* Remote / network control.
* Auto Assembler execution.
* Pattern / signature scanning and versioned target profiles.
* Tamper resistance against a malicious co-resident process.
