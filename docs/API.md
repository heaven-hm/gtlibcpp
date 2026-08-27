# API Reference

This is the complete public API reference for the `gtlibcpp::` core.
The legacy `GTLibc.*` API is documented at the bottom; see
[MIGRATION.md](MIGRATION.md) for the recommended upgrade path.

## Conventions

* Every public operation returns a `Result<T>` (or `Result<void>` for
  side-effect-only operations) carrying an `Error` with at minimum a
  code, a human-readable message, and the operation name. Addresses,
  byte counts, system error codes, and request IDs are populated
  where applicable.
* `Result<T>::ok()` returns `true` on success. Dereferencing the
  error path (`!ok()` and then `value()`) **aborts**. The intended
  pattern is `if (auto r = op(); r) { ... } else { log(r.error()); }`.
* All types in the namespace `gtlibcpp` are C++17, exception-neutral
  on every public path (any `std::exception` from a backend becomes a
  structured `ErrorCode::internal`).
* `Address` is `std::uint64_t`; on x86 targets the high 32 bits are
  zero. `Architecture` is recorded per `TargetIdentity`.

## Result and Error

### `enum class ErrorCode : std::uint32_t`

Stable error codes. The string returned by `to_string(ErrorCode)` is
part of the agent wire contract.

| Code                                  | When                                                      |
|---------------------------------------|-----------------------------------------------------------|
| `ok`                                  | Success.                                                  |
| `not_connected`                       | No backend / no target.                                  |
| `target_not_found`                    | `OpenProcess` returned null.                              |
| `target_arch_mismatch`                | Architecture does not match.                             |
| `target_identity_mismatch`            | `TargetIdentity` does not match the manifest.             |
| `target_dead`                         | Process is no longer alive.                              |
| `invalid_address`                     | Address out of range, wraps, or `0`.                     |
| `invalid_size`                        | Size 0, too large, or > 1 MiB.                            |
| `invalid_offsets`                     | Pointer chain has 0 offsets.                              |
| `invalid_hotkey`                      | Empty hotkey list.                                       |
| `invalid_entry_id`                    | Empty / duplicate id.                                    |
| `invalid_type`                        | Unknown freeze variable type.                            |
| `invalid_string`                      | String capacity violated, missing NUL.                   |
| `read_failed`                         | Backend returned a read error.                           |
| `partial_read`                        | Backend returned fewer bytes than requested.             |
| `write_failed`                        | Backend returned a write error.                          |
| `partial_write`                       | Backend returned fewer bytes than requested.             |
| `verification_mismatch`              | Post-write verify did not match the intended value.       |
| `snapshot_missing`                    | No freeze registered for the id.                         |
| `rollback_failed`                     | Restore could not rewrite the snapshotted value.          |
| `parse_failed`                        | XML could not be parsed.                                 |
| `parse_unsupported`                   | A Cheat Engine construct we explicitly do not support.    |
| `parse_diagnostic`                    | Non-fatal parser finding.                                |
| `policy_denied`                       | The policy layer refused the operation.                  |
| `approval_required`                   | The mutation needs an approved token.                    |
| `capability_missing`                  | The requested capability is not declared.               |
| `rate_limited`                        | Per-target rate limit exceeded.                          |
| `timeout`                             | Operation timed out.                                     |
| `cancelled`                           | Operation was cancelled.                                 |
| `internal`                            | Unexpected internal error (often a caught exception).     |

### `struct Error`

```cpp
struct Error {
    ErrorCode   code{ErrorCode::ok};
    std::string message{};
    std::string operation{};
    std::uint64_t address{0};
    std::size_t   bytes{0};
    std::uint32_t system_error{0};   // GetLastError on Windows, errno elsewhere
    std::string request_id{};
    bool ok() const noexcept;
};
```

`make_error(...)` constructs an `Error` with all fields set.

### `class Result<T>`

```cpp
template <typename T>
class Result {
public:
    static Result success(T value);
    static Result failure(Error error);

    bool ok() const noexcept;
    explicit operator bool() const noexcept;

    // Aborts on error. Use ok() first.
    const T&  value() const&;
    T&&        value() &&;

    // Explicit opt-in: returns the fallback on error.
    T value_or(T fallback) const&;
    T value_or(T fallback) &&;

    // Aborts on error with a more diagnostic message.
    const T& expect(const char* msg) const&;

    const Error& error() const&;
    Error        error() &&;
};
```

