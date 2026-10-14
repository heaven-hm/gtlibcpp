#!/usr/bin/env python3
"""GTLibCpp trainer client: MCP tools or direct JSON-RPC, standard library only.

Build: cmake -S . -B build && cmake --build build --config Release
Run: python3 examples/agent_trainer_client.py --exe build/examples/gtlibcpp_agent_trainer
     python3 examples/agent_trainer_client.py --exe build/examples/gtlibcpp_agent_trainer --mode json-rpc
On Windows, supply the path to gtlibcpp_agent_trainer.exe (for example build/examples/Release/gtlibcpp_agent_trainer.exe).
"""
import argparse
import json
import subprocess


def call(server, method, params, request_id):
    message = {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}
    server.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
    server.stdin.flush()
    raw = server.stdout.readline()
    if not raw:
        raise RuntimeError("Trainer server exited before responding")
    result = json.loads(raw)
    if result.get("id") != request_id:
        raise RuntimeError(f"Mismatched response ID: {result}")
    if "error" in result:
        raise RuntimeError(f"{method}: {result['error']}")
    return result["result"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, help="Path to the compiled trainer server")
    parser.add_argument("--mode", choices=("mcp", "json-rpc"), default="mcp")
    parser.add_argument("--pid", type=int, help="Optional authorized Windows target PID (read-only)")
    parser.add_argument("--arch", choices=("x86", "x64"))
    parser.add_argument("--image-path")
    args = parser.parse_args()
    command = [args.exe]
    if args.mode == "json-rpc":
        command.append("--json-rpc")
    if args.pid is not None:
        command.append(f"--pid={args.pid}")
    if args.arch:
        command.append(f"--arch={args.arch}")
    if args.image_path:
        command.append(f"--image-path={args.image_path}")

    with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          text=True, encoding="utf-8") as server:
        if args.mode == "mcp":
            init = call(server, "initialize",
                        {"protocolVersion": "2025-03-26", "capabilities": {},
                         "clientInfo": {"name": "gtlibcpp-trainer-example", "version": "1.0"}},
                        1)
            print("MCP version:", init["protocolVersion"])
            server.stdin.write(json.dumps({"jsonrpc": "2.0",
                                           "method": "notifications/initialized"}) + "\n")
            server.stdin.flush()
            tools = call(server, "tools/list", {}, 2)["tools"]
            print("Available MCP tools:", ", ".join(item["name"] for item in tools))
            for index, (name, arguments) in enumerate((
                ("gt_inspect", {}),
                ("gt_read", {"address": 0x300000010, "size": 4}),
            ), start=3):
                if args.pid is not None and name == "gt_read":
                    print("Skipping example memory read: demo address is not valid for a real PID.")
                    continue
                reply = call(server, "tools/call",
                             {"name": name, "arguments": arguments}, index)
                if reply.get("isError"):
                    raise RuntimeError(reply)
                print(name, reply["content"][0]["text"])
        else:
            print("inspect:", call(server, "inspect", {}, 1))
            if args.pid is None:
                print("read:", call(server, "read",
                                    {"address": 0x300000010, "size": 4}, 2))
            else:
                print("Read-only target attached. Supply a verified address in your own client.")

if __name__ == "__main__":
    main()
