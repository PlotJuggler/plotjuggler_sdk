# PlotJuggler SDK — agent entry point

C++20 plugin SDK and host-loading libraries. PJ4 and plugin repositories consume
the SDK as a Conan package or source checkout; SDK changes belong in this repo.

## Read path

Before implementing a helper, consult [Existing SDK utilities](docs/sdk-utilities.md)
and read the matching header. If no entry matches, search the public headers and
`cmake/` before adding an implementation. Then read the relevant module
instructions and task-specific guide. Source providers must also follow
[the provider contract](docs/provider-guide.md).

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
| ObjectStore services and ownership | [V4_STORE.md](V4_STORE.md) |
| Dialog setters/events | [docs/dialog-sdk-reference.md](docs/dialog-sdk-reference.md) |
| Image annotation / plot marker wire formats | [image_annotations_format.md](docs/image_annotations_format.md), [plot_markers_format.md](docs/plot_markers_format.md) |
| C++ style and error handling | [docs/cpp_design_recommendations.md](docs/cpp_design_recommendations.md) |
| Release planning and consumer migration gates | [docs/BACKLOG.md](docs/BACKLOG.md) |

`pj_source` (`plotjuggler_sdk::source`, `PJ::sdk::source`) and `pj_plugins`
depend on `pj_base`; both use nlohmann/json. Provider headers live under
`pj_base/sdk/source/`. The pre-0.31 `descriptor_import_support` component and
include directory forward to `source` for one release; new code uses `source`.

DataSource/MessageParser/Toolbox C protocols live in `pj_base`; the Dialog
protocol lives in `pj_plugins/dialog_protocol/`. The datastore and duplicate
plugin-resolution catalog belong to the application, not this SDK.

The installed `cmake/` helpers (`pj_configure_plugin`, `pj_embed_file`,
`pj_harden_plugin_exports`, `pj_add_parser_module`, `pj_add_sdk_test_fixture`)
are public API and follow the same versioning contract as headers.

## Build and test

```bash
./build.sh            # RelWithDebInfo
./build.sh --debug    # Debug + ASAN
./test.sh             # all discovered build directories
./test_sdk_install.sh # installed-package consumer
```

Dependencies come from `conanfile.py`. Before committing, run
`./build.sh --debug && ./test.sh` and pre-commit (clang-format 22.1.0).
Packaging changes also require `./test_sdk_install.sh`.

## Release Versioning

Propose a release only according to plugin impact:

| Change | Version |
|---|---|
| ABI/API break requiring consumer recompilation or source changes, protocol/layout change, or canonical wire-schema break | MAJOR |
| Backward-compatible API/capability addition; existing binaries work without recompilation | MINOR |
| Consumer-visible bug fix | PATCH |
| Docs, comments, tests or implementation changes invisible to consumers | No bump |

Tail-append optional slots and gate them by `struct_size`; keep minimum vtable
sizes frozen. `abidiff` must show additions only for a MINOR.
`pj_base/abi/baseline.abi` is refreshed only for an intentional MAJOR break.
See [ABI evolution rules](pj_plugins/docs/ARCHITECTURE.md#0a-abi-stability-and-evolution-rules-v5).

Consumers pin `plotjuggler_sdk/[>=X.Y.Z <(X+1).0.0]`, explicitly: the lower
bound is the newest feature actually used, the upper bound the next MAJOR.
Pre-1.0 has the same no-breaks guarantee: `[>=0.Y.Z <1.0.0]`.

`VERSION` is the only hand-maintained version; Conan, CMake and conda derive
theirs from it. Release tags must agree. Tagging or pushing a release requires
explicit user authorization.

## Coding conventions

- Google clang-format style: two spaces, 120-column limit.
- `CamelCase` types, `camelBack` functions, `lower_case` variables,
  `lower_case_` members, `kCamelCase` constants.
- Match module ownership: vocabulary in `PJ`, SDK helpers in `PJ::sdk`,
  provider helpers in `PJ::sdk::source`, standalone parser modules in `pj`.
- Fallible operations use `PJ::Expected<T>` / `PJ::Status`; invariants use
  `PJ_ASSERT`. Warnings: `-Wall -Wextra -Werror`.

## Documentation requests

- "Read all documentation": include every tracked `.md`, including `.claude/`.
- "Update documentation": fix outdated claims and add guidance whose absence
  caused a bug.
- "Check documentation": verify the affected contracts against the code;
  report discrepancies when edits are outside the authorized task.