`Result<void>` is a specialisation: `success()` and `failure(Error)`;
`ok()` for the test; `error()` for the structured failure.

## Address, Architecture, TargetIdentity

```cpp
using Address = std::uint64_t;
constexpr Address invalid_address = 0;

enum class Architecture : std::uint8_t { unknown, x86, x64 };

const char* to_string(Architecture) noexcept;  // "x86" / "x64" / "unknown"

struct TargetIdentity {
    std::uint32_t pid{};
    std::uint64_t start_time{};        // Windows FILETIME
    std::string   image_path{};
    std::string   image_sha256{};      // lower-case hex
    Architecture  architecture{Architecture::unknown};
    bool empty() const noexcept;
    bool operator==(const TargetIdentity&) const noexcept;
    bool operator!=(const TargetIdentity&) const noexcept;
};

struct ExpectedValue {
    std::string kind{"hex"};           // "hex" | "int" | "float" | "double" | "string"
    std::string value{};
    bool empty() const noexcept;
};
```

## Memory backend

```cpp
class IMemoryBackend {
public:
    virtual ~IMemoryBackend() = default;

    // Read exactly `size` bytes. On partial read returns
    // ErrorCode::partial_read. On OS error returns
    // ErrorCode::read_failed with system_error populated.
    virtual Result<std::vector<std::uint8_t>>
    read(Address address, std::size_t size) = 0;

    // Write `bytes`. On partial write returns ErrorCode::partial_write.
    virtual Result<std::size_t>
    write(Address address, const std::vector<std::uint8_t>& bytes) = 0;

    virtual bool          is_alive() noexcept = 0;
    virtual std::string   target_id()  const = 0;
    virtual TargetIdentity identity() const = 0;
};
using MemoryBackendPtr = std::shared_ptr<IMemoryBackend>;

// Free-function range checks. Call these before any read/write.
Result<std::size_t> validate_read_request (Address, std::size_t);
Result<std::size_t> validate_write_request(Address, std::size_t);
```

`MemoryBackendPtr` is the canonical way to construct a session.

### Windows backend

```cpp
struct WindowsBackendOptions {
    std::uint32_t pid{0};
    std::string   image_path{};
    Architecture  architecture{Architecture::unknown};
    bool          read_only{false};
    std::uint32_t desired_access{0};   // 0 = sensible default
};

MemoryBackendPtr make_windows_backend(const WindowsBackendOptions& options);
```

`OpenProcess` is called with the minimum access rights the operation
needs. The SHA-256 image hash is computed via `BCrypt`; the start
time via `GetProcessTimes`. If `OpenProcess` fails, the returned
backend reports `is_alive() == false` and `identity().pid == 0`.

## Memory session

```cpp
class MemorySession {
public:
    explicit MemorySession(MemoryBackendPtr backend);
    MemorySession(const MemorySession&) = delete;
    MemorySession& operator=(const MemorySession&) = delete;
    MemorySession(MemorySession&&) = delete;
    MemorySession& operator=(MemorySession&&) = delete;
    ~MemorySession() = default;

    // Typed scalar read. T must be trivially copyable and not a
    // pointer. Use read<std::uintptr_t> for pointer reads.
    template <typename T>
    Result<T> read(Address address);

    // Raw byte read; the vector size is exactly `size` on success.
    Result<std::vector<std::uint8_t>>
    read_bytes(Address address, std::size_t size);

    // Typed scalar write. T must be trivially copyable and not a
    // pointer.
    template <typename T>
    Result<std::size_t> write(Address address, const T& value);

    // Raw byte write.
    Result<std::size_t>
    write_bytes(Address address, const std::vector<std::uint8_t>& bytes);

    // Read, write, read; fail if the post-write read does not match.
    template <typename T>
    Result<T> compare_write_verify(Address address, const T& value,
                                  std::uint32_t max_attempts = 1);

    // Walk a pointer chain. Each offset is added to the previous
    // result and read as std::uintptr_t (4 bytes on x86, 8 on x64).
    // Returns the final concrete address; reports which step failed.
    Result<Address> resolve_pointer_chain(
        Address base, const std::vector<Address>& offsets);

    // Multi-offset raw read.
    Result<BatchResult> read_offsets(
        Address base, const std::vector<Address>& offsets, std::size_t size);

    // Multi-offset typed write. Stops at the first failed step and
    // reports the failed address.
    template <typename T>
    Result<BatchResult> write_offsets(
        Address base, const std::vector<Address>& offsets, const T& value);

    // Read up to `capacity` bytes from `address` and interpret as a
    // string. require_nul_terminator=true returns ErrorCode::invalid_string
    // if no NUL is found.
    Result<BoundedString> read_string(Address address, std::size_t capacity,
                                     bool require_nul_terminator);

    // Write the bounded string plus a NUL into `target_capacity` bytes
    // starting at `address`.
    Result<std::size_t> write_string(Address address, const BoundedString& value,
                                   std::size_t target_capacity);

    const TargetIdentity& identity() const noexcept;
    bool          is_alive() const noexcept;
    std::string   target_id()  const;
    IMemoryBackend* backend() const noexcept;
};
```

