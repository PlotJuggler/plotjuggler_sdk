# Toolbox plugin

A Toolbox is an interactive tool that, unlike the other families, can **read**
existing host data, **transform** it, and **write** new topics or whole new data
sources. It usually has a dialog for its UI.

Full reference: `pj_plugins/docs/toolbox-guide.md`.

## Header + skeleton

```cpp
#include <pj_base/sdk/toolbox_plugin_base.hpp>

namespace {
class MyToolbox : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    return 0;   // or PJ::kToolboxCapabilityHasDialog if you embed a dialog
  }

  std::string saveConfig() const override { return config_; }
  PJ::Status  loadConfig(std::string_view json) override {
    config_ = std::string(json);
    return PJ::okStatus();
  }

  void onDataChanged() override {
    // host tells you the dataset changed; re-read if you cache anything
  }

  // Do work when the user acts (e.g. from the dialog). Take the timestamps from
  // the input series you transform — do not invent them.
  PJ::Status applyTransform(PJ::Timestamp input_ts) {
    if (!toolboxHostBound() || !runtimeHostBound()) return PJ::unexpected("hosts not bound");
    auto host = toolboxHost();
    auto source = host.createDataSource("my_output");
    if (!source) return PJ::unexpected(source.error());
    auto topic = host.ensureTopic(*source, "result");
    if (!topic) return PJ::unexpected(topic.error());

    const PJ::sdk::NamedFieldValue f[] = {{.name = "value", .value = 99.0}};
    auto st = host.appendRecord(*topic, input_ts, PJ::Span(f, 1));
    if (!st) return PJ::unexpected(st.error());

    runtimeHost().notifyDataChanged();   // ONE call per logical operation, after success
    return PJ::okStatus();
  }

 private:
  std::string config_ = "{}";
};
}  // namespace

PJ_TOOLBOX_PLUGIN(MyToolbox,
    R"({"id":"my-toolbox","name":"My Toolbox","version":"1.0.0"})")
```

## Host services

- **`toolboxHost()`** — data plane: `catalogSnapshot()` (enumerate
  sources/topics/fields), `readSeriesArrow(field, &schema, &array)` (read a series),
  `createDataSource()`, `ensureTopic()`, `appendRecord()` / `appendArrowStream()`
  (write), plus **object writes** (`registerObjectTopic()`, `pushOwnedObject()`).
- **`objectReadHost()`** — **object reads** live on a separate, *optional* service:
  it returns a nullable `const ToolboxObjectReadHostView*`. Null-check it before
  reading ObjectStore data.
- **`runtimeHost()`** — control plane: `notifyDataChanged()`, `reportMessage()`.

## Reading a series (Arrow)

```cpp
#include <pj_base/sdk/arrow.hpp>

PJ::sdk::ArrowSchemaHolder schema;
PJ::sdk::ArrowArrayHolder  array;
auto st = toolboxHost().readSeriesArrow(field, schema.out(), array.out());
if (st) {
  // Two columns: children[0] = int64 timestamp (ns); children[1] = the field's
  // value column, typed per field (commonly float64). Read while the holders are
  // alive; they release at scope exit.
}
```

For bulk output build an `ArrowArrayStream` (e.g. via nanoarrow) and write it once
with `appendArrowStream(topic, std::move(streamHolder), "timestamp")` — far faster
than row-by-row `appendRecord`.

## Traps specific to Toolbox

- **Coalesce `notifyDataChanged()` yourself** — the host does not promise to.
  Call it **once per logical operation**, not per row. Forgetting it entirely
  means your new series never appear in the UI; calling it per record floods the
  host.
- **Host methods only from the host thread** (SKILL.md rule 2). Background work
  must marshal back.
- **Arrow RAII, no dangling pointers** (SKILL.md rule 6). Never keep a raw
  `ArrowSchema*`/`ArrowArray*` past its holder's scope.
- **A `catalogSnapshot()` is a point-in-time view.** After you write and call
  `notifyDataChanged()`, re-acquire the snapshot if you need to see your own writes.
- **Object writes are tail-slot-gated.** `registerObjectTopic()` /
  `pushOwnedObject()` may be absent on an older host; check the returned
  `Expected`/`Status` and degrade gracefully.

## Emitting objects (images, clouds, …)

`registerObjectTopic(source, name, metadata_json)` then
`pushOwnedObject(topic, ts, serialized_bytes)` — serialize with the matching
builtin codec first. See `references/builtin-objects.md` and `V4_STORE.md`.

## Embedding a dialog

Return `PJ::kToolboxCapabilityHasDialog` from `capabilities()`, keep the dialog as a
member, and expose it via `getDialog()` returning `PJ::borrowDialog(dialog_)`. Emit
`PJ_TOOLBOX_PLUGIN` and `PJ_DIALOG_PLUGIN` in the same file. For a persistent
(non-modal) panel rather than a pop-up, also OR in
`PJ::kToolboxCapabilityNonModalDialog` — and note that non-modal panels ignore
`requestAccept()`; wire "Apply/Save" through an owner-installed callback instead.
See `references/dialog.md`.

## Testing

`pj_plugins/testing/toolbox_test_store.hpp` is an in-memory store speaking the
toolbox host ABI (including the Arrow read path) so you can unit-test reads/writes
without a host.
