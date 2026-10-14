# AI agent integration: MCP and JSON-RPC trainer

GTLibCpp now includes a **read-only trainer gateway** at
`examples/agent_trainer.cpp`. It exposes the same `AgentService` backend
through two external interfaces:

- **MCP over stdio (default):** `initialize`, `ping`, `tools/list`, and
  `tools/call` using newline-delimited JSON-RPC 2.0 messages.
- **Direct JSON-RPC:** `--json-rpc` exposes `inspect`, `read`,
  `resolve`, and `status` with the established `AgentService` payloads.

Both run against the included `SimulatedGameProcess` by default. On Windows,
`--pid=NUMBER --arch=x86|x64 --image-path=PATH` can connect in
**read-only** mode to a target you are authorized to inspect. The example
intentionally does not expose writes, freeze, apply, restore, or kill-switch
controls while mutation-policy review findings remain unresolved.

## Build

```sh
cmake -S . -B build -DGTLIBCPP_BUILD_EXAMPLES=ON -DGTLIBCPP_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

The executable is `build/examples/gtlibcpp_agent_trainer` on most
single-configuration generators and may be under `build/examples/Release/`
(with `.exe`) on Windows multi-configuration generators.

## Run a trainer through MCP

```sh
python3 examples/agent_trainer_client.py --exe build/examples/gtlibcpp_agent_trainer
```

This client initializes MCP, lists tools, inspects the simulated game, and
reads the health value (100) at address `0x300000010`. The MCP server
advertises:

| Tool | Purpose |
| --- | --- |
| `gt_inspect` | Inspect the attached game's identity |
| `gt_read` | Read 1–256 bytes at a validated integer address |
| `gt_resolve` | Resolve a pointer chain of up to 16 integer offsets |
| `gt_status` | Inspect registered freeze status without mutation |

For a coding agent that supports stdio MCP, configure the command to point
to `gtlibcpp_agent_trainer`; no API key is necessary for local simulated use.
For example, in clients accepting MCP server configuration:

```json
{
  "mcpServers": {
    "gtlibcpp": {
      "command": "/absolute/path/to/gtlibcpp_agent_trainer",
      "args": []
    }
  }
}
```

The agent can discover and call the tools automatically after connecting.
MCP availability and config file locations vary by client.

## Run a trainer through direct JSON-RPC

```sh
python3 examples/agent_trainer_client.py --exe build/examples/gtlibcpp_agent_trainer --mode json-rpc
```

A direct request is one line of JSON followed by a newline:

```json
{"jsonrpc":"2.0","id":"1","method":"read","params":{"address":12884901904,"size":4}}
```

Replace the example address above with **12884901904** (decimal
`0x300000010`) when sending a real request; addresses must be JSON
integers, not quoted hexadecimal strings.

## Safety and limitations

This is a practical **agent-compatible trainer example**, not a remote MCP
service or a completed authenticated mutation API. It starts one process per
client, uses stdio rather than network transport, and does not give external
agents unrestricted write access. Review and resolve all outstanding PR
security findings before implementing an opt-in mutation gateway. For
production use, add process allowlists, target hash verification,
explicit per-operation approvals, payload binding, audit logging, and Windows
named-pipe lifecycle regression tests.
