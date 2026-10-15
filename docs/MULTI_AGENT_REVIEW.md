# Multi-Agent PR Review Protocol & Quality Gates

This document defines the automated multi-agent adversarial review gates and release criteria for `gtlibcpp`.

---

## 1. Core Principles

1. **Adversarial Roles Split:**
   - **Critique Specialist:** Focuses on uncovering subtle edge cases, buffer overflows, pointer wrap-around vulnerabilities, ABI contract mismatches, and race conditions.
   - **Validation Specialist:** Focuses on deterministic reproducibility, regression test coverage, sanitizer runs (ASan/UBSan), and cross-platform build matrix verification.
   - **Release Gatekeeper:** Enforces policy sign-off: minimum review depth, deduplicated finding logs, and mandatory human sign-off before default branch merges.

2. **Evidence Before Assertions:**
   - Every critique must include a concrete failure scenario or mathematical proof of overflow/underflow.
   - Unverified stages or manufactured passes are strictly forbidden.

3. **Deterministic CI Supremacy:**
   - AI code review is advisory. Deterministic CI build and test matrix passes across Windows and POSIX targets are required hard gates.

---

## 2. Review Dimensions & Checklist

### A. Memory Safety & Pointer Arithmetic
- [ ] Address addition with offsets checked for overflow (`UINT64_MAX - base < offset`).
- [ ] Address subtraction with negative offsets checked for underflow (`offset > base`).
- [ ] Read and write buffers validated against safety caps (1 MiB ceiling).
- [ ] Zero-length and null-buffer operations rejected fail-closed.

### B. Concurrency & Lifetime
- [ ] Background thread pools (e.g. `FreezeManager`) synchronize joins with active mutexes.
- [ ] Worker loops respond promptly to cancellation without deadlocking.

### C. Error & Wire Contracts
- [ ] Strongly typed `ErrorCode` values mapped explicitly across IPC and RPC boundaries.
- [ ] No unauthenticated writes or freeze mutations permitted without explicit policy authorization.

### D. Regression Test Verification
- [ ] Every P0/P1 fix must include a dedicated unit test case in `tests/core_tests.cpp`.
- [ ] All tests run and pass against the exact PR head commit.

---

## 3. Secure Environment Configuration

To run review agents via Claude Code, OpenAI Codex, or custom automation:
1. Store tokens in environment variables (`GITHUB_TOKEN`, `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`).
2. Never hardcode credentials into scripts, test games, or commit messages.
3. Grant minimal necessary token scopes (`repo:status`, `pull_requests:read`, `pull_requests:write`). Automated merge rights to `master` must be restricted to human maintainers.
