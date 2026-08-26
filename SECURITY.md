# Security policy

GTLibCpp is a memory operations library intended for **authorised
offline use only**. The library does not, on its own, gain access
to any process; the caller must explicitly open a target with
`OpenProcess` (or the equivalent on another platform) and bind a
`MemorySession` to that handle.

## Use cases the library is designed for

* Local, single-user, single-machine trainers for software the
  user owns or has explicit permission to debug or mod.
* Local automation agents that operate on a target the operator
  has pre-registered in a target manifest, with capability tokens
  scoped to a specific operation and a short TTL.
* Educational / research trainers used in controlled environments
  with code the user has the right to inspect.

## Use cases the library is explicitly not designed for

* Cheating in online multiplayer games. The library does not
  contain any anti-detection, network evasion, or kernel-level
  bypass, and adding any of those would be a separate, larger
  threat-model discussion.
* Operating on a process without the consent of its owner.
* Unattended remote control. The agent transport is local-only
  by default; remote control is out of scope and would require
  its own review.
* Executing arbitrary Auto Assembler, shell commands, or DLL
  loads. The parser fails closed on those entry kinds.

## Reporting a vulnerability

Please open a private security advisory on GitHub
(https://github.com/heaven-hm/gtlibcpp/security/advisories/new)
or contact the maintainer through the email address in the
repository profile. Do not file public issues for suspected
vulnerabilities until a fix is available.

## Supported versions

The `develop` branch and the latest tagged release receive
security updates. Earlier tags and the `master` branch as it
existed before the production-baseline issue (#1) are not
maintained.
