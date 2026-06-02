# C++ API Design Recommendations

A practical guide to writing C++20 APIs that are easy to use correctly and hard to use incorrectly.

## Core Philosophy

> Design APIs that fall into the **pit of success**: the correct usage path must be the easiest path.

The gold standard is Qt's design philosophy: minimal, complete, consistent, intuitive, and leading to readable code. A semi-experienced user should be able to use your API without documentation, and anyone reading code that uses it should understand what it does.

---

## 1. Let the Type System Prevent Misuse

The compiler is your best reviewer. If wrong code compiles, the API has a design flaw.

**Use strong domain types instead of bare primitives:**

```cpp
// Bad: what do these doubles mean?
Rect(double, double, double, double);

// Good: the types document intent and prevent swapping
Rect(Position origin, Size extent);

// Bad: raw integer for duration
void setDuration(int ms);

// Good: self-documenting, unit-safe
void setDuration(std::chrono::milliseconds d);
```

**Use `enum class` instead of `bool` parameters:**

```cpp
// Bad: what does true, false mean here?
connect(server, true, false);

// Good: self-documenting at the call site
connect(server, UseSSL::Yes, Verbose::No);
```

**Further type-system tools:**
- Mark return values `[[nodiscard]]` when ignoring them is a bug
- Use `explicit` on single-argument constructors by default
- Constrain templates with concepts for clear error messages
- Enforce invariants in constructors/factories, not in documentation

---

## 2. Const by Default

Treat mutability as the exception. This mirrors Rust's `let` / `let mut` philosophy and enables better reasoning, optimization, and thread safety.

- Declare every local variable `const` unless it must be mutated
- Pass non-trivial inputs by `const&`
- Mark every member function `const` unless it modifies observable state
- Use `constexpr` to push computation to compile time
- Keep mutable scope minimal and local
- Expose mutation explicitly through API shape and naming

The C++ Core Guidelines codify this as rules Con.1 through Con.4: *by default, make objects immutable, member functions const, and pointers/references point to const.*

---

## 3. Prefer Free Functions Over Classes

Scott Meyers' key insight: a free function that works through a class's public interface *increases* encapsulation compared to making it a member.

```cpp
// Bad: unnecessary class wrapper
class StringUtils {
    static bool isBlank(std::string_view s);
};

// Good: free function in a namespace
namespace string_utils {
    bool is_blank(std::string_view s);
}
```

**When to use what:**

| Condition | Recommendation |
|---|---|
| No state, no invariants | Free function |
| Must be virtual | Member function |
| `operator>>` or `operator<<` | Non-member |
| Can work with public interface only | Non-member (prefer this) |
| Manages internal invariant/state | Member function |

A class with no member variables (or only static members) should not be a class.

---

## 4. Stateless and Pure First

Pure functions (no side effects, deterministic output) are trivially testable, thread-safe, cacheable, and composable.

- Write pure functions by default: all inputs through parameters, all outputs through return values
- Push side effects (I/O, network, filesystem, clock, random, UI) to the boundaries
- Never hide mutable global or shared state behind utility APIs
- When state is necessary, make it explicit in the API shape

---

## 5. Simplicity Over Abstraction

Every abstraction has a cost: indirection, cognitive load, and coupling. Justify each one with a concrete need.

**Prefer the simplest mechanism that works:**

1. Free function
2. Function + lambda/callback
3. Template with concepts
4. Concrete class
5. Abstract base class / type erasure

Move down this list only when a simpler option is insufficient.

**Anti-patterns to avoid:**
- Interfaces/ABCs created "just in case"
- Mechanical SOLID application (especially SRP taken to extremes, producing an explosion of tiny classes)
- Inheritance when composition suffices -- inheritance is the strongest coupling between two classes

> *"The biggest risk of overusing SOLID is reduced readability, as simple functionality requiring navigation through multiple abstract classes, interfaces, and dependency injections becomes hard to follow."*

---

## 6. Function Signatures That Resist Misuse

- Keep parameter count low (ideally 3 or fewer); use a named struct for groups of related parameters
- Never have adjacent parameters of the same type with different meanings
- Return values instead of out-parameters
- Use structured bindings with small named structs for multi-value returns
- Keep functions focused: one thing done well

```cpp
// Bad: two string_views that can be swapped
void copy(std::string_view src, std::string_view dst);

// Good: strong types make the order unambiguous
void copy(SourcePath src, DestPath dst);
```

---

## 7. Value Semantics by Default

Design types that behave like `int`: copied, moved, and destroyed automatically. This eliminates dangling references, memory leaks, and aliasing surprises.

