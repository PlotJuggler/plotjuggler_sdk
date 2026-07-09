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

1. **Ship the `-Werror` gate in a released tag.** The recipe sets
   `-DPJ_WARNINGS_AS_ERRORS=OFF` so a not-yet-pinned compiler on CCI's build
   matrix cannot fail the build on a new warning. That option was added on this
   branch (`CMakeLists.txt`). It must be present in the tag CCI consumes:
   merge this branch, cut a new release tag, then update `config.yml` +
   `conandata.yml` (new version + new sha256). The sha256 currently pinned is
   for the *pre-gate* `v0.14.0` tag and is only good for local smoke testing.
2. **Pick the entry version.** Per the SDK's own versioning note, the first
   public release carrying the unified `pj.data_processors.v1` service must be
   `1.0.0`. Decide whether CCI is seeded with `0.14.x` or `1.0.0`.
3. **Confirm dependencies exist in CCI** (verified 2026-07-01, all present):
   `nlohmann_json/3.12.0`, `fmt/12.1.0`, `fast_float/8.1.0`.

## Test locally before opening the PR

From inside a checkout of the conan-center-index fork:

```bash
cd recipes/plotjuggler_sdk
# Build + run the test_package for the default profile:
conan create all --version=0.14.0 --build=missing
# Exercise the option matrix CCI will build:
conan create all --version=0.14.0 -o "&:with_host=False" --build=missing
conan create all --version=0.14.0 -o "&:assert_throws=True" --build=missing
conan create all --version=0.14.0 -s compiler.cppstd=20 --build=missing
```

Then run the Conan Center hooks locally (catches KB-Hxxx violations the reviewer
bot will flag):

```bash
conan config install https://github.com/conan-io/conan-center-index.git \
  -sf .c3i/hooks -tf .conan2/extensions/hooks   # one-time
conan create all --version=0.14.0 --build=missing               # hooks run inline
```

## Open the PR

Fork `conan-io/conan-center-index`, drop this tree under `recipes/`, commit, and
open a PR titled `plotjuggler_sdk/0.14.0`. Expect one or more review rounds —
the usual notes are around option count (`with_host`, `assert_throws`), license
packaging, and the compiler-minimum map. Acceptance is at CCI maintainer
discretion.

## Recurring cost per release

Each future SDK release needs a follow-up CCI PR: add the new version to
`config.yml`, add its `url` + `sha256` to `conandata.yml`. Weigh this against the
Cloudsmith Conan remote (`conan.cloudsmith.io/plotjuggler/plotjuggler`) you
already operate, which needs no external review but requires consumers to add
the remote.
