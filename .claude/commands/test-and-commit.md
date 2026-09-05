Validate and commit staged/unstaged changes. Run these steps in order, stopping on first failure:

1. `./build.sh --debug` — Debug+ASAN build must succeed
2. `./test.sh` — all tests must pass
3. `git ls-files -z '*.cpp' | xargs -0 clang-tidy-22 -p build` — no clang-tidy warnings

If all three pass, create a git commit following the repo's commit conventions (see git log for style). If any step fails, fix the issue and re-run from step 1.
