# pj_base — SDK vocabulary, builtin object schemas, and the C plugin ABI

Before adding a helper, use [Existing SDK utilities](../docs/sdk-utilities.md).
Provider work also follows [the provider contract](../docs/provider-guide.md).

`pj_base` owns the vocabulary, the builtin object schemas and codecs, the
DataSource/MessageParser/Toolbox C protocols and the DataSource/Toolbox C++
bases. The `pj_source` provider library (`plotjuggler_sdk::source`) is built
from this directory too. The Dialog protocol and the MessageParser/Dialog/Widget
bases live in `pj_plugins`.

`pj_base` has zero public library dependencies. fast_float and fmt are private
build details. It must not depend on Qt, `pj_datastore` or `pj_plugins`.

## Layout
- `include/pj_base/` — vocabulary primitives: `types.hpp`, `time.hpp` (absolute
  time spine: `Timepoint`/`Duration` + `fromRaw`/`toRaw`), `time_math.hpp`,
  `time_format.hpp`, `slider_window.hpp`, `type_tree.hpp`, `dataset.hpp`,
  `expected.hpp`, `span.hpp`, `number_parse.hpp`, `assert.hpp`,
  `diagnostic_sink.hpp`, `buffer_anchor.hpp`.
- `include/pj_base/*_protocol.h`, `plugin_data_api.h`, `builtin_object_abi.h`,
  `plugin_abi_export.hpp` — the stable C-ABI surface (plugin families, host
  services, descriptor import, parser route claims and functional extension).
- `include/pj_base/builtin/` — one struct header (`*.hpp`) and one wire codec
  (`*_codec.hpp`) per builtin type, the tagged type-erased `BuiltinObject`
  holder and codec dispatcher, and the compile-time field tables
  (`field_table.hpp`, one `<type>_fields.hpp` per tabled struct).
- `include/pj_base/sdk/` — C++ SDK over the ABI: DataSource + Toolbox
  `*_plugin_base.hpp`, `service_registry.hpp`/`service_traits.hpp`, host views,
  Arrow RAII holders, `semver.hpp`, `testing/`. `sdk/version.hpp` is generated
  from `cmake/version.hpp.in`.
- `include/pj_base/sdk/source/` — `pj_source` provider helpers; see
  `../docs/provider-guide.md`.
- `proto/pj/` — canonical `.proto` wire contracts for the builtin types (see its README).
- `feature_floors.json` — the release that introduced each host-contract
  surface; see [Versioning and releasing](../docs/RELEASING.md#host-contract-floors).
- `abi/baseline.abi` — golden libabigail dump; the ABI-stability regression baseline.
- `src/`, `tests/` — implementations and gtests.

## Gotchas
- ABI numbering is **frozen**. `BuiltinObjectType` (builtin_object.hpp) and
  `PJ_builtin_object_type_t` (builtin_object_abi.h) share stable numeric values.
  Never renumber. Types 2 and 12 are permanently reserved. Append only, and
  classify each new type in `feature_floors.json`.
- Every vtable slot is `PJ_NOEXCEPT`. A throw across the ABI boundary calls
  `std::terminate`. See the header block in `plugin_data_api.h`.
- `BuiltinObject` stores its `BuiltinObjectType` tag next to the opaque value.
  It deliberately avoids `std::variant` for forward compatibility and uses no
  RTTI. Recover via `obj.get<T>()` / `sdk::typeOf`. See `builtin/builtin_object.hpp`.
- A builtin struct member change is a MAJOR that `abidiff` does not see; follow
  [Versioning](../CLAUDE.md#versioning) for any API/ABI change.

## Read deeper
| For | Read |
|---|---|
| Existing numeric/time/source helpers and testing support | [SDK utilities](../docs/sdk-utilities.md) |
| Building a source provider | [Provider contract](../docs/provider-guide.md) |
| Builtin type design, serialization families, type-erasure rules | `../docs/builtin_type.md` |
| ImageAnnotations / PlotMarkers wire formats | `../docs/image_annotations_format.md`, `../docs/plot_markers_format.md` |
| ObjectStore services | `../docs/object_store.md` |
| C++ style / error-handling (`Expected`, `PJ_ASSERT`) | `../docs/cpp_design_recommendations.md` |
| Per-family ABI driving order + thread tags | `include/pj_base/data_source_protocol.h`, `message_parser_protocol.h`, `toolbox_protocol.h` |
| Writing a DataSource plugin | `include/pj_base/sdk/data_source_patterns.hpp` (start here) |
| Plugin families overview, host loaders | `../pj_plugins/docs/` |
| Wire `.proto` contracts | `proto/pj/README.md` |