### `struct BatchResult`

```cpp
struct BatchResult {
    bool complete{true};
    std::vector<Address> completed_addresses{};
    std::vector<Address> attempted_addresses{};
    std::optional<Error> failure{};

    static BatchResult ok_full(std::vector<Address> done);
    static BatchResult partial(std::vector<Address> done,
                               std::vector<Address> attempted,
                               Error failure);
};
```

### `class BoundedString`

```cpp
class BoundedString {
public:
    static Result<BoundedString> from_utf8(
        const std::vector<std::uint8_t>& bytes, std::size_t capacity,
        bool require_nul_terminator);
    static Result<BoundedString> from_bytes(
        const std::vector<std::uint8_t>& bytes, std::size_t capacity,
        bool require_nul_terminator);

    const std::vector<std::uint8_t>& bytes() const noexcept;
    std::size_t size()     const noexcept;
    std::size_t capacity() const noexcept;
    bool        truncated() const noexcept;

    Result<std::vector<std::uint8_t>> to_bytes(std::size_t target_capacity) const;
};
```

## Freeze manager

```cpp
struct FreezeRequest {
    std::string  id{};                                    // unique per session
    Address      address{0};
    std::string  type_name{"uint32"};                     // see below
    std::uint64_t value_u64{0};
    std::chrono::milliseconds interval{50};
    std::size_t   size{0};                                // 0 = derive from type
};

struct FreezeStatus {
    std::string  id{};
    Address      address{0};
    std::size_t  size{0};
    std::uint64_t value_u64{0};
    std::size_t  successful_rewrites{0};
    std::size_t  failed_rewrites{0};
    std::uint64_t original_value_u64{0};
    bool         active{false};
    bool         restored{false};
    std::string  last_error{};
};

class FreezeManager {
public:
    explicit FreezeManager(std::shared_ptr<MemorySession> session);
    ~FreezeManager();

    FreezeManager(const FreezeManager&) = delete;
    FreezeManager& operator=(const FreezeManager&) = delete;

    Result<void>          freeze(FreezeRequest request);
    Result<void>          unfreeze(const std::string& id);
    Result<void>          restore(const std::string& id);
    Result<FreezeStatus>  status (const std::string& id) const;
    std::vector<FreezeStatus> all_status() const;

    void cancel_all();                 // joins every worker
    std::size_t active_count() const;
};
```

Type names accepted by `FreezeRequest::type_name`:
`uint8`, `int8`, `uint16`, `int16`, `uint32`, `int32`, `uint64`,
`int64`, `float`, `double`.

`freeze` snapshots the original value at the address, then starts a
worker thread that re-writes the value every `interval` until
`unfreeze` or `cancel_all` is called. The worker is joinable on
destruction. If the target process dies the worker exits
cooperatively and reports the exit via `last_error`.

`restore` joins the worker, then re-writes the snapshotted value
exactly once. The freeze entry is marked `restored = true`.

## Policy

