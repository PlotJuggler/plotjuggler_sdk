# Conan Center submission for `plotjuggler_sdk`

These files are the **Conan Center Index (CCI) recipe** for this SDK. They do
**not** belong to the plotjuggler_sdk build — they are staged here so the recipe
is version-controlled and kept in sync with the source. To publish, copy the
`recipes/plotjuggler_sdk/` tree into a fork of
[`conan-io/conan-center-index`](https://github.com/conan-io/conan-center-index)
and open a PR.

```
recipes/plotjuggler_sdk/
├── config.yml                      # version -> folder map
└── all/
    ├── conanfile.py                # the recipe (downloads a released tag, no vendored sources)
    ├── conandata.yml               # tag url + sha256 per version
    └── test_package/               # minimal consumer CCI builds to validate the package
```

This is separate from the repo's own `conanfile.py`, which vendors the working
tree for local dev/consume. The CCI recipe instead downloads the released
**tag tarball** by URL + sha256 and never sees your working tree.

## Prerequisites before submitting

The recipe is seeded with **`0.16.2`** — the first tag that satisfies these:

1. **The `-Werror` gate ships in the consumed tag.** The recipe sets
   `-DPJ_WARNINGS_AS_ERRORS=OFF` so a not-yet-pinned compiler on CCI's build
   matrix cannot fail the build on a new warning. That option landed in
   `CMakeLists.txt` via #139 (first present in `v0.15.0`).
2. **The tag no longer ships `PluginRuntimeCatalog`.** #144 removed the
   host-side duplicate-resolution catalog (moved to the app). Whatever CCI
   accepts first becomes the floor consumers pin against — seeding with
   `0.15.0` would publish API that `0.16.0` removes, breaking the repo's
   "no breaking changes within 0.x" promise for CCI consumers.
3. **The tag builds on CCI's macOS deployment target.** CCI's macOS builders
   target below 13.3, where libc++ marks the floating-point `std::to_chars`
   overloads unavailable — `v0.16.0` failed there (`plugin_data_api.hpp`), and
   `v0.16.1`'s fallback guard misspelled the availability macro and never
   engaged. `0.16.2` carries the working `snprintf` fallback.
4. **Confirm dependencies exist in CCI** (verified 2026-07-01, all present):
   `nlohmann_json/3.12.0`, `fmt/12.1.0`, `fast_float/8.1.0`.

## After tagging a new release

The digest of a tag tarball cannot be known before the tag is cut, so a
release-prep PR carries an all-zeros placeholder `sha256` in `conandata.yml`.
Once the `vX.Y.Z` tag is pushed, recompute with the `curl … | sha256sum`
one-liner in `conandata.yml` and replace the placeholder before copying this
tree into the conan-center-index fork.

## Test locally before opening the PR

From inside a checkout of the conan-center-index fork:

```bash
cd recipes/plotjuggler_sdk
# Build + run the test_package. The recipe validate()s C++20, so a profile
# whose compiler.cppstd is < 20 (e.g. the common gnu17 default) is correctly
# rejected — pass an explicit C++20 std:
conan create all --version=0.16.2 -s compiler.cppstd=20 --build=missing
# Exercise the option matrix CCI will build:
conan create all --version=0.16.2 -s compiler.cppstd=20 -o "&:with_host=False" --build=missing
conan create all --version=0.16.2 -s compiler.cppstd=20 -o "&:assert_throws=True" --build=missing
```

Then run the Conan Center hooks locally (catches KB-Hxxx violations the reviewer
bot will flag):

```bash
conan config install https://github.com/conan-io/conan-center-index.git \
  -sf .c3i/hooks -tf .conan2/extensions/hooks   # one-time
conan create all --version=0.16.2 -s compiler.cppstd=20 --build=missing   # hooks run inline
```

## Open the PR

Fork `conan-io/conan-center-index`, drop this tree under `recipes/`, commit, and
open a PR titled `plotjuggler_sdk/0.16.2`. Expect one or more review rounds —
the usual notes are around option count (`with_host`, `assert_throws`), license
packaging, and the compiler-minimum map. Acceptance is at CCI maintainer
discretion.

## Recurring cost per release

Each future SDK release needs a follow-up CCI PR: add the new version to
`config.yml`, add its `url` + `sha256` to `conandata.yml`. Weigh this against the
Cloudsmith Conan remote (`conan.cloudsmith.io/plotjuggler/plotjuggler`) you
already operate, which needs no external review but requires consumers to add
the remote.
