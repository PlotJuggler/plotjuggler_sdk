---
name: plotjuggler-plugin
description: >-
  Write or modify a PlotJuggler plugin (DataSource, MessageParser, Toolbox, or
  Dialog) using the plotjuggler_sdk C++20 SDK. Use this whenever the task is to
  implement, extend, debug, or build a PlotJuggler plugin — importing a file
  format, streaming live data, parsing message payloads, adding a data-processing
  toolbox, or building a plugin configuration dialog — even if the user does not
  say the word "plugin" (e.g. "add a CSV loader to PlotJuggler", "make PJ read my
  MCAP", "parse my protobuf topic", "a dialog for my source"). It gives the fast
  path (which base class, which header, which macro), the correct CMake for both
  in-tree and installed-SDK builds, and the load-bearing ABI/lifetime rules that
  are easy to get wrong. Do NOT use it for changing the SDK's own ABI/protocol
  (that is a maintainer task with different rules — see the note below).
---

# Writing a PlotJuggler plugin

PlotJuggler is extended by four **plugin families**, each a small C++20 class you
subclass and export from a shared library (`.so`/`.dll`/`.dylib`). The SDK
(`plotjuggler_sdk`) gives you a base class per family; a one-line macro emits all
the C-ABI plumbing (entry point, version symbol, exception trampolines, symbol
folding) so **you never touch the raw ABI**. Your job is to override a handful of
virtual methods and ship the library.

This skill is the author-oriented fast path. The repo's `pj_plugins/docs/` guides
are the full reference; this steers you to the right one and front-loads the
things that silently break a plugin.

## Are you writing a plugin, or changing the SDK?

- **Writing a plugin *against* the SDK** → this skill. The macros handle ABI
  versioning, tail slots, `struct_size`, weak linkage, exception safety. You do
  **not** need to read the ABI-stability rules.
- **Changing the SDK's own ABI/protocol** (adding vtable slots, editing a struct,
  bumping a `PJ_*_PROTOCOL_VERSION`, touching `abi/baseline.abi`) → this is a
  **maintainer** task. Stop and follow `pj_plugins/docs/ARCHITECTURE.md` §0a and
  the "Release Versioning" contract in the root `CLAUDE.md`. Different rules apply.

## Step 0 — How do you get the SDK?

A plugin author obtains `plotjuggler_sdk` one of three ways. **All three end at the
same link line** — `target_link_libraries(my_plugin PRIVATE
plotjuggler_sdk::plugin_sdk)` — only the acquisition step differs:

- **Conan package** (`plotjuggler_sdk`) — the common external route; the official
  plugins use it. Pin the compatibility range explicitly:
  `plotjuggler_sdk/[>=X.Y.Z <(X+1).0.0]`, where `X.Y.Z` is the version that
  introduced the newest SDK feature you actually use (pre-1.0 the upper bound is
  `<1.0.0` — the SDK guarantees no breaks within `0.x`). Tip from the official
  repo: keep the version in a single `SDK_VERSION` file your `conanfile.py` reads.
  Then `find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk)`.
- **pixi / conda package** (`plotjuggler_sdk`) — same `find_package` + target once
  the package is in your environment.
- **git submodule / vendored source** — `add_subdirectory(plotjuggler_sdk)`; the
  same `plotjuggler_sdk::plugin_sdk` alias exists in-tree, and
  `pj_emit_plugin_manifest` becomes available too, so the CMake below is identical.
  (Editing the SDK repo itself is this world: the raw umbrella target is
  `pj_plugin_sdk`; bare `pj_base` lacks the MessageParser and dialog SDK headers,
  so prefer the umbrella. The `pj_plugins/docs/*-guide.md` snippets show
  per-piece in-tree targets.)

## Step 1 — Pick the family

| Your goal | Family | Read |
|---|---|---|
| Turn a **file** or a **live source** (socket, serial, hardware) into topics | **DataSource** | `references/data-source.md` |
| Decode a **byte payload** on a topic into named fields (JSON, protobuf, ROS, custom) | **MessageParser** | `references/message-parser.md` |
| A **tool** that reads existing data, transforms it, and writes new topics | **Toolbox** | `references/toolbox.md` |
| A **configuration UI** (for a source/parser/toolbox, or standalone) | **Dialog** | `references/dialog.md` |

A single `.so` can host **more than one family** — the supported pattern is an
owner + its embedded Dialog (one `PJ_*_PLUGIN(...)` macro per family at file
scope). Don't ship *unrelated* families in one DSO: catalog discovery reads one
primary family descriptor per library.

If your data is object-like (image, point cloud, occupancy grid, transforms,
markers…) rather than scalar time series, also read `references/builtin-objects.md`
**before** writing — it changes what you emit and how.

## Step 2 — The fast path per family

