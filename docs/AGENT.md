# Agent

`AgentService` is the local-only control plane. It speaks JSON-RPC
2.0 over a dependency-injected `IAgentTransport`. The production
build wires in a Windows named-pipe transport; tests and the
headless CLI use the in-process queue pair.

## Wire format

Every request is a single JSON document framed by the transport:

```
{ "jsonrpc": "2.0", "id": "<request-id>", "method": "<name>", "params": { ... } }
```

Every response is:

```
{ "jsonrpc": "2.0", "id": "<request-id>", "result": { ... } }    // success
{ "jsonrpc": "2.0", "id": "<request-id>", "error": { ... } }      // failure
```

Errors use the JSON-RPC 2.0 error object:

```
{
  "code": 5002,                // -32700 parse, -32600 invalid, -32601 not found, or a gtlibcpp::ErrorCode
  "message": "mutation requires approval",
  "data": "approval_required"  // gtlibcpp::to_string(ErrorCode)
}
```

`code` is a `gtlibcpp::ErrorCode` cast to `int32_t` for any
domain-level error, and the standard `-32700` / `-32600` / `-32601`
for JSON-RPC framing errors. `data` is the string form of the
domain code, so clients can switch on `data` without interpreting
the integer.

## Methods

| Method          | Capability       | Description                                                    |
|-----------------|------------------|----------------------------------------------------------------|
| `inspect`       | (read-only)      | Return target identity, architecture, current capabilities.    |
| `read`          | read             | Read `size` bytes from `address`.                               |
| `resolve`       | read             | Walk a pointer chain starting at `base`.                       |
| `preview`       | (any)            | Mint an approval token for a subsequent `apply`.              |
| `apply`         | (any)            | Execute a previously-preapproved mutation.                    |
| `verify`        | read             | Re-read an address (for post-apply verification).              |
| `restore`       | rollback         | Rewrite the snapshotted original value at a freeze address.   |
| `freeze`        | freeze           | Start a freeze; returns the freeze id.                         |
| `unfreeze`      | unfreeze         | Stop a freeze without restoring.                               |
| `status`        | read             | Return freeze status (single or all).                         |
| `parse`         | parse            | Parse a `.ct` payload; return supported / unsupported / diagnostics. |
| `kill_switch`   | (operator-only)  | Engage or release the kill switch.                            |

### `inspect` (request)

```json
{ "jsonrpc": "2.0", "id": "1", "method": "inspect", "params": {} }
```

```json
{
  "jsonrpc": "2.0",
  "id": "1",
  "result": {
    "target": {
      "pid": 4242,
      "image_path": "C:/game/game.exe",
      "image_sha256": "sha256:abc...",
      "architecture": "x64",
      "start_time": 133456789012345678
    },
    "capabilities": {
      "read": true,
      "write": false,
      "freeze": false,
      "rollback": false
    }
  }
}
```

### `read` (request)

```json
{
  "jsonrpc": "2.0", "id": "2", "method": "read",
  "params": { "address": 4198400, "size": 4 }
}
```

```json
{
  "jsonrpc": "2.0", "id": "2",
  "result": { "address": 4198400, "size": 4, "hex": "11223344" }
}
```

### `resolve` (request)

```json
{
  "jsonrpc": "2.0", "id": "3", "method": "resolve",
  "params": { "base": 4194304, "offsets": [16, 32] }
}
```

```json
{
  "jsonrpc": "2.0", "id": "3",
  "result": { "address": 4200128 }
}
```

### `preview` (request)

```json
{
  "jsonrpc": "2.0", "id": "4", "method": "preview",
  "params": {
    "request_id": "op-1",
    "target_alias": "fixture",
    "capability": "write",
    "address": 4198400,
    "size": 4,
    "expected_current_hash": "sha256:oldvalue",
    "value_repr": "uint32 0x11223344"
  }
}
```

```json
{
  "jsonrpc": "2.0", "id": "4",
  "result": {
    "approval_token": "op-1",
    "issued_at_ms": 1693142400000,
    "expires_at_ms": 1693142700000,
    "target": "fixture"
  }
}
```

The `approval_token` is the `request_id`; the agent reuses it for
the `apply`. The 5-minute TTL is enforced by the policy layer
inside `apply`.

### `apply` (request)

