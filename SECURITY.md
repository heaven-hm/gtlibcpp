# Security policy

GTLibCpp is a memory operations library intended for **authorised
offline use only**. The library does not, on its own, gain access
to any process; the caller must explicitly open a target with
`OpenProcess` (or the equivalent on another platform) and bind a
`MemorySession` to that handle.

## Use cases the library is designed for

* Local, single-user, single-machine trainers for software the
  user owns or has explicit permission to debug or mod. The
  library supports both **32-bit (x86) and 64-bit (x64) games**,
  including WOW64 (a 32-bit game running on a 64-bit host).
* Local automation agents that operate on a target the operator
  has pre-registered in a target manifest, with capability tokens
  scoped to a specific operation and a short TTL.

## Use cases the library is explicitly not designed for

* Cheating in online multiplayer games.
* Operating on a process without the consent of its owner.
* Unattended remote control. The agent transport is local-only
  by default; remote control is out of scope.
* Executing arbitrary Auto Assembler, shell commands, or DLL
  loads. The parser fails closed on those entry kinds.

## Reporting a vulnerability

Please open a private security advisory on GitHub
(https://github.com/heaven-hm/gtlibcpp/security/advisories/new)
or contact the maintainer through the email address in the
repository profile. Do not file public issues for suspected
vulnerabilities until a fix is available.
