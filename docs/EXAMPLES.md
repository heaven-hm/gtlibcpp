# Examples

All examples assume the consumer project is linked against
`gtlibcpp::gtlibcpp_core` (cross-platform) and, on Windows,
`gtlibcpp::gtlibcpp_win32` (Windows-only backend + named-pipe
transport).

## 1. Read a 4-byte scalar from a known address (Windows)

```cpp
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/result.hpp"
#include "gtlibcpp/windows_backend.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>

int main() {
    gtlibcpp::WindowsBackendOptions opts{};
    opts.pid          = 1234;
    opts.image_path   = "C:/path/to/authorised/target.exe";
    opts.architecture = gtlibcpp::Architecture::x64;

    auto backend  = gtlibcpp::make_windows_backend(opts);
    if (!backend) {
        std::fprintf(stderr, "could not open process\n");
        return 1;
    }
    gtlibcpp::MemorySession session(backend);
    const auto read = session.read<std::uint32_t>(0x401000);
    if (!read) {
        std::fprintf(stderr, "read failed: %s (op=%s addr=0x%llx)\n",
                     read.error().message.c_str(),
                     read.error().operation.c_str(),
                     static_cast<unsigned long long>(read.error().address));
        return 2;
    }
    std::printf("value: 0x%08x\n", read.value());
    return 0;
}
```

## 2. Compare-before-write (no agent, in-process)

```cpp
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/result.hpp"

#include <cstdint>
#include <memory>

class FakeBackend : public gtlibcpp::IMemoryBackend {
public:
    gtlibcpp::Result<std::vector<std::uint8_t>>
    read(gtlibcpp::Address a, std::size_t n) override {
        std::vector<std::uint8_t> out(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            auto it = mem.find(a + i);
            if (it != mem.end()) out[i] = it->second;
        }
        return gtlibcpp::Result<std::vector<std::uint8_t>>::success(std::move(out));
    }
    gtlibcpp::Result<std::size_t>
    write(gtlibcpp::Address a, const std::vector<std::uint8_t>& b) override {
        for (std::size_t i = 0; i < b.size(); ++i) mem[a + i] = b[i];
        return gtlibcpp::Result<std::size_t>::success(b.size());
    }
    bool is_alive() noexcept override { return true; }
    std::string target_id() const override { return "fake"; }
    gtlibcpp::TargetIdentity identity() const override { return id_; }
    gtlibcpp::TargetIdentity id_{1, 1, "x", "", gtlibcpp::Architecture::x64};
    std::map<gtlibcpp::Address, std::uint8_t> mem;
};

int main() {
    auto backend = std::make_shared<FakeBackend>();
    const std::uint32_t original = 0xDEADBEEF;
    backend->mem[0x1000] = (original      ) & 0xFF;
    backend->mem[0x1001] = (original >>  8) & 0xFF;
    backend->mem[0x1002] = (original >> 16) & 0xFF;
    backend->mem[0x1003] = (original >> 24) & 0xFF;

    gtlibcpp::MemorySession session(backend);
    auto r = session.compare_write_verify<std::uint32_t>(0x1000, 0xCAFEF00D);
    if (!r) {
        std::fprintf(stderr, "verify failed: %s\n", r.error().message.c_str());
        return 1;
    }
    const auto after = session.read<std::uint32_t>(0x1000);
    if (!after || after.value() != 0xCAFEF00D) return 2;
    return 0;
}
```

## 3. Freeze a value and restore it

```cpp
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/memory_session.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

int main(/* ... a MemorySession `session` ... */) {
    gtlibcpp::FreezeManager freeze(session);

    gtlibcpp::FreezeRequest fr;
    fr.id        = "health-100";
    fr.address   = 0x401000;
    fr.type_name = "uint32";
    fr.value_u64 = 9999;
    fr.interval  = std::chrono::milliseconds(50);

    auto f = freeze.freeze(fr);
    if (!f) return 1;  // could not start freeze

    // ... time passes; the worker rewrites 9999 every 50 ms ...

    // Restore the original value (whatever was there at freeze time).
    auto r = freeze.restore("health-100");
    if (!r) return 2;
    return 0;
}
```

## 4. Parse a `.ct` file and report supported / unsupported

