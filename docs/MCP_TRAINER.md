# Agent Trainer: MCP and JSON-RPC

This example exposes a **read-only** C++17 GTLibCpp trainer to AI coding agents and conventional JSON-RPC clients. The executable is `gtlibcpp_agent_trainer` (source: `examples/agent_trainer.cpp`). It uses `SimulatedGameProcess`, not an actual game process, so it can be exercised without privileges or a running game.

## Build and test

```bash
cmake -S . -B build -DGTLIBCPP_BUILD_EXAMPLES=ON -DGTLIBCPP_BUILD_TESTS=ON
cmake --build build --config Release --target gtlibcpp_agent_trainer
ctest --test-dir build --output-on-failure -R e2e_gtlibcpp_agent_trainer
```

Depending on the generator, the executable is in `build/examples/` or `build/examples/Release/`. On Windows, the filename has a `.exe` suffix.

## MCP stdio

Launch `gtlibcpp_agent_trainer` without arguments in the MCP client's server configuration. The command reads one JSON-RPC request per stdin line, and writes one response per stdout line; diagnostics go to stderr.

Example MCP client server configuration (replace the path with the built executable):

```json
{
  "mcpServers": {
    "gtlibcpp-trainer": {
      "command": "/absolute/path/to/gtlibcpp_agent_trainer",
      "args": []
    }
  }
}
```

It supports MCP `initialize`, `ping`, `tools/list`, and `tools/call`, negotiating protocol version `2025-03-26`. It also ignores notifications, as required. Available tools are:

| Tool | Arguments | Result |
|---|---|---|
| `gt_inspect` | `{}` | Simulated target identity, architecture |
| `gt_read` | `{"address":12884901904,"size":4}` | Byte values in hex (4 bytes of simulated health at 0x300000010) |
| `gt_resolve` | `{"base":5369757696,"offsets":[0,80]}` | Pointer-chain resolution |
| `gt_status` | `{}` | Read-only freeze status |

An example of a request on stdin after initialization:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"gt_read","arguments":{"address":12884901904,"size":4}}}
```

The MCP tool result contains a text item with the underlying GTLibCpp JSON-RPC reply. The health value is initialized to 100 (`64000000` little-endian). Protocol traffic must not be mixed with other stdout text.

## Read-only attachment to a real Windows process

On Windows, pass an explicit authorized target PID to the same executable:

```powershell
.\build\examples\Release\gtlibcpp_agent_trainer.exe --pid=1234 --arch=x86 --image-path=C:\Games\MyGame\game.exe
```

For a 64-bit process, choose `--arch=x64`. The PID and image path are examples, not auto-discovered values. `--pid` mode opens the process with `PROCESS_VM_READ | PROCESS_QUERY_INFORMATION` only; it does not grant memory writes. The `--image-path` is currently descriptive metadata for the backend, **not an independently verified binding between the PID and executable**. Use this only on processes you own or are authorized to inspect.

To configure an MCP client for a real process, use the same executable and pass `--pid=1234`, `--arch=x86`, and `--image-path=C:\Games\MyGame\game.exe` in the server's `args` array. Both simulated and real-process modes keep the same read-only tool allowlist. There is no network listener.

## Native GTLibCpp JSON-RPC mode

Use `gtlibcpp_agent_trainer --json-rpc` to accept its existing JSON-RPC methods directly, again with one request per input line:

```json
{"jsonrpc":"2.0","id":"health","method":"read","params":{"address":12884901904,"size":4}}
```

Both entry points are intentionally restricted to `inspect`, `read`, `resolve`, and `status`. Address and offset arguments are nonnegative JSON integers; reads must be 1–256 bytes and pointer chains at most 16 offsets. MCP write, apply, preview, freeze, unfreeze, restore and kill-switch operations are **not available** from this gateway. The general `AgentService` mutation handlers are separately restricted and still require further security validation. This gateway does not expose them to agents.

## Future production integration

Real Windows process inspection is available through the optional `--pid` read-only backend. Before enabling any future write-capable MCP tools, independently verify that the PID belongs to the intended executable and enforce explicit operator authorization on every mutation path. Keep the server local with authenticated, access-controlled transports. Add end-to-end security regression tests and Windows CI before enabling MCP mutation operations. Do not directly pass untrusted agent input to write/freeze functions.

This sample is a native C++17 MCP server without a Python/Node runtime. It is intentionally minimal and does not implement HTTP, resources, prompts or dynamic target discovery.

## Mutation protocol note

The general GTLibCpp preview/apply and preview/freeze interfaces require an exact SHA-256 precondition (lowercase hex or `sha256:`-prefixed), a matching preview record, and unchanged target bytes before write. This **does not authenticate** an AI agent or constitute independent operator approval. The MCP trainer deliberately exposes read-only operations even when its backend can inspect a real Windows process.
