# CMake plugin-authoring helpers move into the SDK (0.25.0)

**Status:** approved design (approach "B — move + consolidate", names may change)
**Release:** MINOR → `0.25.0` (new backward-compatible CMake capability; no compiled
change; `abi/baseline.abi` untouched)

## Problem

Three CMake helpers every PlotJuggler plugin needs live in `pj-official-plugins/cmake/`
instead of the SDK:

| Helper | What it does | Callers today |
|---|---|---|
| `pj_embed_manifest` | hex-embeds `manifest.json` into a `constexpr char[]` header for `PJ_*_PLUGIN(Class, kManifest)` | 29 official plugins + 6 PJ4 wasm cmake files |
| `pj_embed_ui` | identical body, for `.ui` XML → `ui_content()` | 25 official plugins + PJ4 wasm |
| `pj_harden_plugin_exports` (+ `CheckElfPluginExports.cmake`, `plugin_exports.map.in`) | version-script allowlist of the ABI entry points + post-build `nm -D` gate against `STB_GNU_UNIQUE` leaks | 28 official plugins |

Consequences of the current boundary:

1. **SDK docs describe helpers the SDK does not ship.** `pj_plugins/docs/dialog-plugin-guide.md`
   ("EmbedUi — external `.ui` files") and the plugin skill point authors at
   `pj-official-plugins/cmake/EmbedManifest.cmake`.
2. **The app reaches across repos:** `PJ4/cmake/PjWasm*.cmake` include
   `${PJ_OFFICIAL_PLUGINS_SOURCE_DIR}/cmake/EmbedManifest.cmake`.
3. **The export allowlist mirrors SDK-owned symbol names** (`pj_plugin_abi_version`,
   `PJ_get_<family>_vtable`) and even pre-declares a `pj_plugin_descriptor_*` that exists nowhere
   in the SDK. When the SDK adds an entry point every downstream copy goes stale and the host
   rejects the plugin at the ABI handshake. `cmake/PjParserModule.cmake` already does this
   correctly in-house for parser modules.
4. **"Make this DSO safe to dlopen" is split across two repos and two functions:**
   `pj_emit_plugin_manifest` (SDK) applies hidden visibility / `-Bsymbolic-functions` / rpath
   and is misnamed (the sidecar is its least important effect); `pj_harden_plugin_exports`
   (plugins) applies the allowlist. They are always called together on the same target.

`CPM.cmake` (vendored third-party bootstrap) and `VendoredPatch.cmake` (patching CPM-fetched
sources) are monorepo build plumbing with nothing PJ-specific. **They stay in
pj-official-plugins.**

## Design

### Public CMake API (shipped with the `plugin_sdk` component)

```cmake
find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk)   # or add_subdirectory(plotjuggler_sdk)

add_library(my_plugin SHARED my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE plotjuggler_sdk::plugin_sdk)

pj_configure_plugin(my_plugin
  FAMILIES        data_source dialog          # required; first is the primary family
  MANIFEST_FILE   manifest.json               # default: ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
  MANIFEST_HEADER generated/my_manifest.hpp   # optional: embed manifest.json as a constexpr header
  MANIFEST_VAR    kMyManifest                 # default kPluginManifest; only with MANIFEST_HEADER
  # ABI_MAJOR <n>                             # sidecar hint; defaults to PJ_ABI_VERSION
  # EXTRA_EXPORTS <symbol>...                 # extra symbols to keep exported (forwarded to the allowlist)
  # NO_EXPORT_HARDENING                       # skip the allowlist + gate (then call pj_harden_plugin_exports yourself)
)

pj_embed_file(my_plugin
  FILE     ui/my_dialog.ui
  HEADER   generated/my_dialog_ui.hpp
  VAR_NAME kMyDialogUi
)
```

**`pj_configure_plugin(<target> FAMILIES <f>... [...])`** — the one call that turns a
`SHARED`/`MODULE` target into a correct PlotJuggler plugin. In order:

1. Validates arguments. `FAMILIES` ⊆ {`data_source`, `message_parser`, `toolbox`, `dialog`},
   at least one. `MANIFEST_FILE` must exist and contain non-empty string `id`, `name`,
   `version` (unchanged validation). `MANIFEST_VAR` without `MANIFEST_HEADER` is an error.
2. Applies symbol isolation and rpath exactly as `pj_emit_plugin_manifest` does today
   (`CXX_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN`, `-Wl,-Bsymbolic-functions` on
   Linux, `$ORIGIN` / `@loader_path` rpath).
3. Writes the sidecar `<target>.pjmanifest.json` (unchanged schema: source manifest +
   `abi_major` + `family`; `family` is the **first** entry of `FAMILIES`), copies it next to
   the DSO post-build, installs it alongside.