```cpp
#include "gtlibcpp/parser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <file.ct>\n";
        return 1;
    }
    std::ifstream f(argv[1]);
    if (!f) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }
    std::ostringstream ss; ss << f.rdbuf();

    gtlibcpp::CheatTableParser parser;
    auto r = parser.parse_string(ss.str());
    if (!r) {
        std::cerr << "parse failed: " << r.error().message << "\n";
        return 1;
    }
    std::cout << "supported: " << r.value().entries.size() << "\n";
    for (const auto& e : r.value().entries) {
        std::cout << "  - " << e.id.value
                  << "  desc=\"" << e.description << "\""
                  << "  addr=0x" << std::hex << e.address << std::dec
                  << "  type=" << gtlibcpp::CheatTableParser::to_string(e.type)
                  << "  hotkeys=" << e.hotkeys.size() << "\n";
    }
    std::cout << "unsupported: " << r.value().unsupported_entries.size() << "\n";
    for (const auto& e : r.value().unsupported_entries) {
        std::cout << "  - " << e.id.value << ": " << e.failure_reason << "\n";
    }
    for (const auto& d : r.value().diagnostics) {
        std::cout << "  * " << d << "\n";
    }
    return 0;
}
```

## 5. Wire up an in-process agent and run a JSON-RPC cycle

```cpp
#include "gtlibcpp/agent.hpp"
#include "gtlibcpp/freeze.hpp"
#include "gtlibcpp/inproc_transport.hpp"
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/parser.hpp"
#include "gtlibcpp/policy.hpp"

#include <iostream>
#include <memory>

int main() {
    // 1. Construct the stack.
    auto backend = std::make_shared<MyWindowsOrFakeBackend>();  // user-supplied
    auto session = std::make_shared<gtlibcpp::MemorySession>(backend);
    auto parser  = std::make_shared<gtlibcpp::CheatTableParser>();
    auto freeze  = std::make_shared<gtlibcpp::FreezeManager>(session);

    gtlibcpp::TargetManifest manifest{
        "fixture", "C:/game/game.exe", "sha256:abc", "x64", true
    };
    auto policy = std::make_shared<gtlibcpp::Policy>(
        std::vector<gtlibcpp::TargetManifest>{manifest});

    gtlibcpp::AgentService agent(session, policy, freeze, parser);

    // 2. Run a single request/response cycle.
    auto reply = agent.handle(
        R"({"jsonrpc":"2.0","id":"1","method":"inspect","params":{}})");
    if (reply) std::cout << reply.value() << "\n";

    // 3. Or run a long-lived server on an in-process transport.
    auto pair = std::make_shared<gtlibcpp::InProcTransportPair>();
    agent.serve(pair->make_server());
    auto client = pair->make_client();
    client->send(R"({"jsonrpc":"2.0","id":"2","method":"status","params":{}})");
    auto r = client->receive();
    if (r) std::cout << r.value() << "\n";
    agent.stop();
    return 0;
}
```

## 6. Inject a custom log sink

```cpp
#include "gtlibcpp/log.hpp"

#include <vector>

struct Captured {
    std::vector<gtlibcpp::LogEvent> events;
    static void sink(const gtlibcpp::LogEvent& e, void* user) {
        static_cast<Captured*>(user)->events.push_back(e);
    }
};

int main() {
    Captured cap;
    gtlibcpp::Logger::instance().set_sink(&Captured::sink, &cap);
    gtlibcpp::Logger::instance().set_min_level(gtlibcpp::LogLevel::debug);

    // ... do work; every MemorySession / FreezeManager / Agent
    // operation now writes one log event into `cap.events` ...
    return 0;
}
```

## 7. End-to-end agent: preview, approve, apply (pseudocode)

```
client                                  agent
  │  ── preview ──►                        │
  │     {request_id: "op-1", ...}            │  policy check: ✓
  │  ◄─ {approval_token: "op-1", ...} ────  │
  │                                        │
  │  (operator reviews and signs)           │
  │                                        │
  │  ── apply ──►                           │
  │     {request_id: "op-1",                │
  │      approved: true,                    │
  │      value_hex: "11223344", ...}        │  policy check: ✓
  │                                        │  memory write:  ✓
  │                                        │  audit:         "allow"
  │  ◄─ {bytes_written: 4, ...} ───────     │
```

The operator UI never opens a network connection. The named pipe
is local. The audit log records every preview and every apply.

## 8. CMake consumer

```cmake
# In your project's CMakeLists.txt
find_package(gtlibcpp 1.0 REQUIRED CONFIG)
add_executable(my_trainer main.cpp)
target_link_libraries(my_trainer PRIVATE gtlibcpp::gtlibcpp_core)
if(WIN32)
    target_link_libraries(my_trainer PRIVATE gtlibcpp::gtlibcpp_win32)
endif()
```

```cpp
#include "gtlibcpp/memory_session.hpp"
#include "gtlibcpp/windows_backend.hpp"
// ... uses the same API as the example above ...
```
