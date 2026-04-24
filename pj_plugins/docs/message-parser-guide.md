# Writing a MessageParser Plugin

> **Tracks the v4 plugin ABI** (`PJ_ABI_VERSION == 4`). The parser
> write-host stays per-record in v4 (parsers decode one message at a
> time; the host coalesces into Arrow batches internally before
> committing to storage). For ABI evolution rules, error semantics, and
> noexcept discipline see `ARCHITECTURE.md`.

## What is a MessageParser?

A MessageParser plugin is a shared library (`.so` / `.dylib` / `.dll`) that
decodes raw byte payloads — JSON, Protobuf, ROS messages, Influx line protocol,
etc. — into named numeric fields that PlotJuggler can plot. Plugins link only
against `pj_base` (no Qt, no host internals) and communicate through a stable
C ABI.

MessageParsers are typically used via **delegated ingest**: a DataSource plugin
acquires raw data from files or network streams and pushes raw payloads through
the host, which routes them to the appropriate parser based on encoding name.

## Quick Start

1. Subclass `PJ::MessageParserPluginBase`
2. Override `parse()` (required) and optionally `bindSchema()`, `saveConfig()`,
   `loadConfig()`
3. Export with `PJ_MESSAGE_PARSER_PLUGIN(YourClass, R"({"name":"...","version":"...","encoding":"..."})")`
4. Build as a shared library linking `pj_base`

A complete example lives at `pj_plugins/examples/mock_json_parser.cpp`.

## Step by Step

### 1. Declare your class

```cpp
#include <pj_base/sdk/message_parser_plugin_base.hpp>

class MyJsonParser : public PJ::MessageParserPluginBase {
 public:
  PJ::Status parse(PJ::Timestamp timestamp_ns,
                    PJ::Span<const uint8_t> payload) override;
};
```

### 2. Implement parse()

When `parse()` is called, the write host is already bound via
`bindWriteHost()`. Use `writeHost()` (protected) to write decoded fields.
Return `okStatus()` on success, or `unexpected("reason")` on failure.

```cpp
PJ::Status MyJsonParser::parse(PJ::Timestamp timestamp_ns,
                                PJ::Span<const uint8_t> payload) {
  if (!writeHostBound()) {
    return PJ::unexpected(std::string("write host not bound"));
  }

  // Decode payload bytes into field values.
  // Use whatever parsing library your plugin links.
  std::string text(reinterpret_cast<const char*>(payload.data()),
                   payload.size());
  double value = std::strtod(text.c_str(), nullptr);

  const PJ::sdk::NamedFieldValue fields[] = {{.name = "value", .value = value}};
  return writeHost().appendRecord(
      timestamp_ns, PJ::Span<const PJ::sdk::NamedFieldValue>(fields, 1));
}
```

The write host is **topic-scoped** — the host binds it to a specific topic
before calling `parse()`. Fields written via `writeHost().appendRecord()` are
automatically namespaced under that topic. The parser does not need to know or
manage topic names.

### 3. Export the plugin

At file scope, after the class definition. The second argument is a JSON
manifest string literal (see Manifest Schema below):

```cpp
PJ_MESSAGE_PARSER_PLUGIN(MyJsonParser,
    R"({"name":"JSON Parser","version":"1.0.0","encoding":"json"})")
```

This generates the `extern "C"` entry point that the host resolves via dlsym.
The manifest is embedded as a compile-time constant in the vtable, so the host
can read it without creating an instance.

### 4. Build

```cmake
add_library(my_parser_plugin SHARED my_parser.cpp)
target_link_libraries(my_parser_plugin PRIVATE pj_base)
```

No other dependencies are needed for headless parsers. If your parser includes
a configuration dialog (see Dialog Integration below), also link `pj_dialog_sdk`:

```cmake
target_link_libraries(my_parser_plugin PRIVATE pj_base pj_dialog_sdk)
```

## Lifecycle

The host drives the parser through these phases:

```
create() → bind_write_host() → [bind_schema()] → parse()* → destroy()
```

1. **create** — the host calls `create()` to allocate a new parser instance.
2. **bind_write_host** — the host provides the data-plane write host. Must be
   called before `parse()`.
3. **bind_schema** (optional) — for parsers that need schema (Protobuf, ROS,
   IDL), the host provides the schema bytes and type name. Parsers that don't
   need schema (JSON, Influx) can ignore this call.
4. **parse** — called once per message. The parser decodes the payload and
   writes fields via `writeHost()`.