4. If `MANIFEST_HEADER` is given: `pj_embed_file(<target> FILE <manifest> HEADER <h> VAR_NAME <v>)`.
5. Unless `NO_EXPORT_HARDENING`:
   `pj_harden_plugin_exports(<target> FAMILIES <families> EXTRA_EXPORTS <extra>)`.

Relative `MANIFEST_FILE` / `MANIFEST_HEADER` paths resolve against
`CMAKE_CURRENT_SOURCE_DIR` / `CMAKE_CURRENT_BINARY_DIR` respectively.

**`pj_embed_file(<target> FILE <path> HEADER <path> VAR_NAME <identifier>)`** — generic
configure-time file → header embed. Emits
`inline constexpr char <VAR_NAME>[] = { 0x.., ..., 0x00 };` (hex bytes: sidesteps MSVC's
16380-char literal limit, C2026, and needs no raw-string delimiter escaping). Tracks `FILE` as a
configure dependency, adds the header's directory as a `PRIVATE` include directory, adds the
header to the target's sources. The header is written **only if its content changed**
(`file(CONFIGURE ...)` semantics) so a reconfigure does not force a recompile of every plugin
TU. Relative `FILE` / `HEADER` resolve as above.

**`pj_harden_plugin_exports(<target> [FAMILIES <f>...] [REQUIRED_EXPORTS <sym>...] [EXTRA_EXPORTS <sym>...])`**
— standalone (kept public: the ros2 inner payload is a plugin with *renamed* entry points and
needs `REQUIRED_EXPORTS`/`EXTRA_EXPORTS` without family derivation). At least one of `FAMILIES`
/ `REQUIRED_EXPORTS`. **Linux/ELF only** (glibc's `STB_GNU_UNIQUE` pinning is the problem being
solved; PE exports nothing without `dllexport`, Mach-O has no unique binding) — returns
silently elsewhere.

- Allowlist (`global:`) = `pj_plugin_abi_version` + **all four** `PJ_get_<family>_vtable`
  (naming an absent symbol is a no-op; keeping all four means an embedded dialog's getter is
  never accidentally localized) + `EXTRA_EXPORTS`. Everything else `local: *`.
  `pj_plugin_descriptor_*` is dropped — the SDK owns this list now and adds names when it
  adds entry points.
- The map is generated into `${CMAKE_CURRENT_BINARY_DIR}/<target>_exports.map` via
  `file(CONFIGURE)` (write-if-different) — no `.map.in` template file to install.
  `LINKER:--version-script=<map>`; `LINK_DEPENDS` on the map and on the checker script.
- Gate (`REQUIRED`) = `pj_plugin_abi_version` + the getters of `FAMILIES` + `REQUIRED_EXPORTS`.
  POST_BUILD: `${CMAKE_COMMAND} -DPLUGIN_SO=$<TARGET_FILE> -DREQUIRED_EXPORTS=a,b
  -DNM_TOOL=${CMAKE_NM} -P PjCheckElfPluginExports.cmake` — fails the build on any
  `STB_GNU_UNIQUE` symbol in `.dynsym` or a missing required export.

**`pj_emit_plugin_manifest(<target> FAMILY <f> [MANIFEST_FILE] [ABI_MAJOR])`** — kept as a
**deprecated alias** with byte-identical behavior (`pj_configure_plugin(... FAMILIES <f>
NO_EXPORT_HARDENING)`), printing one `message(DEPRECATION)` per configure. Removing it would
force existing plugins to edit their CMake, which the versioning contract classifies as MAJOR.

### Files

| Path | Change |
|---|---|
| `cmake/PjPlugin.cmake` | **new** (replaces `cmake/PjPluginManifest.cmake`): `pj_configure_plugin`, `pj_embed_file`, `pj_harden_plugin_exports`, deprecated `pj_emit_plugin_manifest`. `include_guard(GLOBAL)`. |
| `cmake/PjCheckElfPluginExports.cmake` | **new**: script-mode checker (moved from pj-official-plugins, comments updated). Installed next to `PjPlugin.cmake`; located via `CMAKE_CURRENT_FUNCTION_LIST_DIR` so in-tree and installed layouts both work. |
| `cmake/PjPluginManifest.cmake` | **deleted** (nothing includes it by file name outside this repo — verified against PJ4 and pj-official-plugins). |
| `CMakeLists.txt` | `include(PjPlugin)` in-tree; `install(FILES cmake/PjPlugin.cmake cmake/PjCheckElfPluginExports.cmake cmake/PjParserModule.cmake ...)`. |
| `cmake/plotjuggler_sdkConfig.cmake.in` | `plugin_sdk` component includes `PjPlugin.cmake`. |
| `conanfile.py` | `cmake_build_modules` → `PjPlugin.cmake`, `PjParserModule.cmake`. |
| `recipe.yaml` | package-contents check lists `PjPlugin.cmake`, `PjCheckElfPluginExports.cmake`, `PjParserModule.cmake`. |
| `packaging/conan-center-index/...` | untouched — it packages the submitted 0.20.0 sources. |
| `VERSION`, `CHANGELOG.md` | `0.25.0` + entry. |
| `.gitignore` | `.worktrees/` (worktree policy in the global CLAUDE.md; currently shows as untracked). |

