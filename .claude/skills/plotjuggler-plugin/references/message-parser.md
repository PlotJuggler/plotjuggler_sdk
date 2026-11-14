# MessageParser plugin

A MessageParser decodes one **byte payload** at a time into named fields (and
optionally builtin objects). The host binds it to exactly one topic and feeds it
messages. It is **write-only and topic-scoped**: you never name the topic — every
field you write is automatically namespaced under the bound topic.

Full reference: `pj_plugins/docs/message-parser-guide.md`.

## When a MessageParser is the WRONG choice

If your input is a *file* or a *stream* you open yourself, you want a **DataSource**,
not a parser. A parser only decodes payloads the host hands it. A parser is right
when many payloads on a topic share an encoding (JSON, protobuf, ROS, a custom
binary) and you decode each into fields.

## Header (trap)

```cpp
#include <pj_plugins/sdk/message_parser_plugin_base.hpp>   // pj_plugins, NOT pj_base
```

This base class lives under `pj_plugins/sdk/`, unlike the DataSource/Toolbox bases
which live under `pj_base/sdk/`. Do not substitute a `pj_base/sdk/` include.

## The current model: register SchemaHandlers, don't override parse()

The base class keeps a **handler table** keyed by schema/type name. You register a
`PJ::sdk::SchemaHandler` per schema you understand; the base class's `parse()`
dispatches through the table, and newer hosts call the scalar/object routes
directly. Overriding `parse()` yourself is the **legacy v4 path** — it still works,
but the SDK header marks it for deprecation, and every official parser uses the
handler table. A handler *returns* records; it does not call the write host:

```cpp
#include <pj_base/number_parse.hpp>
#include <pj_plugins/sdk/message_parser_plugin_base.hpp>
#include <string_view>

namespace {
class MyParser : public PJ::MessageParserPluginBase {
 public:
  MyParser() {
    // Register under "" so parsing works even when bindSchema() is never
    // called (schema-less encodings like JSON land on the default entry).
    registerSchemaHandler("", makeHandler());
  }

  // Re-register under the real type name once the host binds a schema.
  PJ::Status bindSchema(std::string_view type_name, PJ::Span<const uint8_t> schema) override {
    if (auto st = MessageParserPluginBase::bindSchema(type_name, schema); !st) return st;
    if (findSchemaHandler(type_name) == nullptr) {
      registerSchemaHandler(std::string(type_name), makeHandler());
    }
    return PJ::okStatus();
  }

 private:
  PJ::sdk::SchemaHandler makeHandler() {
    return {
        .object_type = PJ::sdk::BuiltinObjectType::kNone,   // declare what you emit
        .parse_scalars =
            [](PJ::Timestamp /*transport_ts*/, PJ::Span<const uint8_t> payload)
                -> PJ::Expected<PJ::sdk::ScalarRecord> {
              std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
              auto value = PJ::parseNumber<double>(text);       // locale-independent
              if (!value) return PJ::unexpected("payload is not a number");
              PJ::sdk::ScalarRecord rec;
              rec.ts = std::nullopt;   // nullopt → host uses the transport timestamp
              rec.fields.push_back({.name = "value", .value = *value});
              return rec;
            },
        .parse_object = nullptr};
  }
};
}  // namespace

PJ_MESSAGE_PARSER_PLUGIN(MyParser,
    R"({"id":"my-parser","name":"My Parser","version":"1.0.0","encoding":["json"]})")
```

Key pieces:

