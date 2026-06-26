# Changelog

All notable changes to `plotjuggler_sdk` are recorded here. Versioning policy is in
[`CLAUDE.md`](./CLAUDE.md) → "Release Versioning".

## [Unreleased] — on branch `feature/plot-markers`, not yet publicly tagged

### Host service: `pj.markers.v1` → `pj.generators.v1` (UNRELEASED BREAK)

The whole-series host-driven service was unified under a single contract with a string
`kind` discriminator:

- **Removed** `PJ_markers_host_vtable_t` / `MarkersHostService` (the old `pj.markers.v1`).
- **Added** `PJ_generators_host_vtable_t` / `GeneratorsHostService` (`pj.generators.v1`)
  with `kind="markers"` implemented (objects → ObjectStore), a `language` param,
  compile-only `validate_script`, an ephemeral-preview flag, and `out_topics` return.
- `kind="transform"` (per-sample timeseries → DerivedEngine) is **reserved**, not
  implemented; its end-state home (a generators `kind` vs the separate
  `pj.data_processors.v1` service) is decided when the transform-editor work merges.
- A speculative `kind="field"` (object engine emitting a DataEngine series) that
  briefly existed on this branch was **removed** before release — zero consumers, and
  it violated the object/timeseries engine split. Its removal changed no ABI struct
  (`kind` is a runtime string, not a vtable enum), so it carried no version bump.

**Versioning note.** This is an API removal — normally MAJOR. It ships as `0.13.0`
only because no PUBLIC tag ever contained `pj.markers.v1` (it never left this branch),
so no released plugin is broken. **The first public release that carries
`pj.generators.v1` must be tagged `1.0.0`** per the pre-1.0 break rule in `CLAUDE.md`.
