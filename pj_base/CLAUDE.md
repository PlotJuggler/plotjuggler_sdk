# pj_base — SDK vocabulary, builtin object schemas, and the C plugin ABI

pj_base is the **Level 0** foundation and the **SDK boundary** for plugin authors. It owns: the zero-dependency vocabulary types (`Timestamp`, `DatasetId`, `Range`, `Expected<T>`, `Span`, `TypeTree`); the canonical *builtin object* schemas (`sdk::Image`, `PointCloud`, `DepthImage`, `OccupancyGrid`, `FrameTransforms`, … — 15 types) **and 14 of their wire codecs** (RobotDescription has none); and the **C ABI** primitives every plugin family speaks (`plugin_data_api.h` + the service registry) plus the C-ABI protocol headers for **three** families — `data_source_protocol.h`, `message_parser_protocol.h`, `toolbox_protocol.h`. The **Dialog** protocol header is the exception: it lives in `pj_plugins/dialog_protocol/`, not here. It also ships the C++ SDK base classes for DataSource and Toolbox; the MessageParser and Dialog base classes live in `pj_plugins`. Builds as a STATIC lib with **zero public deps** — `fast_float` is a `BUILD_INTERFACE` private impl detail of `parseNumber`. Must NOT depend on `pj_datastore`, `pj_plugins`, Qt, or any Conan runtime lib. This is a read-only submodule subtree: change it only when explicitly working in `plotjuggler_core`.

## Layout
- `include/pj_base/` — vocabulary primitives: `types.hpp`, `type_tree.hpp`, `dataset.hpp`, `expected.hpp`, `span.hpp`, `number_parse.hpp`, `assert.hpp`, `diagnostic_sink.hpp`, `buffer_anchor.hpp`.
- `include/pj_base/builtin/` — the 15 builtin object struct headers (`*.hpp`; 16 enum values in `BuiltinObjectType`, value 2 reserved) + their 14 wire codecs (`*_codec.hpp`; RobotDescription has none) + the `BuiltinObject` (`std::any`) type-erased holder.
- `include/pj_base/sdk/` — C++ SDK over the ABI: DataSource + Toolbox `*_plugin_base.hpp`, `service_registry.hpp`/`service_traits.hpp`, host views, Arrow RAII holders, `testing/`.
- `include/pj_base/*_protocol.h`, `plugin_data_api.h`, `builtin_object_abi.h`, `plugin_abi_export.hpp` — the stable C-ABI surface for DataSource/MessageParser/Toolbox (the Dialog protocol header lives in `pj_plugins/dialog_protocol/`).
- `proto/pj/` — canonical `.proto` wire contracts for the builtin types (see its README).
- `src/`, `tests/` — codec/parse impls and gtests.
- `abi/baseline.abi` — golden libabigail dump; the ABI-stability regression baseline.

## Gotchas
- ABI numbering is **frozen**: `BuiltinObjectType` (builtin_object.hpp) and `PJ_builtin_object_type_t` (builtin_object_abi.h) share stable numeric values — never renumber; type 2 is permanently reserved. Append-only.
- Every vtable slot is `PJ_NOEXCEPT`; a throw across the ABI boundary calls `std::terminate`. See the header block in `plugin_data_api.h`.
- `BuiltinObject` is `std::any`, deliberately not `std::variant`, for forward-compat — recover via `std::any_cast<T>` / `sdk::typeOf`. See `builtin/builtin_object.hpp`.
- ABI/struct-layout or signature changes require a Conan **MINOR** bump and a refreshed `abi/baseline.abi`; tail-appended slots are a PATCH (see submodule CLAUDE.md → Release Versioning).

## Read deeper
| For | Read |
|---|---|
| Builtin type design, serialization families, type-erasure rules | `../docs/builtin_type.md` |
| ImageAnnotations canonical wire format | `../docs/image_annotations_format.md` |
| C++ style / error-handling (`Expected`, `PJ_ASSERT`) | `../docs/cpp_design_recommendations.md` |
| Per-family ABI driving order + thread tags | `include/pj_base/data_source_protocol.h`, `message_parser_protocol.h`, `toolbox_protocol.h` |
| Writing a DataSource plugin | `include/pj_base/sdk/data_source_patterns.hpp` (start here) |
| Plugin families overview, host loaders | `../pj_plugins/docs/` |
| Wire `.proto` contracts | `proto/pj/README.md` |