- **Rule of Zero**: do not define special member functions unless your class directly manages a resource. If all members are value types or RAII wrappers, compiler defaults are correct.
- **Rule of Five**: only for small, focused resource-owning wrappers. Keep resource management separate from business logic.
- Use `std::unique_ptr` for sole ownership, `std::shared_ptr` only for true shared lifetime
- Reserve reference semantics for objects with unique identity or genuine polymorphic needs

---

## 8. Ownership and Lifetime

Make ownership explicit. The reader should never have to guess who owns what.

- Use RAII for all resource management -- never require callers to call `cleanup()`
- Pass `std::string_view` / `std::span` / `PJ::Span` for read-only input when lifetime is bounded by the call
- Never store a non-owning view beyond its source's guaranteed lifetime
- Never return references to internal data that subsequent operations may invalidate

---

## 9. Error Handling

Pick one strategy per API layer. Never mix exceptions, error codes, and global error state.

### Situation → mechanism

| Situation | Mechanism |
|---|---|
| Recoverable failure with return value | `PJ::Expected<T>` |
| Recoverable failure, no return value | `PJ::Status` |
| Truly exceptional (contract violation, OOM) | Exceptions |
| Simple absence, no error detail needed | `std::optional<T>` |

```cpp
[[nodiscard]] PJ::Expected<Config> load_config(std::string_view path);
[[nodiscard]] PJ::Status save_config(const Config& cfg, std::string_view path);

PJ::Expected<Config> load_config(std::string_view path) {
  if (!std::filesystem::exists(path))
    return PJ::unexpected(fmt::format("file not found: {}", path));
  // ...
  return config;
}
```

- Mark every fallible function `[[nodiscard]]`
- Check `.has_value()` before accessing the value in `Expected<T>`
- Use `.error()` to access the error message string
- Success: `PJ::okStatus()`. Error: `PJ::unexpected(msg_string)`.
- Propagate errors upward rather than logging at low levels

---

## 10. DRY With Restraint

DRY applies to **knowledge**, not syntax. Two identical-looking blocks serving different domain purposes should stay separate.

- **Rule of Three**: don't extract until you see the pattern three times and can name it clearly
- Before extracting, ask:
  - Is this the same piece of knowledge, or a coincidence?
  - If I change this, will every consumer want that change?
  - Will these uses evolve together or diverge?

> *"Duplication is far cheaper than the wrong abstraction."* -- Sandi Metz

**AHA: Avoid Hasty Abstractions.** Optimize for change first, abstract only when duplication itself becomes the barrier.

---

## 11. Design for Testability

> *"If a function is hard to test, its design is wrong."* -- Jody Hagins, CppCon 2024

- Separate pure logic from side-effecting code; test the pure core directly
- Inject dependencies (I/O, clock, network) as parameters or template arguments, not globals
- Prefer template-based injection over virtual interfaces when runtime polymorphism isn't needed
- Design for deterministic tests: no dependence on wall-clock, filesystem, network, or execution order
- Every bug fix includes a regression test; every API change includes a behavior test

---

## 12. Modern C++20 Features (Use Judiciously)

Use C++20 features that improve correctness and readability, not for novelty.

| Feature | Use for |
|---|---|
| **Concepts** | Constraining templates, replacing SFINAE, self-documenting requirements |
| **Ranges** | Composable data pipelines, eliminating raw iterator pairs |
| **`std::span` / `std::string_view`** | Non-owning function parameters |
| **`constexpr`** | Compile-time computation (greatly expanded in C++20) |
| **Structured bindings** | Clean multi-value returns |
| **`[[nodiscard]]`** | Preventing ignored return values |
| **Designated initializers** | Readable construction of option structs |

Avoid template metaprogramming when `concepts` + `constexpr if` suffices.

---

## 13. Library Dependencies

The project uses a small set of third-party libraries alongside the C++20 standard library.

### String Formatting

Use `fmt` for all string formatting and concatenation:

| Task | Use |
|---|---|
| Formatting / concatenation | `fmt::format("prefix: {}", value)` |
| Joining containers | `fmt::join(container, ", ")` inside `fmt::format` |

### Hash Containers

**In `.cpp` files:** use `tsl::robin_map` / `tsl::robin_set` for performance-critical hash containers.

**In `.hpp` headers:** use `std::unordered_map` / `std::unordered_set` to avoid leaking dependencies.

| Need | Use (in `.cpp`) | Use (in `.hpp`) |
|---|---|---|
| Hash map (default) | `tsl::robin_map` | `std::unordered_map` |
| Hash set (default) | `tsl::robin_set` | `std::unordered_set` |

### Error Handling

| Pattern | Type |
|---|---|
| Function returns value or fails | `PJ::Expected<T>` |
| Function can fail, no return value | `PJ::Status` |
| Non-owning span parameter | `PJ::Span<T>` / `std::span<T>` |

