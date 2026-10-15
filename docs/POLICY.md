# Policy

The policy layer is the gate between the agent and the target
process. Every mutation request — every `apply` call — is checked
by `Policy::authorize_with_token` before it is forwarded to
`MemorySession`. A failed authorize returns a `Result<void>` whose
`Error` carries the precise deny reason; the agent service surfaces
that as the JSON-RPC error payload.

## Mental model

```
agent request  ──►  Policy.authorize_with_token
                      │
                      ├─ kill switch engaged?     ─► deny policy_denied
                      ├─ manifest for alias?      ─► deny policy_denied
                      ├─ identity matches?        ─► deny target_identity_mismatch
                      ├─ capability allowed?      ─► deny policy_denied
                      ├─ address and size valid?  ─► deny invalid_*
                      ├─ expected_current_hash?   ─► deny policy_denied
                      ├─ preview?                 ─► allow + audit "preview"
                      ├─ approved + valid token?  ─► deny approval_required / policy_denied
                      ├─ rate limiter slot?       ─► deny rate_limited
                      └─► allow + audit "allow"
```

Every successful `preview` mints a `PendingApproval` in the
`AgentService`, valid for 5 minutes. The corresponding `apply` must
echo the `request_id` and present an `ApprovalToken` whose
`request_id`, `target_alias`, and TTL window match the pending
entry. The token's signature field is opaque to the policy layer
itself; the agent is responsible for verifying it before minting.

## Manifest format

```cpp
gtlibcpp::TargetManifest{
    "fixture",                                  // alias
    "C:/fixtures/fixture.exe",                 // image_path
    "sha256:abc123...",                         // image_sha256 ("" = unchecked)
    "x64",                                      // architecture ("x86" / "x64" / "" = unchecked)
    true,                                       // allow_write
};
```

`manifest.matches(identity)` returns `true` when every non-empty
field equals the corresponding `TargetIdentity` field. So the
manifest `{"", "C:/game.exe", "sha256:abc", "x64", false}` matches
any process whose `image_path` is `C:/game.exe`, whose image hash
is `sha256:abc`, and whose architecture is `x64`, regardless of pid
or start time.

`Policy::manifests()` returns the list of declared manifests, in
declaration order.

## Capability table

| Capability     | Bit                | Required for                                  |
|----------------|--------------------|-----------------------------------------------|
| `read`         | `1u << 0`          | every read (always granted on connect)        |
| `write`        | `1u << 1`          | `apply` that writes a typed value             |
| `freeze`       | `1u << 2`          | `apply` that starts a freeze                  |
| `unfreeze`     | `1u << 3`          | `apply` that stops a freeze                   |
| `rollback`     | `1u << 4`          | `apply` that restores a snapshotted value     |
| `parse`        | `1u << 5`          | agent-side `.ct` parsing                       |

`Policy` rejects any capability not in this table with
`ErrorCode::capability_missing`. `read` is always implicitly
granted; the manifest's `allow_write` flag controls whether
`write` / `freeze` / `unfreeze` / `rollback` are accepted.

## Audit event format

```cpp
gtlibcpp::AuditEvent{
    "op-1",                            // request_id
    "fixture",                         // alias
    4242,                              // pid
    "uint32 0x11223344",                // operation (value_repr)
    "write",                           // capability
    "allow",                           // decision: "allow" / "deny" / "preview"
    "approved",                        // reason
    now_ms(),                          // timestamp_ms
};
```

`decision` is one of:

* `"preview"` — preview succeeded; an `apply` is now possible.
* `"allow"` — mutation authorized and applied.
* `"deny"` — request refused; see `reason` for the precise cause.

`reason` is the human-readable string passed to the `Error` of the
denied call, e.g. `"no manifest for target alias fixture"`,
`"manifest does not grant write"`, `"expected_current_hash is
required for every mutation"`.

The audit sink is a `void (*)(const AuditEvent&, void* user)`
function pointer. The agent service registers a sink that forwards
to its structured log; tests register a sink that pushes events
into a `std::vector<AuditEvent>` for inspection. If no sink is
registered, audit events are dropped on the floor (the library
does not log to stderr by default).