5. **destroy** — the host destroys the instance.

## Write Host API

Access via `writeHost()` (protected). The write host is topic-scoped — every
field and record you write is automatically placed under the parser's assigned
topic.

| Method | Purpose |
|---|---|
| `ensureField(name, type)` | Optional: pre-register a field. Enables `appendBoundRecord`. Returns a `FieldHandle`. |
| `appendRecord(timestamp, fields)` | Write a row of named field values. Auto-creates new fields. |
| `appendBoundRecord(timestamp, fields)` | Write using pre-resolved field handles (faster). |

The parser write surface is **per-record only** in v4. There is no
`appendArrowStream` / `appendArrowIpc` slot on the parser write host:
one `parse()` call decodes one message, so batch boundaries are the
host's concern, not the parser's. The host coalesces per-record
writes into Arrow batches internally before committing them to
storage. If you are porting a plugin that used to emit whole IPC
streams directly (a Parquet-to-Arrow bulk loader, for example), it
belongs as a **DataSource** plugin instead — see
`data-source-guide.md` for the `appendArrowStream` contract.

### Named vs bound writes

For simple parsers, use `appendRecord()` with `NamedFieldValue` — fields are
auto-created on first non-null value, and names are resolved on each call:

```cpp
const PJ::sdk::NamedFieldValue fields[] = {
    {.name = "temperature", .value = 23.5},
    {.name = "humidity", .value = 61.0}};
writeHost().appendRecord(timestamp_ns, PJ::Span(fields));
```

For high-throughput parsers, pre-register fields with `ensureField()` and use
`appendBoundRecord()` with `BoundFieldValue` — field handles are resolved once:

```cpp
// During bind_schema or first parse:
auto temp_field = writeHost().ensureField("temperature",
                                          PJ::PrimitiveType::kFloat64);
auto hum_field = writeHost().ensureField("humidity",
                                          PJ::PrimitiveType::kFloat64);

// During each parse:
const PJ::sdk::BoundFieldValue fields[] = {
    {.field = *temp_field, .value = 23.5},
    {.field = *hum_field, .value = 61.0}};
writeHost().appendBoundRecord(timestamp_ns, PJ::Span(fields));
```

## Optional Features

### Schema binding

Override `bindSchema()` to receive schema data before parsing begins. The
default implementation is a no-op.

```cpp
PJ::Status bindSchema(std::string_view type_name,
                       PJ::Span<const uint8_t> schema) override {
  // Store schema for use during parse().
  type_name_ = std::string(type_name);
  schema_.assign(schema.begin(), schema.end());
  // Build lookup tables, compile descriptors, etc.
  return PJ::okStatus();
}
```

The `type_name` is the encoding-specific message type (e.g.
`"sensor_msgs/Imu"` for ROS, `"my.package.ImuSample"` for Protobuf). The
`schema` bytes are encoding-specific (e.g. ROS `.msg` definition text,
Protobuf `FileDescriptorSet` binary).

### Configuration persistence

Override `saveConfig()` / `loadConfig()` to support layout save/restore:

```cpp
std::string saveConfig() const override { return config_json_; }

PJ::Status loadConfig(std::string_view json) override {
  config_json_ = std::string(json);
  // Parse JSON and apply settings.
  // e.g. max_array_size_, use_embedded_timestamp_, etc.
  return PJ::okStatus();
}
```

Common configuration patterns:
- **Array clamping**: `{"max_array_size": 100}` — limits how many array
  elements are expanded into individual series.
- **Embedded timestamps**: `{"use_embedded_timestamp": true}` — the parser
  extracts timestamp from the payload (e.g. ROS `Header.stamp`) instead of
  using the host-provided `timestamp_ns`.

### Embedded timestamp extraction

The `parse()` method receives a host-provided `timestamp_ns`. If the message
payload contains its own timestamp (e.g. a ROS Header or protobuf timestamp
field), the parser is free to ignore the host timestamp and write records with
the extracted timestamp instead:

```cpp
PJ::Status parse(PJ::Timestamp timestamp_ns,
                  PJ::Span<const uint8_t> payload) override {
  // Extract embedded timestamp from payload.
  PJ::Timestamp ts = use_embedded_timestamp_
      ? extractTimestamp(payload)
      : timestamp_ns;

  const PJ::sdk::NamedFieldValue fields[] = { /* ... */ };
  return writeHost().appendRecord(ts, PJ::Span(fields));
}
```

This is a parser-internal decision, controlled via `loadConfig()`.

