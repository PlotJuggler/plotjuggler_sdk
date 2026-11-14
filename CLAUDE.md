# PlotJuggler SDK — agent entry point

C++20 plugin SDK and host-loading libraries. PJ4 and plugin repositories consume
the SDK as a Conan package or source checkout; SDK changes belong in this repo.

## Read path

- **Writing a plugin against the SDK:** the
  [plotjuggler-plugin skill](.claude/skills/plotjuggler-plugin/SKILL.md).
- **Changing the SDK:** before implementing a helper, consult
  [Existing SDK utilities](docs/sdk-utilities.md) and read the matching header.
  If no entry matches, search the public headers and `cmake/` before adding an
  implementation. Then read the module instructions and the task's guide from
  the table below. Source providers also follow
  [the provider contract](docs/provider-guide.md).
- **Changing an ABI struct, vtable, service id, builtin type or wire schema:**
  read the [ABI evolution rules](pj_plugins/docs/ARCHITECTURE.md#0a-abi-stability-and-evolution-rules-v5)
  and [Versioning and releasing](docs/RELEASING.md) first.

The metadata query language and `PJ::common::RollingTransferRate` live in
**pj-official-plugins `common/query/` and `common/transfer_rate/`**, respectively.
Reuse those implementations; do not reimplement them here. Their headers and
targets are listed in the utilities index.

Docs define intended contracts; code shows implementation. Resolve disagreements
explicitly and check documentation whenever behavior, APIs, ABI layouts, module
ownership or storage formats change. Read only the relevant reference sections.

## Modules and references

| Task | Start here |
|---|---|
| Vocabulary, numeric/time utilities, builtin objects, C protocols | [pj_base/CLAUDE.md](pj_base/CLAUDE.md) |
| Provider descriptors, origins, artifacts, jobs, limits, completion and Stop | [docs/provider-guide.md](docs/provider-guide.md) |
| Plugin authoring, host loaders, discovery and parser routing | [pj_plugins/CLAUDE.md](pj_plugins/CLAUDE.md) |
| Canonical object types and codecs | [docs/builtin_type.md](docs/builtin_type.md) |
| ObjectStore services and ownership | [docs/object_store.md](docs/object_store.md) |
| Dialog setters/events | [docs/dialog-sdk-reference.md](docs/dialog-sdk-reference.md) |
| Widget plugins (panels with a plugin-created Qt widget) | [pj_plugins/docs/widget-guide.md](pj_plugins/docs/widget-guide.md), [pj_plugins/widget_protocol/CLAUDE.md](pj_plugins/widget_protocol/CLAUDE.md) |
| Image annotation / plot marker wire formats | [image_annotations_format.md](docs/image_annotations_format.md), [plot_markers_format.md](docs/plot_markers_format.md) |
| C++ style and error handling | [docs/cpp_design_recommendations.md](docs/cpp_design_recommendations.md) |
| Versions, host-contract floors, changelog, releases | [docs/RELEASING.md](docs/RELEASING.md) |
| Open SDK items and consumer migration gates | [docs/BACKLOG.md](docs/BACKLOG.md) |

`pj_source` (target `plotjuggler_sdk::source`, namespace `PJ::sdk::source`) is
built from `pj_base/`, with headers under `pj_base/include/pj_base/sdk/source/`.
It and `pj_plugins` depend on `pj_base`; both use nlohmann/json.

DataSource/MessageParser/Toolbox C protocols live in `pj_base`; the Dialog
protocol lives in `pj_plugins/dialog_protocol/` and the Widget protocol in
`pj_plugins/widget_protocol/`. A Widget plugin links Qt; the SDK never does.
The datastore and duplicate plugin-resolution catalog belong to the
application, not this SDK.

The installed `cmake/` helpers (`pj_configure_plugin`, `pj_embed_file`,
`pj_harden_plugin_exports`, `pj_add_sdk_test_fixture`) are public API and follow
the same versioning contract as headers.

`experimental/` holds components that are built and tested here but never
installed or packaged, and that carry no compatibility promise. It currently
holds functional parser modules: see
[experimental/parser_module/README.md](experimental/parser_module/README.md).

## Build and test

```bash
./build.sh            # RelWithDebInfo into build/
./build.sh --debug    # Debug + ASAN into build/debug_asan
./test.sh             # every build directory that exists; takes no arguments
ctest --test-dir build/debug_asan -R <regex> --output-on-failure   # a subset
./test_sdk_install.sh # installed-package consumer
pre-commit run --all-files
```

Dependencies come from `conanfile.py`. Before committing, run
`./build.sh --debug && ./test.sh` and pre-commit (clang-format 22.1.0).
Packaging changes also require `./test_sdk_install.sh`.

CI builds and tests on Linux, Windows (MSVC) and macOS, so module code stays
portable: platform code goes behind `#ifdef` or CMake. Linux CI also runs two
gates `build.sh` does not enable: the ABI drift check (see
[Versioning and releasing](docs/RELEASING.md#abi-and-api-changes)) and the WASI
parser-module conformance (`-DPJ_WASI_SDK_ROOT=<wasi-sdk>`, then `ctest -L wasi`).

## Versioning

Propose a release only according to plugin impact:

| Change | Version |
|---|---|
| ABI/API break requiring consumer recompilation or source changes, protocol/layout change, or canonical wire-schema break | MAJOR |
| Backward-compatible API/capability addition; existing binaries work without recompilation | MINOR |
| Consumer-visible bug fix | PATCH |
| Docs, comments, tests or implementation changes invisible to consumers | No bump |

- A member change in a builtin object struct is a MAJOR even though `abidiff`
  does not see it: PlotJuggler reads parser objects through the direct C++ bridge.
- A new host-contract surface (host-vtable tail slot, service id, wire flag,
  dialog host capability, builtin object type) is classified in
  `pj_base/feature_floors.json` in the same PR; the `feature_floors_check` test
  fails otherwise.
- Changelog entries go under `## [X.Y.Z] — Unreleased`, with one
  `Host contract:` line per release.
- `VERSION` is the only version to edit. In-progress work that PlotJuggler needs
  early ships as a `X.Y.Z-dev.N` pre-release.
- Tagging or pushing a release requires explicit user authorization.

The full rules (floors, plugin compatibility fields, consumer pins,
pre-releases, cutting a release) are in [docs/RELEASING.md](docs/RELEASING.md).

## Coding conventions

- Google clang-format style: two spaces, 120-column limit.
- `CamelCase` types, `camelBack` functions, `lower_case` variables,
  `lower_case_` members, `kCamelCase` constants.
- Match module ownership: vocabulary in `PJ`, SDK helpers in `PJ::sdk`,
  provider helpers in `PJ::sdk::source`; the experimental parser-module
  authoring kit uses `pj`.
- Fallible operations use `PJ::Expected<T>` / `PJ::Status`; invariants use
  `PJ_ASSERT`.
- Warnings are strict (`-Wall -Wextra -Wshadow -Wold-style-cast -Wconversion
  -Wpedantic` and more; `/W4` on MSVC) and errors by default
  (`PJ_WARNINGS_AS_ERRORS`). The root `CMakeLists.txt` holds the list.

## Documentation requests

- "Read all documentation": include every tracked `.md`, including `.claude/`.
- "Update documentation": fix outdated claims and add guidance whose absence
  caused a bug. Docs describe the current state; finished plans and historical
  analyses go to `docs/archive/`, which is not a reference.
- "Check documentation": verify the affected contracts against the code;
  report discrepancies when edits are outside the authorized task.
