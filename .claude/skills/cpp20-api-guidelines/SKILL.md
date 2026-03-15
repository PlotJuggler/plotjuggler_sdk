---
name: cpp20-api-guidelines
description: "Generate and review C++20 code in a simplicity-first style. Use when tasks involve API design, code reviews, refactors, or coding standards where correctness and usability matter: const-by-default, stateless-first interfaces, minimal abstraction (avoid SOLID-for-its-own-sake), DRY with restraint, testability by design, explicit ownership/lifetimes, PJ::Expected<T>/PJ::Status error handling, and tsl::robin_map containers."
---

# C++20 API Guidelines

## Workflow

1. Read `cpp_design_recommendations.md` (project root) before producing code or recommendations.
   This is the authoritative design guide with rationale, examples, and library usage.
2. Apply the rules as hard constraints (`MUST`/`MUST NOT`) and preferences (`SHOULD`).
3. If existing project conventions conflict with a `SHOULD`, follow project conventions and note the tradeoff.
4. Run `references/review-checklist.md` as a final self-check before returning code.
5. Keep output concise: propose the simplest valid API that prevents common misuse.

## Key References (project root)

- `cpp_design_recommendations.md` -- Design guide (rules, examples, rationale, parameter passing, container selection)
- `.clang-tidy` -- Reference clang-tidy config (bugprone-*, modernize-*, etc.)

## Output Requirements

- Prefer small, intention-revealing APIs.
- Prefer free functions over classes unless state/invariants/lifecycle require a class.
- Keep ownership, lifetime, side effects, and error semantics explicit.
- **Error handling:**
  - Use `PJ::Expected<T>` (recoverable error with value), `PJ::Status`
    (recoverable error, no return value), `PJ::Span<T>` (non-owning view).
  - Success: `PJ::ok_status()`. Error: `PJ::unexpected(msg)`. Check: `.has_value()`.
- Use `tsl::robin_map` for hash containers in `.cpp` files, `std::unordered_map` in headers.
  Use `fmt::format` for string formatting.
- Include minimal code examples only when they clarify contracts or misuse prevention.