Subclass the base, override the minimum, register with the macro. The macro's
second argument is the **manifest JSON literal** (see Step 3).

| Family | Subclass | `#include` | Override (minimum) | Register |
|---|---|---|---|---|
| DataSource — file import | `PJ::FileSourceBase` | `pj_base/sdk/data_source_patterns.hpp` | `extraCapabilities()`, `importData()` | `PJ_DATA_SOURCE_PLUGIN(C, m)` |
| DataSource — live stream | `PJ::StreamSourceBase` | `pj_base/sdk/data_source_patterns.hpp` | `extraCapabilities()`, `onStart/onPoll/onStop()` | `PJ_DATA_SOURCE_PLUGIN(C, m)` |
| DataSource — manual | `PJ::DataSourcePluginBase` | `pj_base/sdk/data_source_plugin_base.hpp` | `capabilities()`, `start/stop()`, `currentState()` | `PJ_DATA_SOURCE_PLUGIN(C, m)` |
| MessageParser | `PJ::MessageParserPluginBase` | ⚠ `pj_plugins/sdk/message_parser_plugin_base.hpp` | `registerSchemaHandler()` in ctor/`bindSchema()` | `PJ_MESSAGE_PARSER_PLUGIN(C, m)` |
| Toolbox | `PJ::ToolboxPluginBase` | `pj_base/sdk/toolbox_plugin_base.hpp` | `capabilities()` (+ data ops) | `PJ_TOOLBOX_PLUGIN(C, m)` |
| Dialog | `PJ::DialogPluginTyped` | `pj_plugins/sdk/dialog_plugin_typed.hpp` + `.../widget_data.hpp` | `manifest()`, `ui_content()`, `widget_data()`, event handlers | `PJ_DIALOG_PLUGIN(C, m)` |

> ⚠ **Header-location trap.** Three base classes live under `pj_base/sdk/`, but
> `MessageParserPluginBase` lives under **`pj_plugins/sdk/`**, and the Dialog SDK
> lives under `pj_plugins/sdk/` too (installed there from the `dialog_protocol`
> module). Some in-repo docs still show the parser header under `pj_base/` — that
> is stale; use the paths in the table.

The quickest correct start is to copy a working example from this repo and edit it:
`examples/sdk_consumer/` (minimal external DataSource with the full CMake),
`pj_plugins/examples/mock_*.cpp` (DataSource, parser, toolbox, source+dialog), and
`pj_plugins/dialog_protocol/examples/mock_dialog.cpp` (standalone dialog). Each
family reference embeds an API skeleton for when the repo isn't at hand (domain
placeholders like `openSocket()` are yours to fill in).

## Step 3 — Build it

One CMakeLists.txt serves every acquisition channel from Step 0:

```cmake
cmake_minimum_required(VERSION 3.22)
project(my_plugin LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan / pixi / installed package:
find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk)
# Submodule / vendored source instead:  add_subdirectory(plotjuggler_sdk)

add_library(my_plugin SHARED my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE plotjuggler_sdk::plugin_sdk)  # same in all worlds

pj_emit_plugin_manifest(my_plugin
  FAMILY        data_source     # data_source | message_parser | toolbox | dialog
  MANIFEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
)
```

The single `plotjuggler_sdk::plugin_sdk` component is the whole author surface —
base + parser SDK + dialog SDK. You do **not** link a separate dialog target
downstream (`pj_dialog_sdk` is an in-tree name).

**Manifest — two things named "manifest", only one is authoritative:**

1. **The JSON literal you pass to `PJ_*_PLUGIN(Class, "…")`** is embedded in the
   library and is the **source of truth** the host reads at discovery/load.
   Required keys: `id`, `name`, `version`. Family extras: MessageParser **must**
   include `"encoding": ["json", …]` (the host routes payloads by these names, case-
   sensitive); DataSource may add `"file_extensions": [".csv"]`.
2. **`manifest.json` + `pj_emit_plugin_manifest(...)`** is **optional but
   recommended**. It writes a human-readable `<target>.pjmanifest.json` sidecar
   (for tooling/packaging — runtime discovery does *not* read it) **and**, more
   importantly, applies the symbol-isolation settings that stop your plugin's
   symbols from clashing with the host's (hidden visibility everywhere;
   `-Wl,-Bsymbolic-functions` on Linux/ELF — macOS's two-level namespace and
   Windows' export model give the equivalent natively). Keep its `id/name/version` in sync with the macro literal — best by
   making `manifest.json` the single source: generate a constexpr header from it at
   build time and pass that to the macro (the official plugins do this with a small
   CMake embed helper; see `pj-official-plugins/cmake/EmbedManifest.cmake` for a
   ready-made one), instead of maintaining two hand-written copies.

## Step 4 — The rules that silently break a plugin