```json
{
  "jsonrpc": "2.0", "id": "5", "method": "apply",
  "params": {
    "request_id": "op-1",
    "target_alias": "fixture",
    "capability": "write",
    "address": 4198400,
    "size": 4,
    "expected_current_hash": "sha256:oldvalue",
    "approved": true,
    "value_hex": "11223344",
    "value_repr": "uint32 0x11223344"
  }
}
```

```json
{
  "jsonrpc": "2.0", "id": "5",
  "result": { "bytes_written": 4, "address": 4198400 }
}
```

`value_hex` is the byte sequence, big-endian, two hex digits per
byte. The policy layer checks that `value_hex.size() == 2 * size`;
otherwise it returns `ErrorCode::invalid_string`.

### `verify` (request)

```json
{
  "jsonrpc": "2.0", "id": "6", "method": "verify",
  "params": { "address": 4198400, "size": 4 }
}
```

```json
{
  "jsonrpc": "2.0", "id": "6",
  "result": { "hex": "11223344" }
}
```

`verify` is read-only; it does not consume a rate-limiter slot and
does not require approval.

### `restore` (request)

```json
{
  "jsonrpc": "2.0", "id": "7", "method": "restore",
  "params": { "freeze_id": "freeze-1" }
}
```

```json
{ "jsonrpc": "2.0", "id": "7", "result": { "restored": true } }
```

`restore` requires `Capability::rollback` and an approved token.
The freeze entry is marked `restored = true` after the original
value is written back.

### `freeze` (request)

```json
{
  "jsonrpc": "2.0", "id": "8", "method": "freeze",
  "params": {
    "id": "freeze-1",
    "address": 4200448,
    "type": "uint32",
    "value": 99,
    "interval_ms": 50
  }
}
```

```json
{ "jsonrpc": "2.0", "id": "8", "result": { "active": true, "id": "freeze-1" } }
```

Accepted `type` strings: `uint8`, `int8`, `uint16`, `int16`,
`uint32`, `int32`, `uint64`, `int64`, `float`, `double`. `value` is
interpreted as an unsigned 64-bit integer; cast to `float` or
`double` when the type requires it.

### `unfreeze` (request)

```json
{
  "jsonrpc": "2.0", "id": "9", "method": "unfreeze",
  "params": { "id": "freeze-1" }
}
```

```json
{ "jsonrpc": "2.0", "id": "9", "result": { "unfrozen": true } }
```

`unfreeze` stops the worker without restoring the original value.
Use `restore` to put the original value back.

### `status` (request)

```json
{
  "jsonrpc": "2.0", "id": "10", "method": "status",
  "params": { "id": "freeze-1" }
}
```

```json
{
  "jsonrpc": "2.0", "id": "10",
  "result": {
    "freeze": {
      "id": "freeze-1",
      "active": true,
      "successful_rewrites": 12,
      "failed_rewrites": 0
    }
  }
}
```

Omit `id` to list every freeze:

```json
{
  "jsonrpc": "2.0", "id": "10",
  "result": {
    "freezes": [
      { "id": "freeze-1", "active": true,
        "successful_rewrites": 12, "failed_rewrites": 0 },
      { "id": "freeze-2", "active": false,
        "successful_rewrites": 0, "failed_rewrites": 1,
        "last_error": "target process exited during freeze" }
    ]
  }
}
```

### `parse` (request)

```json
{
  "jsonrpc": "2.0", "id": "11", "method": "parse",
  "params": { "xml": "<CheatTable>...</CheatTable>" }
}
```

```json
{
  "jsonrpc": "2.0", "id": "11",
  "result": {
    "supported": [
      { "id": "id:56", "description": "Game_Level",
        "address": 0, "type": "uint8", "is_signed": false,
        "hotkeys": [112] }
    ],
    "unsupported": [
      { "id": "id:270", "reason": "Auto Assembler / Lua entries are not supported in this build" }
    ],
    "diagnostics": [
      "entry id:56 has a symbolic address expression: \"IGI.exe\" + 0x139560",
      "entry id:270: Auto Assembler skipped",
      "parsed 1 supported entries, 1 unsupported entries"
    ]
  }
}
```

`parse` is read-only. The agent's higher-level workflow is:
parse a `.ct`, surface the supported entries to the operator, gate
every action through `preview` + `apply` with the manifest.

