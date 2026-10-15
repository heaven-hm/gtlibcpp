## Summary and risk assessment

Describe what changed, why it changed, and the affected native APIs / call sites.

- Related issue:
- Exact target branch:
- Risk level (P0/P1/P2/P3):
- C/C++ ABI or public API compatibility impact:
- Target process memory read/write/authorization impact:

## Independent review gates

- [ ] **Correctness reviewer:** Confirm behavioral contract, pointer arithmetic, overflow, error propagation, and callers.
- [ ] **Security reviewer:** Confirm process identity, explicit authorization, fail-closed behavior, input bounds, and logging without secrets.
- [ ] **Windows native/ABI reviewer:** Check x86/x64 layouts, pointer widths, calling conventions, lifetimes, and races.
- [ ] **Tests reviewer:** Check negative regression tests, tests for partial failures, and CI matrix coverage at this exact head SHA.
- [ ] All P0/P1 findings in changed code are fixed with a regression test or explicitly documented as blocking.
- [ ] Every inline review thread is addressed based on code evidence (not simply marked resolved).
- [ ] Latest-head CI passes, including Windows x64 MSVC, x64 clang-cl and x86 MSVC.
- [ ] Changes to legacy APIs include migration notes and tests for old callers.

## Test evidence

Paste the exact commit SHA, relevant commands/results, and links to CI runs:

| Check | Outcome / evidence |
| --- | --- |
| Correctness | |
| Security | |
| ABI/concurrency | |
| Regression tests | |
| Windows CI (latest head) | |

## Merge gate

**Do not merge** on green CI alone. The author must describe remaining uncertainties; no P0/P1 finding in the PR's changed functionality can remain unresolved. A reviewer or maintainer must explicitly approve merging to `develop` or `master`. Bot/AI reviews are advisory and **must not count as independent human reviewer approval**. Do not auto-merge on AI review output alone.