These cut across all families. The family references add family-specific traps
(dialog `buttonBox` naming, parser topic-scoping, toolbox `notifyDataChanged`
coalescing, etc.) — read them.

1. **Never let an exception cross the ABI boundary.** Return
   `PJ::unexpected("clear reason")` / `PJ::okStatus()` from fallible methods. The
   macro's trampolines are only a safety net: fallible entry points convert a
   stray exception's `what()` into the ABI error, non-fallible ones can only
   swallow it, and an exception escaping a `noexcept` slot terminates the
   process. Explicit `Status` returns are the only predictable diagnostics.
2. **Keep the data plane on the host's callback thread.** `writeHost()`,
   `toolboxHost()`, and `pushMessage()` are single-threaded — a background I/O
   thread must buffer under a mutex and hand data off inside `onPoll()`/`poll()`.
   The authoritative source is the thread tag on each slot in the protocol
   headers: slots tagged `[thread-safe]` — `reportMessage()`,
   `isStopRequested()`, `notifyState()`, `requestStop()`, and the toolbox's
   `notifyDataChanged()` — may be called from any thread (e.g. reporting a
   connection loss from an I/O callback). Everything else stays on the callback
   thread.
3. **Timestamps are `int64` nanoseconds since the Unix epoch.** Not seconds, not
   row indices. Extract or synthesize a real absolute nanosecond value. Use
   `std::chrono::system_clock` for receive-time stamps — **never**
   `high_resolution_clock`, which is since-boot on some platforms. (Only data with
   genuinely no time axis — e.g. a CSV with no time column — may fall back to a
   documented synthetic index-as-time mode.)
4. **`saveConfig()`/`loadConfig()` must be a complete, deterministic round-trip**
   with no ambient state (no `QSettings`, no cwd assumptions). Persist every
   field. Concretely: a DataSource must run `loadConfig(saved) → start()`
   headless with no dialog; a parser must decode from its config alone; dialogs
   and toolboxes restore from the layout *before data arrives* — so tolerate
   configs from older plugin versions (migrate or default missing keys), don't
   *fail* `loadConfig()` just because referenced data isn't loaded yet, and
   re-resolve in `onDataChanged()`.
5. **String lifetimes across the ABI.** SDK overrides return `std::string` — the
   base class buffers it for the ABI, one buffer per slot, invalidated at the
   next call of the *same* method on the same instance. So never hold the host's
   view of a previous return, and if you ever hand out a raw `const char*`
   yourself, it must stay valid until your next call.
6. **Arrow buffers use RAII holders.** Wrap read out-params in
   `PJ::sdk::ArrowSchemaHolder` / `ArrowArrayHolder`; wrap write streams in
   `PJ::sdk::ArrowStreamHolder`. On a **successful** append the host takes
   ownership (the holder disarms); on **failure** you still own it (the holder
   releases). Never release twice.
7. **Guard host access.** Check `writeHostBound()` / `runtimeHostBound()` (or the
   toolbox equivalents) before using a host view.
8. **Object-like data → a builtin type + its codec.** Images, point clouds,
   grids, transforms, markers go through the builtin object vocabulary, not raw
   scalar fields. See `references/builtin-objects.md`.

## Verify

- Builds clean and produces a shared library.
- `PJ_*_PLUGIN` macro present exactly once per family; manifest literal has the
  required keys (+ `encoding` for a parser).
- Load it in PlotJuggler, or better: a unit test that `dlopen`s the **real built
  `.so`** through the host loaders (`DataSourceLibrary::load(path)`,
  `MessageParserLibrary::load(path)`, … — inject the path as a compile
  definition), binds the SDK test helpers
  (`pj_base/sdk/testing/parser_write_recorder.hpp`,
  `pj_plugins/testing/toolbox_test_store.hpp`) as host services, and asserts on
  what was written. This exercises the exact ABI surface the host uses.
- Round-trip `saveConfig()` → `loadConfig()` → run with no dialog.

## Deep-dive routing

| Question | Doc |
|---|---|
| What each family may do; capabilities; permission matrix; config contract | `pj_plugins/docs/REQUIREMENTS.md` |
| How the C ABI / loaders / host bridge work (mostly maintainer detail) | `pj_plugins/docs/ARCHITECTURE.md` |
| Writing each family, in depth | `pj_plugins/docs/{data-source,message-parser,toolbox,dialog-plugin}-guide.md` |
| Dialog `WidgetData` setters + event-handler signatures | `docs/dialog-sdk-reference.md` |
| Builtin object types + their wire codecs | `docs/builtin_type.md`, `pj_base/include/pj_base/builtin/` |
| Object store: publish/read objects, ownership, lazy fetch | `V4_STORE.md` |
| C++ style, error handling, `PJ::Expected<T>`, naming | `docs/cpp_design_recommendations.md` |
