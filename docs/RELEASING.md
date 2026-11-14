# Versioning and releasing the SDK

What a change must record so that its release is correct, and how a release or
a development pre-release is cut. The bump table is in the root
[CLAUDE.md](../CLAUDE.md#versioning); the ABI rules are in
[ABI evolution rules](../pj_plugins/docs/ARCHITECTURE.md#0a-abi-stability-and-evolution-rules-v5).

## ABI and API changes

- Tail-append optional slots and gate them by `struct_size`; keep the minimum
  vtable sizes frozen. For a MINOR, `abidiff` must show additions only.
- A member change in a builtin object struct (`pj_base/include/pj_base/builtin/`)
  is a MAJOR while PlotJuggler reads parser objects through the direct C++
  bridge. `abidiff` does not see it.
- `pj_base/abi/baseline.abi` is refreshed (target `abi_update_baseline`) only for
  an intentional MAJOR break. It stays at the last MAJOR, so the ABI check
  reports every addition since. The check runs in Linux CI; to run it locally,
  configure with `-DPJ_ENABLE_ABI_CHECK=ON` (needs libabigail's `abidw` and
  `abidiff`) and build the `abi_check` target.

## Host-contract floors

The SDK version is **build identity only**: it names what a plugin compiled
against, never what host it runs on. `pj_base` and `pj_source` are static
libraries compiled into the plugin, so a release that only adds client-side
helpers does not change runtime host compatibility and never adds a floor.

A host-contract surface does. The checker inventories five kinds from the
protocol headers (`tools/feature_floors/extract_host_surfaces.py`):

- host-vtable tail slots;
- versioned host-served service ids (`pj.<name>.v<N>`);
- `PJ_*_FLAG*` wire flags;
- dialog host capabilities (`PJ_dialog_host_capability_t` members);
- builtin object types (`PJ_builtin_object_type_t` members), since an older
  host cannot decode a type it does not know.

A PR that adds one classifies it in `pj_base/feature_floors.json` and refreshes
the snapshot with `python3 tools/feature_floors/extract_host_surfaces.py --write`.
The ctest `feature_floors_check` (`tools/feature_floors/check_feature_floors.py`)
fails on an unclassified surface. It is registered only when CMake finds
Python 3.

The plugin compatibility contract:

- `min_sdk_required` is the functioning floor and the only admission gate.
- `suggested_sdk_version` is the full-feature floor, informational. A plugin
  that declares it also names a `floor_test`.
- `abi_major` marks hard breaks. `PJ_ABI_VERSION` is the orthogonal hard-break
  counter.
- `min_plotjuggler_version` is deprecated. The SDK still decodes it, and
  PlotJuggler shows it but never gates on it.

`feature_floors.json` and the plugin-side checker `feature_floor_check.py` are
installed in `share/plotjuggler_sdk/`, so plugin repositories check their own
manifests against the same table.

## Changelog

Entries go under `## [X.Y.Z] — Unreleased`. The floor checker finds that section
by the word "Unreleased", so keep it in the heading until the release, then
drop ` — Unreleased`.

Every release section carries one `Host contract:` line: either
`unchanged (no floor impact)` or `extended: <identifiers> (floor X.Y.Z)`. The
floor checker requires the `extended` form whenever a surface newer than the
last released version exists.

## Consumer pins

Consumers pin `plotjuggler_sdk/[>=X.Y.Z <(X+1).0.0]` explicitly: the lower
bound is the newest feature actually used, the upper bound the next MAJOR. On
the `0.x` line that is `[>=0.Y.Z <1.0.0]`; no `0.x` release breaks an earlier one.

## Version sources

`VERSION` is the only version to edit for a release. CMake, Conan and conda
derive theirs from it, and the release tag must agree. The conan-center-index
recipe staged under `packaging/` lists its versions by hand and is not part of
the build.

## Development pre-releases

In-progress SDK work that PlotJuggler needs before its contract is complete
ships as `X.Y.Z-dev.N` (`VERSION` and the tag `vX.Y.Z-dev.N` agree), not as a
public release. The real `X.Y.Z` is cut once, when the contract is complete.

- **Publishes:** the tag runs `release.yml` like any release (same build matrix,
  JFrog upload) and creates a GitHub release marked pre-release;
  `conda-release.yml` skips it. Consumers pin it exactly
  (`plotjuggler_sdk/X.Y.Z-dev.N`): Conan version ranges skip pre-releases unless
  told to resolve them.
- **No ABI promise between two dev builds.** The MINOR/MAJOR rules bind only the
  final `X.Y.Z`.
- **Names the final version:** `@since`, `feature_floors.json` and the changelog
  `Host contract:` floor say `X.Y.Z`, never `X.Y.Z-dev.N`.
- **Ordering is SemVer:** `X.Y.Z-dev.1 < X.Y.Z-dev.2 < X.Y.Z`, so a host built
  from `X.Y.Z-dev.1` rejects a plugin whose `min_sdk_required` is `X.Y.Z`; a
  plugin made for a dev host declares `X.Y.Z-dev.N`. `PJ_SDK_VERSION_*` and the
  CMake package version carry `X.Y.Z`; `PJ::sdkVersion()` keeps the full string,
  so gate admission with `PJ::SemVer`, not the macros.

## Cutting a release

Tagging or pushing a release requires explicit user authorization.

1. `VERSION` holds the release version; the changelog section is renamed from
   `## [X.Y.Z] — Unreleased` to `## [X.Y.Z]` and carries its `Host contract:` line.
2. Push the tag `vX.Y.Z` (or `vX.Y.Z-dev.N`). `release.yml` builds every
   platform, uploads to the JFrog Conan remote and creates the GitHub release;
   `conda-release.yml` publishes the conda package for a final release.
