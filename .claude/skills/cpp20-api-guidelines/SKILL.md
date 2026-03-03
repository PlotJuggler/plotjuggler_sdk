---
name: cpp20-api-guidelines
description: "Generate and review C++20 code with Abseil in a simplicity-first style. Use when tasks involve API design, code reviews, refactors, or coding standards where correctness and usability matter: const-by-default, stateless-first interfaces, minimal abstraction (avoid SOLID-for-its-own-sake), DRY with restraint, testability by design, explicit ownership/lifetimes, PJ::Expected<T>/PJ::Status error handling in public API headers, and Swiss Table containers."
---

# C++20 API Guidelines (with Abseil)

## Workflow

1. Read `cpp_design_recommendations.md` (project root) before producing code or recommendations.
   This is the authoritative design guide with rationale, examples, and Abseil usage.
2. Apply the rules as hard constraints (`MUST`/`MUST NOT`) and preferences (`SHOULD`).
3. If existing project conventions conflict with a `SHOULD`, follow project conventions and note the tradeoff.
4. Run `references/review-checklist.md` as a final self-check before returning code.
5. Keep output concise: propose the simplest valid API that prevents common misuse.

## Key References (project root)

- `cpp_design_recommendations.md` -- Design guide (rules, examples, rationale, parameter passing, container selection)
- `.clang-tidy` -- Reference clang-tidy config (abseil-*, bugprone-*, modernize-*, etc.)

## Output Requirements

- Prefer small, intention-revealing APIs.
- Prefer free functions over classes unless state/invariants/lifecycle require a class.
- Keep ownership, lifetime, side effects, and error semantics explicit.
- **Error handling — two-layer policy:**
  - Public API headers (`.hpp`): use `PJ::Expected<T>` (recoverable error with value), `PJ::Status`
    (recoverable error, no return value), `PJ::Span<T>` (non-owning view). NO absl types in headers.
  - Implementation files (`.cpp`): absl types are always OK — `absl::StrCat`, `absl::flat_hash_map`, etc.
  - Success: `PJ::ok_status()`. Error: `PJ::unexpected(msg)`. Check: `.has_value()`.
- Use `absl::flat_hash_map` for hash containers, `absl::Duration`/`Time` for time,
  `absl::StrCat`/`StrFormat` for string operations (in .cpp files).
- Include minimal code examples only when they clarify contracts or misuse prevention.