```cpp
enum class Capability : std::uint32_t {
    read         = 1u << 0,
    write        = 1u << 1,
    freeze       = 1u << 2,
    unfreeze     = 1u << 3,
    rollback     = 1u << 4,
    parse        = 1u << 5,
};
Capability operator|(Capability, Capability) noexcept;
std::uint32_t as_bits(Capability) noexcept;

struct TargetManifest {
    std::string  alias{};            // human-readable handle
    std::string  image_path{};
    std::string  image_sha256{};      // "" => not checked
    std::string  architecture{};      // "x86" / "x64" / "" => not checked
    bool         allow_write{false};  // false => read-only target
    bool matches(const TargetIdentity&) const noexcept;
};

struct MutationRequest {
    std::string  request_id{};
    std::string  target_alias{};
    Capability   capability{Capability::read};
    bool         preview{false};
    bool         approved{false};
    std::string  expected_current_hash{};   // required for any mutation
    Address      address{0};
    std::size_t  size{0};
    std::string  value_repr{};
};

struct ApprovalToken {
    std::string  request_id{};
    std::string  target_alias{};
    std::int64_t issued_at_ms{};
    std::int64_t expires_at_ms{};
    std::string  signature{};
};

struct AuditEvent {
    std::string  request_id{};
    std::string  alias{};
    std::uint32_t pid{};
    std::string  operation{};
    std::string  capability{};
    std::string  decision{};          // "allow" / "deny" / "preview"
    std::string  reason{};
    std::int64_t timestamp_ms{};
};

class RateLimiter {
public:
    explicit RateLimiter(std::size_t max_per_minute);
    Result<void> allow(const std::string& alias);
};

class KillSwitch {
public:
    void engage(std::string reason) noexcept;
    void release() noexcept;
    bool engaged() const noexcept;
    const std::string& reason() const noexcept;
};

class Policy {
public:
    using AuditSink = void (*)(const AuditEvent&, void* user);
    Policy(std::vector<TargetManifest> manifests,
           AuditSink sink = nullptr,
           void* user = nullptr);

    Result<void> authorize(const TargetIdentity&, const MutationRequest&) const;
    Result<void> authorize_with_token(const TargetIdentity&,
                                      const MutationRequest&,
                                      const ApprovalToken*) const;

    void set_kill_switch_reason(std::string reason) noexcept;
    void emit_audit(AuditEvent event) const noexcept;

    const std::vector<TargetManifest>& manifests() const noexcept;
    RateLimiter& rate_limiter() noexcept;
    const RateLimiter& rate_limiter() const noexcept;
    const KillSwitch& kill_switch() const noexcept;
};
```

`Policy::authorize` enforces, in order: kill switch; manifest exists
and identity matches; capability allowed; address and size valid;
`expected_current_hash` non-empty; if `preview` then return success
and audit; otherwise require `approved` and (if provided) a
non-expired `ApprovalToken`; finally consume a rate-limiter slot.
On every deny, an audit event with `decision = "deny"` is emitted.

## Cheat-table parser

```cpp
enum class VariableType : std::uint8_t {
    unknown, uint8, int8, uint16, int16, uint32, int32, uint64, int64,
    float32, float64, string, bytes,
};

struct EntryId {
    std::string value{};
    bool empty() const noexcept;
    bool operator==(const EntryId&) const noexcept;
    bool operator!=(const EntryId&) const noexcept;
};
// std::hash<EntryId> is also defined.

struct CheatEntry {
    EntryId      id{};
    std::string  description{};
    std::string  section{};
    Address      address{0};
    std::vector<Address> offsets{};
    VariableType type{VariableType::unknown};
    bool         is_signed{false};
    std::string  hotkey_action{};
    std::vector<int> hotkeys{};
    std::string  default_value_text{};
    bool         auto_assembler{false};
    bool         shell_command{false};
    bool         dll_load{false};
    bool         raw_byte_write{false};
    std::string  failure_reason{};
};

struct CheatTable {
    std::string  title{};
    std::uint32_t version_ce{0};
    std::vector<CheatEntry> entries{};
    std::vector<std::string> diagnostics{};
    std::vector<CheatEntry> unsupported_entries{};
};

class CheatTableParser {
public:
    CheatTableParser();
    ~CheatTableParser();

    Result<CheatTable> parse_file(const std::string& path) const;
    Result<CheatTable> parse_string(const std::string& xml) const;

    static std::string sanitize(const std::string& input);
    static Result<VariableType>
    parse_variable_type(const std::string& text, bool& is_signed);
    static std::string to_string(VariableType t);
};
```

See [PARSER.md](PARSER.md) for the supported XML subset and the
fail-closed kinds.

## Agent service

