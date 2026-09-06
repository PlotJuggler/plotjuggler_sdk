# pj_base — SDK vocabulary, builtin object schemas, and the C plugin ABI

Before adding a helper, use [Existing SDK utilities](../docs/sdk-utilities.md).
Provider work also follows [the provider contract](../docs/provider-guide.md).

`pj_base` owns vocabulary, builtin schemas/codecs, DataSource/MessageParser/Toolbox
C protocols and the DataSource/Toolbox C++ bases. The Dialog protocol and
MessageParser/Dialog bases live in `pj_plugins`.

`pj_source` headers live here under `sdk/source/`. Link
`plotjuggler_sdk::source` to use them.

`pj_base` has zero public library dependencies. fast_float and fmt are private
build details. It must not depend on Qt, `pj_datastore` or `pj_plugins`.

## Layout
- `include/pj_base/` — vocabulary primitives: `types.hpp`, `time.hpp` (absolute time spine: `Timepoint`/`Duration` + `fromRaw`/`toRaw`), `type_tree.hpp`, `dataset.hpp`, `expected.hpp`, `span.hpp`, `number_parse.hpp`, `assert.hpp`, `diagnostic_sink.hpp`, `buffer_anchor.hpp`.
- `include/pj_base/builtin/` — 18 builtin struct headers (`*.hpp`) and all 18
  wire codecs (`*_codec.hpp`). Numeric tags are stable; values 2 and 12 are
  permanently reserved. Also contains the tagged, type-erased `BuiltinObject`
  holder and type-erased codec dispatcher.
- `include/pj_base/sdk/` — C++ SDK over the ABI: DataSource + Toolbox `*_plugin_base.hpp`, `service_registry.hpp`/`service_traits.hpp`, host views, Arrow RAII holders, `testing/`.
- `include/pj_base/*_protocol.h`, `plugin_data_api.h`, `builtin_object_abi.h`, `plugin_abi_export.hpp` — the stable C-ABI surface for DataSource/MessageParser/Toolbox (the Dialog protocol header lives in `pj_plugins/dialog_protocol/`).
- `proto/pj/` — canonical `.proto` wire contracts for the builtin types (see its README).
- `include/pj_base/sdk/source/` — `pj_source` provider helpers; see `../docs/provider-guide.md`.
- `include/pj_base/time_format.hpp`, `slider_window.hpp` — UTC/duration formatting, checked ISO parsing and slider ranges.
- `src/`, `tests/` — codec/parse impls and gtests.
- `abi/baseline.abi` — golden libabigail dump; the ABI-stability regression baseline.

## Gotchas
- ABI numbering is **frozen**. `BuiltinObjectType` (builtin_object.hpp) and
  `PJ_builtin_object_type_t` (builtin_object_abi.h) share stable numeric values.
  Never renumber. Types 2 and 12 are permanently reserved. Append only.
- Every vtable slot is `PJ_NOEXCEPT`. A throw across the ABI boundary calls
  `std::terminate`. See the header block in `plugin_data_api.h`.
- `BuiltinObject` stores its `BuiltinObjectType` tag next to the opaque value.
  It deliberately avoids `std::variant` for forward compatibility and uses no
  RTTI. Recover via `obj.get<T>()` / `sdk::typeOf`. See `builtin/builtin_object.hpp`.
- Follow [Release Versioning](../CLAUDE.md#release-versioning) for API/ABI changes; a MINOR does not refresh `abi/baseline.abi`.

## Read deeper
| For | Read |
|---|---|
| Existing numeric/time/source helpers and testing support | [SDK utilities](../docs/sdk-utilities.md) |
| Building a source provider | [Provider contract](../docs/provider-guide.md) |
| Builtin type design, serialization families, type-erasure rules | `../docs/builtin_type.md` |
| ImageAnnotations canonical wire format | `../docs/image_annotations_format.md` |
| C++ style / error-handling (`Expected`, `PJ_ASSERT`) | `../docs/cpp_design_recommendations.md` |
| Per-family ABI driving order + thread tags | `include/pj_base/data_source_protocol.h`, `message_parser_protocol.h`, `toolbox_protocol.h` |
| Writing a DataSource plugin | `include/pj_base/sdk/data_source_patterns.hpp` (start here) |
| Plugin families overview, host loaders | `../pj_plugins/docs/` |
| Wire `.proto` contracts | `proto/pj/README.md` |
