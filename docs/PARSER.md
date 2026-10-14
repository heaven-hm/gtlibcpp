# Cheat-table parser

`CheatTableParser` reads Cheat Engine 7.x `.ct` files and produces a
typed `CheatTable`. The parser is intentionally narrow: it supports
the versioned subset the agent service can act on, and it fails
closed on every construct we have decided not to support in this
build.

## Mental model

```
.ct file  ──►  sanitize()  ──►  XmlReader.parse_root()
                                  │
                                  ├─ <CheatTable>?
                                  │   ├─ <Title>?           (captured)
                                  │   ├─ <VersionCE>?       (captured)
                                  │   └─ <CheatEntries>?
                                  │       └─ <CheatEntry>[] (loop)
                                  │           ├─ <ID> | id attr     (stable id)
                                  │           ├─ <Description> | desc attr
                                  │           ├─ <Section>?
                                  │           ├─ <Variable>      (or direct children)
                                  │           │   ├─ <Type> | <VariableType>
                                  │           │   ├─ <Address>
                                  │           │   ├─ <Offsets> | <Offset>*
                                  │           │   └─ <Value>?
                                  │           ├─ <ShowAsSigned>?
                                  │           ├─ <LastState>?
                                  │           ├─ <GroupHeader>?
                                  │           ├─ <Color>?
                                  │           ├─ <Hotkeys>
                                  │           │   └─ <Hotkey>[]
                                  │           │       ├─ <Action>
                                  │           │       └─ <Code>
                                  │           └─ auto-assembler / shell / dll
                                  │               / bytes  (fail-closed)
                                  ▼
                              CheatTable{ entries, unsupported, diagnostics }
```

Both the wrapped form (`<CheatEntry><Variable>...`) and the direct
form (`<CheatEntry><VariableType>...` without a `<Variable>`) are
accepted. The two checked-in fixtures, `CheatTable/IGI.CT` and
`CheatTable/assaultcube.ct`, use the direct form; the synthetic
tests use the wrapped form.

## Variable type resolution

```cpp
gtlibcpp::Result<gtlibcpp::VariableType> type;
bool is_signed = false;
type = gtlibcpp::CheatTableParser::parse_variable_type("4 Bytes", is_signed);
// type.value() == VariableType::uint32, is_signed == false

type = gtlibcpp::CheatTableParser::parse_variable_type("2 Bytes", is_signed);
// type.value() == VariableType::uint16

type = gtlibcpp::CheatTableParser::parse_variable_type("Char", is_signed);
// type.value() == VariableType::int8, is_signed == true
```

Accepted strings (case-insensitive, leading / trailing whitespace
ignored):

| Type string                                | Result                  | `is_signed` |
|--------------------------------------------|-------------------------|-------------|
| `Byte`, `uint8`, `1 byte`                  | `VariableType::uint8`   | `false`     |
| `Char`, `int8`, `signed byte`              | `VariableType::int8`    | `true`      |
| `2 Bytes`, `2 byte`, `Word`, `uint16`      | `VariableType::uint16`  | `false`     |
| `Short`, `int16`, `signed 2 bytes`         | `VariableType::int16`   | `true`      |
| `4 Bytes`, `4 byte`, `DWORD`, `uint32`     | `VariableType::uint32`  | `false`     |
| `Int`, `int32`, `Long`, `signed 4 bytes`   | `VariableType::int32`   | `true`      |
| `8 Bytes`, `8 byte`, `QWORD`, `uint64`     | `VariableType::uint64`  | `false`     |
| `Int64`, `signed 8 bytes`                  | `VariableType::int64`   | `true`      |
| `Float`, `float32`, `Single`               | `VariableType::float32` | `false`     |
| `Double`, `float64`                        | `VariableType::float64` | `false`     |
| `String`                                  | `VariableType::string`  | `false`     |
| `Bytes`, `Array of Bytes`                  | `VariableType::bytes`   | `false`     |
| anything else                              | `ErrorCode::parse_unsupported` | n/a |

`<ShowAsSigned>1</ShowAsSigned>` overrides the default sign for an
unsigned type (e.g. a `4 Bytes` entry shown as signed reads as
`int32` instead of `uint32`).

## Stable entry IDs

The parser derives a stable `EntryId` for every entry, preferring:

1. The `ID` attribute (`<CheatEntry ID="42">`) — `id:42`.
2. The `<ID>42</ID>` child element — `id:42`.
3. The `Description` attribute or `<Description>` child — `desc:Health`.
4. `anon:<index>` as a last resort.