### Dialog integration

A MessageParser can provide a configuration dialog by exporting a dialog vtable
from the same `.so`. This is useful for parsers that need GUI-based schema
selection (e.g. Protobuf `.proto` file loading, include-path management,
message-type selection).

The host resolves the dialog via `MessageParserLibrary::resolveDialogVtable()`.

#### Ownership model — independent owned instance

Unlike a DataSource dialog (which is a member of the source, accessed via a
borrowed handle through `getDialog()`), a **parser dialog is an independent
owned instance**. The host creates it via `dialog_vt->create()`, runs it
through `DialogEngine`, and feeds the resulting config JSON to parser instances
via `load_config()`. The dialog and parser classes share a JSON config schema
but are otherwise decoupled.

This works because parser instances are created per-topic by the host during
`ensureParserBinding()`, while the dialog should be presented once per parser
*library*. There is no `get_dialog_context` on the parser vtable — and none is
needed.

```
     Parser .so
┌──────────────────────────────────┐
│  class ProtoDialog               │  ← PJ::DialogPluginTyped
│    (file picker, include paths,  │
│     message-type combo)          │
│                                  │
│  class ProtoParser               │  ← PJ::MessageParserPluginBase
│    (no dialog_ member)           │
│    loadConfig(json) applies it   │
│                                  │
│  PJ_MESSAGE_PARSER_PLUGIN(...)   │  → exports parser vtable
│  PJ_DIALOG_PLUGIN(ProtoDialog)   │  → exports dialog vtable
└──────────────────────────────────┘
```

One `.so`, two vtables, but **no ownership link** between dialog and parser
instances. The host creates the dialog independently and bridges config via
JSON.

#### Lifecycle scenarios

**Inline (embedded in a DataSource dialog):** A source dialog declares a
`pj_parser_slot` placeholder widget. The host detects it, resolves the parser
library for the selected encoding, creates an owned parser dialog instance, and
renders it into the slot. The source and parser dialogs share one window but
persist config independently — `ConfigEnvelope.source_config` for the source,
`ConfigEnvelope.parser_binding` for the parser.

**Standalone:** The host shows the parser dialog as a modal from a settings
panel or parser-selection UI. This is host-application logic, not protocol.

#### Config flow for delegated sources

1. User configures the parser in the dialog → `parser_dialog.save_config()` →
   host stores the JSON in `ConfigEnvelope.parser_binding`.
2. Source calls `runtimeHost().ensureParserBinding(request)` — the source may
   leave `parser_config_json` empty in the request.
3. Host intercepts, injects the stored parser config into
   `parser.load_config()` before calling `parse()`.
4. On layout save, the host persists the full `ConfigEnvelope`.
5. On headless restart, the host loads the envelope →
   `source.loadConfig(source_config)` + restores parser bindings from
   `parser_binding` → `source.start()`.

#### Protobuf example sketch

A Protobuf parser dialog would typically manage:
- `.proto` file selection (file picker)
- Include paths for imports
- Root message-type selection (combo box populated after parsing `.proto` files)
- Config JSON: `{"proto_files": [...], "include_paths": [...], "message_type": "..."}`

The parser's `loadConfig()` receives this JSON, compiles the descriptor pool,
and uses the selected message type for decoding in `parse()`. The dialog and
parser never reference each other — they only share the JSON schema contract.

## Manifest Schema

The manifest is a JSON string literal embedded in the vtable. The host reads
it without instantiating the plugin.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| `name` | string | yes | Human-readable plugin name. |
| `version` | string | yes | Semver version string. |
| `encoding` | string | yes | Encoding this parser handles, e.g. `"json"`, `"protobuf"`, `"ros1msg"`. The host uses this to match binding requests to parsers. |

Example:
```json
{
  "name": "Protobuf Parser",
  "version": "1.0.0",
  "encoding": "protobuf"
}
```

## Threading Model

All parser callbacks — `parse()`, `bindSchema()`, `bindWriteHost()`,
`loadConfig()`, `saveConfig()` — are called **on the host's thread**. The host
guarantees single-threaded access per parser instance: no two callbacks will
overlap for the same instance.

Write host methods (`appendRecord()`, `ensureField()`, etc.) must be called
from the same thread that invoked `parse()`. Do not cache the write host view
and call it from a background thread.

## Lifecycle Invariants

The host guarantees the following call ordering:

