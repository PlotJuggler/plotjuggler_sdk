# Changelog

All notable changes to `plotjuggler_sdk` are recorded here. Versioning policy is in
[`CLAUDE.md`](./CLAUDE.md) → "Release Versioning".

## [0.16.0] — Unreleased, on branch `feature/playback-viewport-services`

> Version number is provisional: 0.15.0 is taken by the in-flight lazy-subscription
> work; renumber at release if the two land in a different order.

### Added — host services `pj.playback.v1` and `pj.viewport.v1` (MINOR, additions only)

Two new optional host services so a plugin (first consumer: the Assistant Agent
toolbox) can drive the app like a user — transport and zoom — without any new
executable surface crossing the ABI:

- **`pj.playback.v1`** (`PJ_playback_host_vtable_t`, `sdk::PlaybackHostView`,
  `sdk::PlaybackHostService`): `play` / `pause` / `seek` / `set_playback_rate` /
  `get_state` (ABI-frozen `PJ_playback_state_t` snapshot) / `to_display_time`
  (absolute int64 ns → display-axis seconds, per-topic dataset offset;
  current-frame semantics). All times are display-axis seconds.
- **`pj.viewport.v1`** (`PJ_viewport_host_vtable_t`, `sdk::ViewportHostView`,
  `sdk::ViewportHostService`): `zoom_to_time_range` (every open time plot's X
  window; per-plot Y preserved; XY/empty plots untouched) and `zoom_reset`
  (fit all).

No existing struct or vtable slot was touched — every already-built plugin keeps
working with no recompile (`abidiff`: additions only).

## [Unreleased] — on branch `feature/plot-markers`, not yet publicly tagged

### Host service: markers + transforms unified into `pj.data_processors.v1` (UNRELEASED BREAK)

The two whole-series host-driven services were collapsed into ONE contract — Pablo's
`pj.data_processors.v1` — with a string `kind` discriminator, so a plugin chooses
`markers`/`transform` (and future engines) from the **same** `create` call:

- **Removed** `PJ_markers_host_vtable_t` / `MarkersHostService` (the old `pj.markers.v1`,
  which never left this branch) and the separate `PJ_generators_host_vtable_t` /
  `GeneratorsHostService` (`pj.generators.v1`) that briefly carried the markers backend.
- **Generalized** `pj.data_processors.v1` `create_data_processor` to the unified shape:
  added `kind` (`"transform"` → DerivedEngine timeseries; `"markers"` → ObjectStore
  PlotMarkers), `language`, a `flags` bitset with `PJ_DATA_PROCESSOR_FLAG_EPHEMERAL`,
  and an `out_topics` resolved-name return. The separate `create_data_processor_ephemeral`
  slot is **removed** — ephemeral preview is now the EPHEMERAL flag on `create`.
- **Generalized** `validate_data_processor_script` to take a `kind` argument.
- `DataProcessorsHostView` gains the unified `create(kind, …)` plus thin convenience
  shims `createTransform` / `createEphemeralTransform` / `createMarkers`; the
  `createTransform`/`createEphemeralTransform`/`remove`/`list`/`recipeOf` signatures are
  unchanged, so existing transform call-sites are unaffected.

**Migration for `pj.data_processors.v1` consumers (Pablo's transform-editor work).** The
ABI `create_data_processor` and `validate_data_processor_script` slot signatures changed,
so a host that IMPLEMENTS the service (e.g. `DataProcessorsRuntimeHost`) must update its
vtable fill; callers using `DataProcessorsHostView::createTransform` keep working via the
shim, while `validateScript` call-sites gain a leading `kind` argument (`"transform"`).

**Versioning note.** This is an ABI/API change to a service that merged to `main`. It
ships as `0.13.0` because no PUBLIC tag has carried `pj.data_processors.v1` yet, so no
released plugin is broken. **The first public release that carries the unified
`pj.data_processors.v1` must be tagged `1.0.0`** per the pre-1.0 break rule in `CLAUDE.md`.