The agent uses the `EntryId` to address entries from a parsed
table, so the same `.ct` file always produces the same `EntryId`
even when the CE-generated GUIDs are stripped.

## Fail-closed kinds

These element names cause the entry to be moved to
`CheatTable::unsupported_entries` with a `failure_reason` and
`auto_assembler` / `shell_command` / `dll_load` / `raw_byte_write`
set. The agent service refuses to act on them; the diagnostics
list reports the exact reason.

| Element name              | Field set                | Reason                                            |
|---------------------------|--------------------------|---------------------------------------------------|
| `AssemblerScript`          | `auto_assembler = true`  | "Auto Assembler entries are not supported"        |
| `AutoAssembler`            | `auto_assembler = true`  | "Auto Assembler entries are not supported"        |
| `AA`                       | `auto_assembler = true`  | "Auto Assembler entries are not supported"        |
| `AAScript`                 | `auto_assembler = true`  | "Auto Assembler entries are not supported"        |
| `LuaScript`                | `auto_assembler = true`  | "Auto Assembler / Lua entries are not supported"   |
| `Inject`                   | `shell_command = true`   | "DLL/shell injection is not supported"            |
| `LoadLibrary`              | `shell_command = true`   | "DLL/shell injection is not supported"            |
| `ShellExecute`             | `shell_command = true`   | "DLL/shell injection is not supported"            |
| `DllInjection`             | `shell_command = true`   | "DLL/shell injection is not supported"            |
| `ByteArray`                | `raw_byte_write = true`  | "raw byte-array writes are not supported"         |
| `Bytes`                    | `raw_byte_write = true`  | "raw byte-array writes are not supported"         |

Auto Assembler is the biggest miss. The first build of the parser
supported a controlled subset of AA; after review we decided the
threat model is too dangerous and we now refuse every AA / Lua entry
without a peephole review. The `failure_reason` field makes this
explicit so the agent can list the skipped entries.

## Symbolic addresses

Some CE 7.x tables use source expressions like
`"IGI.exe" + 0x139560` instead of a plain integer address. The
parser does not resolve the expression; it records the expression
in `CheatTable::diagnostics` and the entry's `address` stays 0.
Module-relative resolution is tracked under the deferred
enhancements issue.

## Diagnostics

Every parse populates `CheatTable::diagnostics` with one entry per
decision:

* `"table has no <CheatEntries> block"`
* `"entry id:1 has a symbolic address expression: \"IGI.exe\" + 0x139560"`
* `"entry id:9: Auto Assembler skipped"`
* `"parsed N supported entries, M unsupported entries"`

The agent's `parse` JSON-RPC method returns `diagnostics` verbatim
so the operator UI can surface them to the user.

## Sanitization

`CheatTableParser::sanitize` strips control characters below `0x20`
(replacing them with a space) while preserving tabs, newlines, and
carriage returns. The legacy regex parser tolerated raw control
bytes that crash the XML reader; the new parser runs `sanitize`
first.

## Worked example

Given:

```xml
<?xml version="1.0" encoding="utf-8"?>
<CheatTable CheatEngineTableVersion="45">
  <Title>IGI cheats</Title>
  <CheatEntries>
    <CheatEntry>
      <ID>56</ID>
      <Description>"Game_Level"</Description>
      <VariableType>Byte</VariableType>
      <Address>"IGI.exe"+0x139560</Address>
      <Hotkeys>
        <Hotkey>
          <Action>Set Value</Action>
          <Code>112</Code>
        </Hotkey>
      </Hotkeys>
    </CheatEntry>
    <CheatEntry>
      <ID>270</ID>
      <Description>"Auto-Assembler example"</Description>
      <VariableType>Auto Assembler Script</VariableType>
      <Address>0</Address>
      <AssemblerScript>[ENABLE]
alloc(newmem,2048)
label(returnhere)
</AssemblerScript>
    </CheatEntry>
  </CheatEntries>
</CheatTable>
```

The parser returns a `CheatTable` with:

* `entries[0]`: `id = "id:56"`, `description = "Game_Level"`,
  `type = uint8`, `address = 0` (symbolic), `hotkeys = [112]`,
  `hotkey_action = "Set Value"`.
* `unsupported_entries[0]`: `id = "id:270"`,
  `auto_assembler = true`, `failure_reason = "Auto Assembler / Lua
  entries are not supported in this build"`.
* `diagnostics` includes the symbolic-address note for entry 56 and
  the Auto Assembler skip for entry 270.