## Approval tokens

```cpp
gtlibcpp::ApprovalToken{
    "op-1",                            // request_id (must match the apply)
    "fixture",                         // target_alias (must match the apply)
    now_ms(),                          // issued_at_ms
    now_ms() + 5 * 60 * 1000,           // expires_at_ms
    "signature opaque to the policy",  // verified by the agent layer
};
```

The agent service mints a `PendingApproval` on every successful
`preview`, valid for 5 minutes. The `apply` request must echo
`request_id` and `target_alias`, and present the token. The policy
layer checks:

* `token->request_id == request.request_id`
* `token->target_alias == request.target_alias`
* `now_ms()` in `[token->issued_at_ms, token->expires_at_ms]`

The signature is opaque to the policy layer; the agent must
verify it (e.g. HMAC over the canonical request) before minting.
A `signature` value of `""` is accepted by the policy layer
**only** in tests; production agents must populate it.

## Rate limiter

`Policy::rate_limiter()` returns a per-target sliding-window
limiter. The default is 120 mutations per minute per alias
(configurable via the `Policy` constructor). Every successful
mutation consumes a slot. Excess requests return
`ErrorCode::rate_limited`. The window is per `alias`, not global,
so a workload that legitimately touches many targets does not get
rate-limited.

## Kill switch

`Policy::kill_switch()` is an atomic, thread-safe toggle. When
engaged, every `authorize` call returns `ErrorCode::policy_denied`
with `reason` `"kill switch engaged: <reason>"`. The agent
service exposes the toggle as the `kill_switch` JSON-RPC method.
The kill switch does **not** drain pending previews; the operator
must `release` it explicitly to resume work.

## Decision record

Every authorize call emits one audit event:

| Path                                    | `decision` | `reason`                                          |
|-----------------------------------------|------------|--------------------------------------------------|
| kill switch engaged                      | `"deny"`   | `kill switch engaged: <reason>`                  |
| no manifest for alias                   | `"deny"`   | `no manifest for target alias <alias>`           |
| identity does not match                 | `"deny"`   | `target identity does not match the authorised manifest` |
| capability not granted                  | `"deny"`   | `manifest does not grant write`                  |
| unknown capability                      | `"deny"`   | `unknown capability requested`                   |
| address 0 / size 0 / size > 1 MiB        | `"deny"`   | `request has no concrete address` / `request size is out of bounds` |
| `expected_current_hash` empty           | `"deny"`   | `expected_current_hash is required for every mutation` |
| preview                                 | `"preview"`| `preview requested`                             |
| `!approved`                             | `"deny"`   | `mutation requires approval`                     |
| token mismatch / expired                | `"deny"`   | `approval token request_id does not match` / `approval token alias does not match` / `approval token is not within its TTL window` |
| rate limited                            | `"deny"`   | `rate limit exceeded for target <alias>`          |
| success                                 | `"allow"`  | `approved`                                        |

## Example: full authorize flow

```cpp
gtlibcpp::TargetManifest manifest{
    "fixture", "C:/game/game.exe", "sha256:abc", "x64", true
};
gtlibcpp::Policy policy({manifest});

gtlibcpp::TargetIdentity id{
    4242, file_time, "C:/game/game.exe", "sha256:abc", gtlibcpp::Architecture::x64
};

// 1. Preview
gtlibcpp::MutationRequest preview{
    "op-1", "fixture", gtlibcpp::Capability::write,
    true,  // preview
    false, // not approved yet
    "old-value-hash",
    0x1000, 4, "uint32 0x11223344"
};
auto r1 = policy.authorize(id, preview);  // success, audit "preview"

// 2. Apply (with a valid token, after the operator approved)
gtlibcpp::ApprovalToken token{
    "op-1", "fixture", now_ms(), now_ms() + 5*60*1000, "sig"
};
gtlibcpp::MutationRequest apply{
    "op-1", "fixture", gtlibcpp::Capability::write,
    false, true, "old-value-hash", 0x1000, 4, "uint32 0x11223344"
};
auto r2 = policy.authorize_with_token(id, apply, &token);  // success, audit "allow"
```