### `kill_switch` (request)

```json
{
  "jsonrpc": "2.0", "id": "12", "method": "kill_switch",
  "params": { "engage": true, "reason": "operator pulled the lever" }
}
```

```json
{ "jsonrpc": "2.0", "id": "12", "result": { "engaged": true } }
```

Pass `engage: false` to release. While engaged, every `preview`
and `apply` returns `ErrorCode::policy_denied` with `reason`
`"kill switch engaged: <reason>"`.

## Request lifecycle

```
client                                 agent
  │  ──── preview ────►                  │
  │                                       ├─ Policy.authorize → preview OK
  │                                       ├─ mint PendingApproval (5 min TTL)
  │  ◄─── preview result ────             │
  │  (operator approves off-band)          │
  │  ──── apply ──────►                  │
  │                                       ├─ look up PendingApproval
  │                                       ├─ Policy.authorize_with_token → allow
  │                                       ├─ MemorySession.write_bytes
  │                                       ├─ audit "allow"
  │  ◄─── apply result ─────              │
```

The `preview` + `apply` pair is the only path through which a
mutation can reach the target. There is no shortcut.

## Transports

### Named pipe (Windows production)

```
$VSINSTALLDIR\VC\Tools\Llvm\x64\bin\clang-cl.exe
$VSINSTALLDIR\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe
```

```cpp
gtlibcpp::NamedPipeServerOptions opts{};
opts.pipe_name    = "gtlibcpp.agent";
opts.max_instances = 1;
opts.buffer_size  = 64 * 1024;

auto server = gtlibcpp::make_named_pipe_server(opts);
auto client = gtlibcpp::make_named_pipe_client("gtlibcpp.agent");
```

The server is multi-shot: after every successful read it
disconnects, closes, and re-creates the pipe so a second client
can connect. The client retries for up to 5 seconds if the server
is not yet ready.

The framing is `kProtocolMagic` (`"GTPC"`) + 4-byte little-endian
length + payload, all on the same connection. The transport does
not split one request across multiple reads; an `IAgentTransport`
implementation that does must buffer internally.

### In-process (tests + headless agents)

```cpp
auto pair = std::make_shared<gtlibcpp::InProcTransportPair>();
auto client = pair->make_client();
auto server = pair->make_server();  // one per agent
svc.serve(server);
client->send(R"({"jsonrpc":"2.0","id":"1","method":"inspect","params":{}})");
auto reply = client->receive();
```

`InProcTransportPair` is a queue pair; one side is "client", the
other "server". The agent service treats the server side as the
listening endpoint and the client side as the only connected peer.
The pair is closed when either side calls `close()`; the close
propagates to both ends.

## Error code reference

| JSON `code`   | `data`                      | Meaning                                         |
|---------------|------------------------------|-------------------------------------------------|
| `-32700`      | (none)                       | JSON parse error                                |
| `-32600`      | (none)                       | Invalid request                                 |
| `-32601`      | (none)                       | Method not found                                |
| `1001`        | `not_connected`              | No backend / no target                          |
| `1002`        | `target_not_found`           | `OpenProcess` returned null                      |
| `1003`        | `target_arch_mismatch`       | Architecture does not match                     |
| `1004`        | `target_identity_mismatch`   | Identity does not match the manifest             |
| `1005`        | `target_dead`                | Process is no longer alive                      |
| `2001`–`2007` | `invalid_address` / `invalid_size` / `invalid_offsets` / `invalid_hotkey` / `invalid_entry_id` / `invalid_type` / `invalid_string` | Validation failure |
| `3001`–`3005` | `read_failed` / `partial_read` / `write_failed` / `partial_write` / `verification_mismatch` | Memory I/O failure |
| `4001`–`4003` | `parse_failed` / `parse_unsupported` / `parse_diagnostic` | XML parser outcome |
| `5001`–`5004` | `policy_denied` / `approval_required` / `capability_missing` / `rate_limited` | Policy gate |
| `6001`–`6002` | `timeout` / `cancelled`        | Operation timing                                |
| `9001`        | `internal`                   | Caught exception or other unexpected error       |

The `data` field is the stable string form of the code and is
safe for clients to switch on; the integer `code` may change if
new error codes are added.