- **`ScalarRecord`** = `{optional<Timestamp> ts, vector<NamedFieldValue> fields}`.
  Leave `ts` empty to use the host's transport timestamp; set it to override with a
  timestamp embedded in the payload (e.g. a sensor's own clock) — typically gated
  by a `use_embedded_timestamp` flag you read in `loadConfig()`.
- **`parse_object`** is the builtin-object route: return an `ObjectRecord`
  (`{optional<Timestamp> ts, BuiltinObject object}`) for images, point clouds, etc.
  Set the handler's `object_type` to the matching `BuiltinObjectType` so the host
  can choose its ingest policy *before* decoding bytes. See
  `references/builtin-objects.md`.
- **Schema-carrying encodings** (protobuf, ROS, IDL): compile descriptors in
  `bindSchema()` and register one handler per schema/type name. A static catalog
  mapping type names → handlers scales well (the official ROS parser maps 20+
  types this way).

## Route claims and functional extensions

A parser that registers `SchemaHandler`s gets both host-facing tables from the
base class, with no code of its own:

- `pj.parser_route_claims.v1` reports exact claims from the handler table. It
  never reports a wildcard. The host synthesizes one universal wildcard scalar
  claim per manifest encoding.
- `pj.parser_functional.v2` carries scalar records and canonical-wire objects
  (complete, or with one spliced bulk field) through synchronous caller-owned
  sinks.

Scalar and object routes resolve independently. Another provider may therefore
own the object route for a type that this parser still flattens to scalars. There
is no extra override or registration API: populate the `SchemaHandler` table and
the base class derives these extensions.

## Optional overrides

- `loadConfig(json)` / `saveConfig()` — parser options (array-size limits,
  embedded-timestamp flag and field name, …). Tolerate unknown/missing keys.
  The array-size option is a **cross-parser config contract** — never invent your
  own keys for it; `pj_plugins/sdk/parser_array_policy.hpp` owns them (canonical
  `max_array_size` / `array_policy` plus the legacy bools, read with precedence):

  ```cpp
  #include <pj_plugins/sdk/parser_array_policy.hpp>

  PJ::Status loadConfig(std::string_view json) override {
    auto cfg = nlohmann::json::parse(json, nullptr, false);
    if (!cfg.is_object()) cfg = nlohmann::json::object();   // empty or invalid config: defaults
    limit_ = PJ::sdk::arrayLimitFromJson(cfg);       // never throws; defaults 500 / clamp
    use_embedded_ts_ = cfg.value("use_embedded_timestamp", false);
    return PJ::okStatus();
  }
  std::string saveConfig() const override {
    nlohmann::json cfg{{"use_embedded_timestamp", use_embedded_ts_}};
    PJ::sdk::arrayLimitToJson(cfg, limit_);           // writes canonical + legacy keys
    return cfg.dump();
  }
  // in the flatten loop (max_size 0 = unlimited):  if (limit_.max_size != 0 && n > limit_.max_size) { if (!limit_.clamp()) skip field; else n = limit_.max_size; }
  PJ::sdk::ArrayLimit limit_;
  ```
- `parse()` — legacy direct route; only touch it when porting old code. Plugins
  that populate the handler table inherit a working `parse()` from the base.

## Traps specific to MessageParser

- **`encoding` in the manifest is mandatory, case-sensitive and from a closed
  list.** The host routes payloads to your parser by matching these names.
  `"json"` ≠ `"JSON"`, and a name outside `PJ::registeredParserEncodings()`
  (`pj_plugins/host/parser_claim_catalog.hpp`) is refused. If it is missing,
  mismatched or unregistered, your parser is never invoked and the plugin looks dead.
- **A parser that returns builtin objects is tied to the SDK MAJOR it was built
  with.** The host reads the returned struct directly, so a change to a builtin
  struct (1.0.0 changed `Log`, `DepthImage` and the `timestamp_ns` members)
  needs a rebuild against the host's MAJOR.
- **Never name a topic.** The write path is pre-scoped to the bound topic; your
  records' field names become `<topic>/<field>` automatically.
- **Do not require `bindSchema()`.** Schema-less encodings never receive it —
  hence the `registerSchemaHandler("")` default-entry idiom above.
- **String field values are views.** `NamedFieldValue.value` holds a
  `string_view` for strings — the backing `std::string` must stay alive until the
  record is consumed (official parsers keep a `std::deque<std::string>` for
  address stability within a parse call).
- **Stay consistent after a failed parse.** The next message must still decode.
  Build schema-derived caches in `bindSchema()`/`loadConfig()`, never half-way
  through a parse; don't rebuild descriptor pools per message.
- **Return diagnostic-quality errors.** `PJ::unexpected("line 5: unexpected token
  at offset 42")` beats `"parse failed"`.

## Configuration dialog

A parser's dialog is an **independent, host-owned** instance (unlike a DataSource's
embedded dialog): the host creates it and bridges its config to parser instances as
JSON. See `references/dialog.md`.

## Testing

Two proven layers:
- Unit-test the decode logic directly (keep it in a plugin-free static lib).
- Load the **real built `.so`** through the host loader
  (`MessageParserLibrary::load(path)`), bind a
  `pj_base/sdk/testing/parser_write_recorder.hpp` as the write host, and assert on
  the recorded rows — this exercises the ABI surface exactly as the host does.