```cpp
class IAgentTransport {
public:
    virtual ~IAgentTransport() = default;
    virtual Result<void>          send   (const std::string& json) = 0;
    virtual Result<std::string>   receive() = 0;
    virtual void                  close  () = 0;
};

class AgentService {
public:
    static constexpr const char* kProtocolVersion = "gtlibcpp.agent/1.0";

    AgentService(std::shared_ptr<MemorySession>    session,
                 std::shared_ptr<Policy>           policy,
                 std::shared_ptr<FreezeManager>    freeze,
                 std::shared_ptr<CheatTableParser> parser);
    ~AgentService();

    AgentService(const AgentService&) = delete;
    AgentService& operator=(const AgentService&) = delete;

    // Run one request/response cycle.
    Result<std::string> handle(const std::string& request_json);

    // Long-lived server loop on a transport. One request at a time.
    void serve(std::shared_ptr<IAgentTransport> transport);
    void stop();
};
```

JSON helpers (also in the `gtlibcpp` namespace):

```cpp
std::string         json_encode(const JsonValue&);
Result<JsonValue>   json_decode(const std::string&);
const JsonValue*    json_get    (const JsonValue& obj, const std::string& key);
std::string         json_get_string(const JsonValue& obj, const std::string& key,
                                    const std::string& fallback = {});
std::int64_t        json_get_int   (const JsonValue& obj, const std::string& key,
                                    std::int64_t fallback = 0);
bool                json_get_bool  (const JsonValue& obj, const std::string& key,
                                    bool fallback = false);
```

`AgentService::handle` is a single request/response cycle; `serve`
runs the loop until `stop` is called. See [AGENT.md](AGENT.md) for
the full method reference and request lifecycle.

## Logger

```cpp
enum class LogLevel : std::uint8_t { debug, info, warn, error };

struct LogEvent {
    LogLevel   level{};
    std::string operation{};
    ErrorCode   code{};
    std::string message{};
    std::uint64_t address{};
    std::size_t   bytes{};
    std::int64_t  timestamp_ms{};
};

class Logger {
public:
    static Logger& instance();

    using Sink = void (*)(const LogEvent&, void* user);
    void set_sink(Sink sink, void* user) noexcept;
    void reset_sink() noexcept;
    void set_min_level(LogLevel) noexcept;

    void log(LogLevel, const std::string& op, ErrorCode, const std::string& msg,
             std::uint64_t address = 0, std::size_t bytes = 0);

    void debug(const std::string& op, const std::string& m,
               std::uint64_t a = 0, std::size_t b = 0);
    void info (const std::string& op, const std::string& m,
               std::uint64_t a = 0, std::size_t b = 0);
    void warn (const std::string& op, const std::string& m,
               ErrorCode c = ErrorCode::ok, std::uint64_t a = 0, std::size_t b = 0);
    void error(const std::string& op, const std::string& m,
               ErrorCode c, std::uint64_t a = 0, std::size_t b = 0);
};
```

The default sink writes a single line to stderr. Production
deployments should inject their own sink that forwards to a
structured log.

## Windows-only factories

```cpp
// src/windows_backend.cpp
struct WindowsBackendOptions { /* see above */ };
MemoryBackendPtr make_windows_backend(const WindowsBackendOptions&);

// src/named_pipe_transport.cpp
struct NamedPipeServerOptions {
    std::string pipe_name{"gtlibcpp.agent"};
    std::uint32_t max_instances{1};
    std::uint32_t buffer_size{64 * 1024};
};
std::shared_ptr<IAgentTransport>
make_named_pipe_server(const NamedPipeServerOptions& options);
std::shared_ptr<IAgentTransport>
make_named_pipe_client(const std::string& pipe_name);
```

`make_named_pipe_client` is now a real implementation that opens
`\\.\pipe\<name>` with the same `kProtocolMagic` + 4-byte length +
payload framing as the server.

## Cross-platform transport (tests + headless agents)

```cpp
class InProcTransportPair;  // queue-pair, enable_shared_from_this

class InProcTransport final : public IAgentTransport {
public:
    explicit InProcTransport(std::shared_ptr<InProcTransportPair> pair,
                             bool client_side);
    // InProcTransportPair::make_client() / make_server() build the
    // two halves of a queue pair.
};
```

## Legacy API

The legacy trainer surface (`GTLibc.hpp`, `GTLibc.cpp`, `GTLibc.tpp`,
`IGITrainer.cpp`, `GenericTrainer.cpp`, `CEParser.{hpp,cpp}`) is still
in the tree and is compiled into the optional `gtlibcpp::win32`
target when `GTLIBCPP_BUILD_DEMOS=ON`. It is not the supported path;
see [MIGRATION.md](MIGRATION.md).
