# Security policy

GTLibCpp is a memory operations library intended for **authorised
offline use only**. The library does not, on its own, gain access
to any process; the caller must explicitly open a target with
`OpenProcess` (or the equivalent on another platform) and bind a
`MemorySession` to that handle. The agent service is local-only
by default and never opens a network listener.

## Use cases the library is designed for

* Local, single-user, single-machine trainers for software the
  user owns or has explicit permission to debug or mod. The
  library supports both **32-bit (x86) and 64-bit (x64) games**,
  including WOW64 (a 32-bit game running on a 64-bit host).
* Local automation agents that operate on a target the operator
  has pre-registered in a target manifest, with capability tokens
  scoped to a specific operation and a short TTL.

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
  loads. The parser fails closed on those entry kinds; see
  [docs/PARSER.md](docs/PARSER.md) for the full fail-closed list.

## Threat model

See [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md) for the in-scope
threats, the assumptions the library relies on, and the items
that are out of scope and require a separate review.

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the layered
design, supported architectures, and thread-safety guarantees.

## Authorised use

By linking against `gtlibcpp::*` you agree to the following:

* You are the owner of the target process, **or** you have
  explicit permission from the owner to inspect or modify the
  target process.
* The target is a single-player or local-cooperative experience.
  You will not use the library against an online service whose
  terms of service prohibit modification of the running
  process.
* The target is not subject to anti-cheat, DRM, or copy
  protection that the modification would circumvent. The
  library does not attempt to bypass any of these.
* You will not use the library to exfiltrate data from a
  process you do not own, or to interfere with another user's
  session.

## Reporting a vulnerability

Please open a private security advisory on GitHub
(https://github.com/heaven-hm/gtlibcpp/security/advisories/new)
or contact the maintainer through the email address in the
repository profile. **Do not** file public issues for suspected
vulnerabilities until a fix is available.

When reporting, please include:

* The version of `gtlibcpp` (commit SHA or release tag) and the
  platform (OS, architecture).
* A minimal reproducer (`.ct` file, JSON-RPC request, etc.) if
  applicable.
* The expected behaviour vs. the observed behaviour.
* Any related public CVE or advisory.

## Supported versions

The `develop` branch and the latest tagged release receive
security updates. Earlier tags and the `master` branch as it
existed before the production-baseline issue (#1) are not
maintained.

## License

MIT — see [LICENSE](LICENSE).