See [Section 9](#9-error-handling) for the full policy and examples.

### Time

The project uses `int64_t` nanoseconds for timestamps. Use `std::chrono` types for durations when type safety is desired.

### String → Number Parsing

Use `PJ::parseNumber<T>(std::string_view)` from `pj_base/number_parse.hpp` for all string-to-number conversion. Returns `std::optional<T>` — `nullopt` unless the entire input is a valid, in-range value (empty, trailing characters, and overflow all yield `nullopt`).

| Pattern | Use |
|---|---|
| Whole-string parse, fallible | `PJ::parseNumber<T>(text)` |

**Do not** call `std::strtod`, `std::strtof`, `std::strtol`, `std::stoi`, `std::stod`, `std::atoi`, `std::atof`, or `QString::toDouble`/`toInt` directly in plotjuggler_sdk code. The standard `std::strto*` family respects `LC_NUMERIC` (a `de_DE` user silently parses `"1.5"` as 1); `parseNumber` is locale-independent (backed by `fast_float` as a private dependency, hidden from the public ABI).

### Hashing

Use `std::hash<T>` for custom hash support. Specialize `std::hash` in the same namespace as the type.

### Synchronization

Use `std::mutex` and `std::scoped_lock` when synchronization is needed.

---

## 14. Concurrency

- Document thread-safety guarantees for every public API
- Avoid data races by construction: value semantics, message passing, confined ownership
- Use `std::mutex` with `std::scoped_lock` when synchronization is needed
- Use atomics/mutexes explicitly and minimally where needed
- Const objects and pure functions are inherently thread-safe

---

## 15. Naming

> *"Naming is probably the single most important issue in API design."* -- Qt Wiki

- Use full, unabbreviated names: `activatePreviousWindow()` not `fetchPrev()`
- Use consistent vocabulary: if one class uses "remove," don't use "delete" elsewhere for the same concept
- Name functions for what they return or do, not how
- Name types for what they represent in the domain
- If a name needs a comment, choose a better name

---

## Parameter Passing Guide

General guidelines:

### Input (read-only)

| Type | How to pass |
|---|---|
| Small trivial type (≤ 16 bytes) | By value |
| `std::string` / large type | `const&` or `std::string_view` |
| Contiguous range | `std::span<const T>` |
| Callable (not stored) | `const Fn&` or `std::function_ref` (C++26) / `const Fn&` |

### Input (sink / will be consumed)

| Type | How to pass |
|---|---|
| `std::string`, `std::vector`, etc. | By value (caller moves in) |
| `std::unique_ptr` | By value (transfers ownership) |

### Input-output

| Type | How to pass |
|---|---|
| Required, mutated by callee | `T&` |
| Optional, may be null | `T*` |

### Output

| Type | How to pass |
|---|---|
| Single value | Return by value |
| Multiple values | Return named struct |
| Fallible | Return `PJ::Expected<T>` |

Never pass `std::shared_ptr` unless the callee genuinely shares ownership. If it just reads the pointee, dereference and pass `const&`.

---

## Additional API Design Tips

Practical tips for robust API design:

- **No sentinel values** -- Don't use `-1`, `INT_MAX`, etc. to signal absence. Use `std::optional`.

- **Factory functions over two-phase init** -- Prefer returning `PJ::Expected<T>` or `std::unique_ptr<T>` from a factory over constructor + `Init()` method. Objects should be fully valid after construction.

- **Option structs for many parameters** -- Wrap functions with > 3 parameters in a struct with defaulted members. Use C++20 designated initializers at the callsite:
  ```cpp
  PrintDouble(5.0, {.prefix = "val=", .precision = 2});
  ```


- **No `vector.at()`** -- Use `operator[]` when the index is known valid. Perform explicit bounds checking with early return or `PJ::Status` when it is not.

- **Tag types** -- Use empty structs to disambiguate overloads when factory functions or enums are insufficient.

- **Return by value for cheap types** -- Avoid returning `const T&` to internal data when `T` is cheap to copy (e.g., `string_view`, `int`). Return by value instead -- this decouples callers from internal representation and preserves optimization freedom.

- **Avoid unnecessarily strong guarantees** -- Weaker guarantees (e.g., no iterator stability) can enable significantly better performance. Open-addressing hash maps like `tsl::robin_map` outperform `std::unordered_map` precisely because they trade iterator stability for cache locality.

---

## Quick Reference: Decision Checklists

### Before writing a class:
- Does it have state? No → free function
- Does it have invariants to guard? No → struct or free function
- Does it manage a resource? No → ensure Rule of Zero
- Does it need polymorphism? No → avoid virtual, use templates/concepts

### Before writing an abstraction:
- Do I have 3+ concrete uses? No → keep it concrete
- Can I name it clearly? No → not a real abstraction yet
- Will uses evolve together? No → keep them separate

### Before adding a parameter:
- Can it be confused with an adjacent parameter? → use strong types
- Is it a bool? → use `enum class`
- Are there > 3 params? → group into a named struct

---

## Container Selection Guide

Pick the simplest container that fits the access pattern.

### Sequences

| Need | Container |
|---|---|
| Default choice | `std::vector` |
| Stable addresses / frequent mid-insert | `std::deque` or `std::list` |
| Fixed size known at compile time | `std::array` |
| Non-owning view into contiguous memory | `std::span` |

### Associative (key lookup)

| Need | Container (`.cpp`) | Container (`.hpp`) |
|---|---|---|
| Ordered, unique keys | `std::map` | `std::map` |
| Unordered (faster), unique keys | `tsl::robin_map` | `std::unordered_map` |

### Sets

| Need | Container (`.cpp`) | Container (`.hpp`) |
|---|---|---|
| Ordered | `std::set` | `std::set` |
| Unordered (fastest) | `tsl::robin_set` | `std::unordered_set` |

### Adaptors

| Need | Container |
|---|---|
| Priority queue | `std::priority_queue` |
| FIFO | `std::queue` |
| LIFO | `std::stack` |

**Small collections (< ~32 elements):** Prefer `std::vector` even for lookup -- linear scan beats tree/hash overhead at small sizes due to cache locality.

---

## Compiler Flags

### GCC / Clang

```
-std=c++20
-Wall -Wextra
-Wcast-qual
-Wconversion
-Wdouble-promotion
-Wformat=2
-Wmissing-declarations
-Wnon-virtual-dtor
-Wold-style-cast
-Woverloaded-virtual
-Wpointer-arith
-Wshadow
-Wunused-result
-Wvla
-Werror              # in CI; optional locally
```

Clang additional:
```
-Wmost
-Wthread-safety      # thread-safety static analysis
```

Debug/test builds:
```
-fsanitize=address,undefined
```

### MSVC

```
/std:c++20
/W4
/WX              # in CI; treat warnings as errors
/permissive-
/DNOMINMAX
/DWIN32_LEAN_AND_MEAN
```

---

## Code Review Severity Guide

When reviewing code, classify issues by severity:

### Critical (must fix before merge)
- Data race or undefined behavior
- Memory leak or use-after-free
- Security vulnerability (injection, buffer overflow, unvalidated input)
- Silent data loss or corruption
- Missing `[[nodiscard]]` on fallible function whose result is ignored

### Major (should fix before merge)
- Raw owning pointer in public API
- Missing `const` on variable/parameter/member function that should be const
- Abstraction without concrete justification
- Mixed error handling strategies in one API layer
- Non-owning view stored beyond source lifetime
- Missing regression test for a bug fix

### Minor (fix if easy, otherwise track)
- Naming inconsistency (abbreviation, different verb for same concept)
- `bool` parameter where `enum class` would be clearer
- Function with > 3 parameters that could use a struct
- Missing `constexpr` on a function that could be constexpr
- Using `std::unordered_map` in `.cpp` files where `tsl::robin_map` would be better

### Suggestion (optional improvement)
- Opportunity to use ranges for cleaner pipeline
- Structured bindings would improve readability
- A concept constraint would improve error messages
- Comment restating what the code already says (remove it)

---

## Sources

- [Qt API Design Principles](https://wiki.qt.io/API_Design_Principles)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- Scott Meyers, *Effective C++* Item 18; [How Non-Member Functions Improve Encapsulation](https://www.aristeia.com/Papers/CUJ_Feb_2000.pdf)
- [fmt library](https://fmt.dev/) -- type-safe formatting
- [tsl::robin_map](https://github.com/Tessil/robin-map) -- fast open-addressing hash map
- [Strong Types for Strong Interfaces](https://www.fluentcpp.com/2016/12/08/strong-types-for-strong-interfaces/) -- Fluent C++
- [When SOLID Breaks: Choose CLARITY](https://krossovochkin.com/posts/2025_05_05_when_solid_breaks_choose_clarity/)
- [CppCon 2024: The Most Important C++ Design Guideline is Testability](https://www.classcentral.com/course/youtube-the-most-important-c-design-guideline-is-testability-jody-hagins-cppcon-2024-419955) -- Jody Hagins
- [CppCon 2025: More Speed & Simplicity](https://isocpp.org/blog/2025/08/cppcon-2025-keynote-more-speed-simplicitypractical-data-oriented-design-in) -- Vittorio Romeo
- Sandi Metz: "Duplication is far cheaper than the wrong abstraction"
- Kent C. Dodds: AHA -- Avoid Hasty Abstractions