### Tests (Linux-specific parts guarded; everything else cross-platform)

In `pj_plugins/CMakeLists.txt` under `PJ_BUILD_TESTS`:

- **`cmake_helpers_fixture_plugin`** — a DataSource + embedded Dialog plugin
  (`pj_plugins/tests/cmake_helpers_fixture_plugin.cpp`) configured with
  `pj_configure_plugin(... FAMILIES data_source dialog MANIFEST_HEADER ... MANIFEST_VAR
  kFixtureManifest)`; its `ui_content()` comes from `tests/cmake_helpers_fixture/dialog.ui`
  via `pj_embed_file`. It also defines
  `extern "C" __attribute__((visibility("default"))) int pj_cmake_fixture_probe()` — a
  default-visibility symbol only the version script can localize. Building it runs the
  post-build gate for real.
- **`cmake_helpers_leaky_plugin`** (Linux + GCC only) — same probe symbol plus a
  vague-linkage template static that GCC emits as `STB_GNU_UNIQUE`, built **without**
  hardening.
- **`plugin_cmake_helpers_test`** (gtest):
  1. `DataSourceLibrary::load(fixture)` succeeds; `handle->manifest()` equals the bytes of
     `manifest.json` (path via compile definition) — proves the manifest embed round-trips
     through the real ABI.
  2. `DialogLibrary::load(fixture)` → `createHandle().ui_content()` equals the bytes of
     `dialog.ui` — proves `pj_embed_file`.
  3. The sidecar next to the DSO parses; `family == "data_source"`,
     `abi_major == PJ_ABI_VERSION`, `id` matches the source manifest.
  4. (Linux) `dlsym(fixture, "pj_cmake_fixture_probe")` is null (localized) while
     `dlsym(leaky, ...)` is non-null — proves the allowlist is applied.
- **CTest script-mode checks** (Linux):
  `cmake_helpers_gate_accepts_fixture` (exit 0),
  `cmake_helpers_gate_rejects_missing_export` (`PASS_REGULAR_EXPRESSION "required export
  \"PJ_get_toolbox_vtable\" is missing"`),
  `cmake_helpers_gate_rejects_unique_symbol` (GCC; `PASS_REGULAR_EXPRESSION "STB_GNU_UNIQUE"`).

`examples/sdk_consumer` (built against the *installed* package by the release workflow and the
conda recipe) switches to `pj_configure_plugin` with `MANIFEST_HEADER`, and
`dialog_controls.cpp`'s inline XML moves to `dialog_controls.ui` embedded with
`pj_embed_file` — so the installed-package path exercises every helper.

### Documentation

- `pj_plugins/docs/dialog-plugin-guide.md`: "EmbedUi" section → `pj_embed_file`.
- `.claude/skills/plotjuggler-plugin/SKILL.md` (Step 0, CMake snippet, manifest section,
  Verify) and `references/dialog.md`: `pj_configure_plugin` / `pj_embed_file`; drop the pointer
  to pj-official-plugins.
- `pj_plugins/docs/ARCHITECTURE.md` loader notes: symbol isolation is provided by
  `pj_configure_plugin` (hidden visibility + `-Bsymbolic-functions` + version-script
  allowlist), not "left to `-fvisibility=hidden`".
- Root `CLAUDE.md` module list + `README.md`: name the shipped CMake module.
- `CHANGELOG.md` 0.25.0 entry with the migration table
  (`pj_emit_plugin_manifest` → `pj_configure_plugin`, `pj_embed_ui`/`pj_embed_manifest` →
  `pj_embed_file` / `MANIFEST_HEADER`).

### Out of scope / follow-ups (separate PRs, other repos)

- **pj-official-plugins:** delete `cmake/{EmbedManifest,EmbedUi,HardenPluginExports,CheckElfPluginExports}.cmake`
  + `plugin_exports.map.in`, drop ~54 `include(...)` lines, migrate call sites
  (`pj_embed_ui` → `pj_embed_file`; `pj_embed_manifest` + `pj_emit_plugin_manifest` +
  `pj_harden_plugin_exports` → one `pj_configure_plugin`), bump `SDK_VERSION` to `0.25.0`.
  `data_load_rerun` gains the gate it is currently missing.
- **PJ4:** the six `PjWasm*.cmake` includes switch to the SDK submodule's module.
- **conan-center-index recipe:** picks up the module list at its next version bump.
- macOS `-exported_symbols_list` branch for `pj_harden_plugin_exports` (mirroring
  `PjParserModule.cmake`) — not needed for the problem being solved; can be added later
  without an API change.
