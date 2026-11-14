Validate and commit staged/unstaged changes. Run these steps in order, stopping on first failure:

1. `./build.sh --debug` — Debug+ASAN build into `build/debug_asan` must succeed
2. `./test.sh` — all tests must pass
3. `pre-commit run --all-files` — clang-format and file hygiene must be clean
4. `git ls-files -z '*.cpp' | xargs -0 clang-tidy-22 -p build/debug_asan` — no clang-tidy warnings

If all pass, create a git commit following the repo's commit conventions (see git log for style). If any step fails, fix the issue and re-run from step 1.