1. `create()` — always first.
2. `bind_write_host()` — before `parse()`.
3. `bind_schema()` (optional) — before `parse()`, called at most once.
4. `load_config()` — before `parse()`, may be called multiple times.
5. `parse()` — called once per message, may be called many times.
6. `destroy()` — always last.

The host will never call `parse()` before `bind_write_host()`, and `destroy()`
is always the last call.

## Error Handling

All fallible SDK methods return `PJ::Status` (`Expected<void>`). Return
`okStatus()` on success, `unexpected("reason")` on failure.

### Patterns

**Check-and-propagate** — the standard pattern for write host calls:

```cpp
auto field = writeHost().ensureField("temperature", PJ::PrimitiveType::kFloat64);
if (!field) {
  return PJ::unexpected(field.error());
}
```

**Parse failures** — when `parse()` encounters malformed payload data:
- Return `unexpected("reason")` — the host logs the error and may skip
  the message. This is always safe; the host will continue calling `parse()`
  for subsequent messages.
- Do **not** leave the parser in an inconsistent state — ensure field handles
  and internal buffers remain valid for the next `parse()` call.

**setLastError()** — available for fine-grained error reporting. For most
cases, returning `unexpected()` from `parse()` is sufficient.

**Exception safety** — the SDK base class catches all C++ exceptions in
virtual method trampolines and converts them to `setLastError()` + false
return. You never need to worry about exceptions crossing the C ABI boundary.

## How Parsers Are Used (Host Perspective)

DataSource plugins that act as transports (MQTT, ZMQ, MCAP, ROS bag files)
don't decode payloads themselves. Instead they declare `kCapabilityDelegatedIngest`
and push raw bytes through the host:

```
DataSource                        Host                         MessageParser
    │                               │                               │
    │  ensureParserBinding(         │                               │
    │    topic="sensor/imu",        │                               │
    │    encoding="protobuf",       │──→ load parser .so            │
    │    type_name="ImuSample",     │──→ create()                   │
    │    schema=descriptor_bytes)   │──→ bind_write_host()          │
    │                               │──→ bind_schema("ImuSample",   │
    │                               │       descriptor_bytes)       │
    │  ←── binding handle           │                               │
    │                               │                               │
    │  pushRawMessage(handle,       │                               │
    │    timestamp, payload)        │──→ parse(timestamp, payload)  │
    │                               │       │                       │
    │                               │       │ writeHost().append... │
    │                               │       ▼                       │
    │                               │    data stored                │
```

The parser is topic-scoped — the host binds a separate write host per topic,
so `ensureField("x")` in the parser creates `"sensor/imu/x"` in the datastore.

## Testing

Use `PJ::sdk::testing::ParserWriteRecorder` from
`pj_base/include/pj_base/sdk/testing/parser_write_recorder.hpp` to write
parser unit tests without re-implementing the fake write-host vtable:

```cpp
#include <pj_base/sdk/testing/parser_write_recorder.hpp>

TEST(MyParserTest, Basic) {
  auto library = PJ::MessageParserLibrary::load(PJ_MY_PARSER_PLUGIN_PATH);
  auto handle = library->createHandle();

  PJ::sdk::testing::ParserWriteRecorder recorder;
  PJ::ServiceRegistryBuilder registry;
  registry.registerService<PJ::sdk::ParserWriteHostService>(recorder.makeHost());
  ASSERT_TRUE(handle.bind(registry.view()));

  const uint8_t payload[] = { /* ... */ };
  ASSERT_TRUE(handle.parse(1000, payload));

  ASSERT_EQ(recorder.rows().size(), 1u);
  EXPECT_EQ(recorder.rows()[0].fields[0].name, "temperature");
  EXPECT_DOUBLE_EQ(recorder.rows()[0].fields[0].numeric, 23.5);
}
```

Each `RecordedField` exposes the primitive type plus `.numeric` (for all
integer/float types, plus `1.0/0.0` for bools), `.bool_value`, and
`.string_value`, so tests can assert uniformly without writing type
dispatch code.

## Examples

- `pj_plugins/examples/mock_json_parser.cpp` — minimal parser that treats
  payloads as text-encoded doubles and writes one "value" field per message.
- `pj_plugins/examples/mock_schema_parser.cpp` — richer parser demonstrating
  the high-throughput pattern: `ensureField()` + `appendBoundRecord()`, schema
  binding, config persistence, and error handling.
- `pj_base/tests/message_parser_plugin_base_test.cpp` — comprehensive test
  fixture exercising the full SDK surface: vtable generation, bind/parse
  round-trip, schema binding, config persistence, and exception safety.
