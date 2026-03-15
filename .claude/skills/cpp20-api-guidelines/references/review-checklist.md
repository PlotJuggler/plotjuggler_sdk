# Review Checklist

Run this checklist before returning recommendations or code.

## API shape
1. Is the API minimal and coherent (no convenience overload sprawl)?
2. Are ownership and lifetime explicit in every public boundary?
3. Is mutation explicit and const applied by default?
4. Could a free function replace a class without losing invariants?
5. Are side effects isolated from pure logic?
6. Is error handling consistent for this layer (PJ::Expected<T> / PJ::Status for errors, std::optional when no error detail needed)?
7. Are out-parameters used only when allocation/copy costs justify them?
8. Is duplicated business logic removed without premature abstraction?

## Misuse resistance
9. Are boolean flags in public APIs replaced by `enum class` or strong option types?
10. Are single-argument constructors `explicit` unless intentional implicit conversion is needed?
11. Are adjacent same-type parameters prevented with strong domain types?
12. Are sentinel values replaced with `std::optional`?
13. Are fallible return values marked `[[nodiscard]]`?
14. Are objects fully valid after construction (no two-phase init)?

## Container & formatting usage
15. Are hash containers in `.cpp` files using `tsl::robin_map`/`tsl::robin_set` instead of `std::unordered_*`?
16. Are hash containers in `.hpp` headers using `std::unordered_map`/`std::unordered_set`?
17. Are string operations using `fmt::format` where appropriate?
18. Are time values using `int64_t` nanoseconds or `std::chrono` types instead of raw integers?
19. Are public API headers using `PJ::Expected<T>` / `PJ::Status` for error handling?
20. Is `.has_value()` checked before accessing `Expected<T>` values (not `.ok()`)?
21. Are custom types hashable via `std::hash` specialization if used as hash keys?

## Testability and safety
22. Can core logic be tested deterministically without global state?
23. Are thread-safety guarantees documented where concurrency exists?
24. Is `std::mutex` / `std::scoped_lock` used where synchronization is needed?
25. Is API naming consistent, clear, and explicit about side effects/ownership?
26. Does the parameter passing follow the guide (value/ref/view)?
