/**
 * @file field_table.hpp
 * @brief Compile-time field tables describing the members of builtin object
 *        structs, so a generic binder (e.g. a script engine) can read/write
 *        any described field by name without per-type glue code.
 *
 * `FieldTable<T>` is a template specialized once per describable struct
 * (see `frame_transforms_fields.hpp`, `image_annotations_fields.hpp`). Each
 * specialization lists its members as `FieldDescriptor`s built with
 * `field<&T::member>("member")`, which deduces the member's `FieldKind` and
 * generates non-capturing-lambda accessors from the member pointer alone —
 * no per-field boilerplate at the call site. `field_table_registry.hpp`
 * exposes `describe(BuiltinObjectType)` to look up a table from the runtime
 * tag carried by a `BuiltinObject`.
 *
 * A table is a flat `Span<FieldDescriptor>`; nested structs and lists of
 * described structs link to their own table through `FieldDescriptor::nested`,
 * so a consumer walks an arbitrarily deep struct tree with one generic
 * recursive routine (see `field_table_test.cpp` for the pattern). A packed
 * record buffer (e.g. `PointCloud::data`) does not fit `field<>()`'s
 * one-member-at-a-time shape — it is derived from several sibling members
 * (bytes, stride, dimensions, channel layout) — so a buffer-bearing type
 * instead provides its own `kBuffer` descriptor factory built directly
 * against `FieldDescriptor` (e.g. `PointCloud`'s `pointCloudDataField()` in
 * `point_cloud_fields.hpp`), resolving a `BufferLayout` on demand.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/span.hpp"

namespace PJ::sdk {

/// Runtime-visible shape of one field, driving which `FieldDescriptor`
/// accessors are populated.
enum class FieldKind : uint8_t {
  kNumber,  ///< Arithmetic member (not bool/int64/enum) — read/written as `double`.
  kBool,    ///< `bool` member — read/written as `double` (0.0/1.0).
  kInt64,   ///< `int64_t`/`uint64_t`/`Timestamp` member — read/written as `int64_t`, never `double`
            ///< (a 53+ bit timestamp silently loses precision as a double).
  kString,  ///< `std::string` member.
  kEnum,    ///< `enum`/`enum class` member — read/written as its underlying integer via `double`.
  kStruct,  ///< Member whose type has its own `FieldTable` specialization; see `nested`.
  kList,    ///< `std::vector<E>` (growable; see `list_emplace`/`list_clear`) or `std::array<E, N>`
            ///< (fixed-size; see `list_replace`) member; see `element_kind`/`nested` for the element shape.
  kBuffer,  ///< Raw record buffer (e.g. `PointCloud::data`), resolved through `BufferLayout`. A concrete
            ///< describable type provides its own `kBuffer` descriptor factory (see `BufferLayout`'s doc comment).
  kOptionalNumber,  ///< `std::optional<Arithmetic>` member (e.g. `Image::compressed_depth_min`) — a
                    ///< nullable number. Presence is read through `has_value()`; the value itself, when
                    ///< present, is read/written as `double` through `get_number`/`set_number`, exactly
                    ///< like `kNumber`. Calling `get_number`/`set_number` when absent is only meaningful
                    ///< after a caller has checked `has_value()`; `set_number` also makes the field present.
};

/// Resolved view of a `kBuffer` field: a byte-packed record buffer plus a
/// PointField-style channel layout for interpreting each record. Built
/// on-demand by `FieldDescriptor::buffer()` — a fresh value each call, not a
/// pointer into a stored layout — so `channels` owns a small vector rather
/// than viewing one (`Channel`'s shape deliberately differs from the owner's
/// own channel-description type, e.g. `PointField`, so the two are not
/// safely alias-able through a reinterpreted span). Resolve once per object
/// and iterate its records from that one `BufferLayout`; `buffer()`
/// allocates the channel vector on every call, so calling it per-record is
/// wasteful.
struct BufferLayout {
  /// Raw record bytes: `record_count` records of `record_step` bytes each
  /// (or row-strided by `row_step` when `row_step != 0`).
  Span<const uint8_t> bytes;
  /// Byte length of one record.
  uint32_t record_step = 0;
  /// Number of records packed into `bytes`.
  uint64_t record_count = 0;
  /// Byte length of one row when records are organized in rows; 0 when flat.
  uint32_t row_step = 0;
  /// True if multi-byte values inside `bytes` are big-endian.
  bool is_bigendian = false;

  /// One named, typed channel packed at a fixed offset within every record.
  struct Channel {
    /// Channel name (e.g. "x", "intensity"). Views into the owner's own
    /// channel-description member (e.g. `PointCloud::fields[i].name`), so it
    /// is valid exactly as long as that owner is.
    std::string_view name;
    /// Byte offset of this channel within one record.
    uint32_t offset = 0;
    /// Wire datatype tag; codec-defined (mirrors PointField datatype values).
    uint8_t datatype = 0;
    /// Number of consecutive `datatype` values packed at `offset`.
    uint32_t count = 1;
  };
  /// Layout of every channel packed into one record of `bytes`, freshly
  /// built by this call (owned, not a view — resolving it does one small
  /// allocation, proportional to channel count, e.g. 4 for an XYZI cloud).
  std::vector<Channel> channels;
};

/// One field of a `FieldTable`: its name, shape, and type-erased accessors.
/// Every accessor not applicable to `kind` is `nullptr` — a consumer
/// switches on `kind` (or `element_kind` inside a `kList`) and calls only
/// the accessor group that kind documents.
struct FieldDescriptor {
  /// Field name as declared in the owning struct (e.g. "translation").
  std::string_view name;
  /// Shape of this field; selects which accessor group below is non-null.
  FieldKind kind = FieldKind::kNumber;
  /// For `kind == kList`: shape of one element. When `nested != nullptr` the
  /// element is a struct and this is always `kStruct` — a consumer that
  /// needs to know "struct element or scalar element" may branch on
  /// `nested` first and only consult `element_kind` for the scalar case.
  /// Unused (left at its default) for every other `kind`.
  FieldKind element_kind = FieldKind::kNumber;
  /// For `kind == kStruct`: the member type's own table.
  /// For `kind == kList`: the element type's table, or `nullptr` for a scalar list.
  const struct FieldTableView* nested = nullptr;

  /// Read/write a `kNumber`, `kBool`, `kEnum`, or `kOptionalNumber` field as
  /// `double`. For `kOptionalNumber`, meaningful only when `has_value()` is
  /// true; `set_number` also makes the field present.
  double (*get_number)(const void*) = nullptr;
  void (*set_number)(void*, double) = nullptr;

  /// For `kind == kOptionalNumber`: true iff the field currently holds a
  /// value. `nullptr` for every other `kind`.
  bool (*has_value)(const void*) = nullptr;

  /// Read/write a `kInt64` field. An underlying `uint64_t` round-trips through
  /// `int64_t` via `std::bit_cast` (same 8 bytes, reinterpreted) rather than a
  /// narrowing numeric conversion, so every bit pattern survives exactly.
  int64_t (*get_int64)(const void*) = nullptr;
  void (*set_int64)(void*, int64_t) = nullptr;

  /// Read/write a `kString` field.
  std::string_view (*get_string)(const void*) = nullptr;
  void (*set_string)(void*, std::string_view) = nullptr;

  /// For `kind == kStruct`: address the nested struct within the owner.
  const void* (*struct_ptr)(const void*) = nullptr;
  void* (*struct_ptr_mut)(void*) = nullptr;

  /// For `kind == kList`: element count and element address — populated for
  /// both a growable list (`std::vector<E>`) and a fixed-size one
  /// (`std::array<E, N>`).
  size_t (*list_size)(const void*) = nullptr;
  const void* (*list_at)(const void*, size_t) = nullptr;

  /// For `kind == kList` backed by a growable `std::vector<E>`: append-one
  /// (returns the address of the newly appended element) and
  /// truncate-to-empty. `nullptr` for a fixed-size list — use `list_replace`
  /// instead.
  void* (*list_emplace)(void*) = nullptr;
  void (*list_clear)(void*) = nullptr;

  /// For `kind == kList` backed by a fixed-size `std::array<E, N>` (e.g.
  /// `CameraInfo::K`): overwrites element `i` in place and returns its
  /// address, `i` in `[0, list_size)`. `nullptr` for a growable list — use
  /// `list_emplace`/`list_clear` instead. The element count never changes
  /// for such a field, so "append" and "truncate" don't apply; a generic
  /// consumer branches on which of the two accessor pairs is non-null to
  /// choose how to write the list (see `field_table_test.cpp`'s
  /// `copyThroughTable` for the pattern).
  void* (*list_replace)(void*, size_t) = nullptr;

  /// For `kind == kBuffer`: resolve the current buffer, or replace it
  /// (taking ownership of the bytes and re-anchoring them). See
  /// `BufferLayout`'s doc comment and a concrete factory such as
  /// `pointCloudDataField()` (`point_cloud_fields.hpp`).
  BufferLayout (*buffer)(const void*) = nullptr;
  void (*buffer_assign)(void*, std::vector<uint8_t>) = nullptr;
};

/// A described struct's field list plus its name, for diagnostics.
struct FieldTableView {
  /// The described struct's type name (e.g. "FrameTransform").
  std::string_view type_name;
  /// Every described member, in declaration order.
  Span<const FieldDescriptor> fields;
};

/// Per-type registry of field descriptors. Specialized once per describable
/// struct as:
/// ```cpp
/// template <> struct FieldTable<Vector2> {
///   static constexpr std::array<FieldDescriptor, 2> fields{
///       field<&Vector2::x>("x"), field<&Vector2::y>("y")};
///   static constexpr FieldTableView view{"Vector2", Span<const FieldDescriptor>(fields)};
/// };
/// ```
/// The primary template is declared only, never defined: `HasFieldTable<T>`
/// detects "no specialization exists for T" through that incompleteness.
template <class T>
struct FieldTable;

namespace detail {

/// Splits a data-member-pointer type `T C::*` into its owner (`C`) and
/// value (`T`) types, for deducing both from a `field<&C::member>` NTTP.
template <class M>
struct MemberPointerTraits;

template <class C, class T>
struct MemberPointerTraits<T C::*> {
  using Owner = C;
  using Value = T;
};

template <class T>
struct IsVector : std::false_type {};
template <class E, class A>
struct IsVector<std::vector<E, A>> : std::true_type {};
template <class T>
inline constexpr bool kIsVector = IsVector<T>::value;

/// Detects `std::array<E, N>` (any `E`, any `N`), for `field<>()`'s
/// fixed-size-list branch (e.g. `CameraInfo::K`).
template <class T>
struct IsArray : std::false_type {};
template <class E, std::size_t N>
struct IsArray<std::array<E, N>> : std::true_type {};
template <class T>
inline constexpr bool kIsArray = IsArray<T>::value;

/// Detects `std::optional<E>`, for `field<>()`'s `kOptionalNumber` branch.
template <class T>
struct IsOptional : std::false_type {};
template <class E>
struct IsOptional<std::optional<E>> : std::true_type {};
template <class T>
inline constexpr bool kIsOptional = IsOptional<T>::value;

/// Always-false, but dependent on `T` — lets a `static_assert` inside the
/// final branch of an `if constexpr` chain fire only when instantiated,
/// instead of unconditionally.
template <class T>
inline constexpr bool kAlwaysFalse = false;

/// True for every scalar type `setScalarAccessors` below knows how to
/// describe: `bool`/other arithmetic (covers `int64_t`/`uint64_t` too,
/// handled as a distinct `FieldKind` inside `setScalarAccessors`),
/// `enum`/`enum class`, and `std::string`.
template <class V>
inline constexpr bool kIsSupportedScalar =
    std::is_arithmetic_v<V> || std::is_enum_v<V> || std::is_same_v<V, std::string>;

/// Locates a scalar value directly AT the given address — the adapter for a
/// `kList` element, where `list_at`/`list_emplace` already return a pointer
/// to the element itself.
template <class V>
struct DirectValueAccess {
  static V* get(void* p) {
    return static_cast<V*>(p);
  }
  static const V* get(const void* p) {
    return static_cast<const V*>(p);
  }
};

/// Locates a scalar value at `owner->*Member` — the adapter for an ordinary
/// struct field, where the `(const) void*` a consumer passes in addresses
/// the OWNING struct, not the member.
template <auto Member>
struct MemberValueAccess {
  using Owner = typename MemberPointerTraits<decltype(Member)>::Owner;
  static auto* get(void* p) {
    return &(static_cast<Owner*>(p)->*Member);
  }
  static auto* get(const void* p) {
    return &(static_cast<const Owner*>(p)->*Member);
  }
};

/// Populates `d`'s get/set accessor pair for scalar value type `V`, located
/// through `Access::get(p)`, and returns the matching `FieldKind`. Shared by
/// `field<Member>()`'s member branch (`Access = MemberValueAccess<Member>`,
/// caller assigns the result to `d.kind`) and its vector-element branch
/// (`Access = DirectValueAccess<Element>`, caller assigns the result to
/// `d.element_kind`) — the kind-deduction and lambda bodies are otherwise
/// identical between "a struct field" and "a scalar list element", so this
/// is the one place that logic is written. Only ever called for `V` with
/// `kIsSupportedScalar<V>` true; the `field<>()` call sites gate on that
/// before calling in, so the `static_assert` below is unreachable through
/// them (kept as a safety net for a future direct caller).
template <class V, class Access>
constexpr FieldKind setScalarAccessors(FieldDescriptor& d) {
  if constexpr (std::is_same_v<V, bool>) {
    d.get_number = [](const void* p) { return static_cast<double>(*Access::get(p)); };
    d.set_number = [](void* p, double v) { *Access::get(p) = (v != 0.0); };
    return FieldKind::kBool;
  } else if constexpr (std::is_same_v<V, int64_t>) {
    d.get_int64 = [](const void* p) { return static_cast<int64_t>(*Access::get(p)); };
    d.set_int64 = [](void* p, int64_t v) { *Access::get(p) = static_cast<V>(v); };
    return FieldKind::kInt64;
  } else if constexpr (std::is_same_v<V, uint64_t>) {
    // Round-trips through int64_t via bit_cast (same 8 bytes, reinterpreted)
    // rather than a narrowing numeric conversion, so every bit pattern
    // survives exactly — see FieldDescriptor::get_int64's doc comment.
    d.get_int64 = [](const void* p) { return std::bit_cast<int64_t>(*Access::get(p)); };
    d.set_int64 = [](void* p, int64_t v) { *Access::get(p) = std::bit_cast<uint64_t>(v); };
    return FieldKind::kInt64;
  } else if constexpr (std::is_enum_v<V>) {
    using Underlying = std::underlying_type_t<V>;
    d.get_number = [](const void* p) { return static_cast<double>(static_cast<Underlying>(*Access::get(p))); };
    d.set_number = [](void* p, double v) { *Access::get(p) = static_cast<V>(static_cast<Underlying>(v)); };
    return FieldKind::kEnum;
  } else if constexpr (std::is_arithmetic_v<V>) {
    d.get_number = [](const void* p) { return static_cast<double>(*Access::get(p)); };
    d.set_number = [](void* p, double v) { *Access::get(p) = static_cast<V>(v); };
    return FieldKind::kNumber;
  } else if constexpr (std::is_same_v<V, std::string>) {
    d.get_string = [](const void* p) -> std::string_view { return *Access::get(p); };
    d.set_string = [](void* p, std::string_view v) { Access::get(p)->assign(v); };
    return FieldKind::kString;
  } else {
    static_assert(kAlwaysFalse<V>, "setScalarAccessors: unsupported scalar type");
  }
}

template <class T, class = void>
inline constexpr bool kHasFieldTableImpl = false;
/// SFINAE probe: substitution fails (leaving the primary `false` above) when
/// `FieldTable<T>` has no specialization, since the primary template of
/// `FieldTable` is declared but never defined and so stays incomplete.
template <class T>
inline constexpr bool kHasFieldTableImpl<T, std::void_t<decltype(FieldTable<T>::view)>> = true;

}  // namespace detail

/// True iff `FieldTable<T>` has a specialization (i.e. `T` is describable),
/// detected without requiring the caller to define one.
template <class T>
concept HasFieldTable = detail::kHasFieldTableImpl<T>;

/// Builds the `FieldDescriptor` for data member `Member` (e.g.
/// `field<&Vector2::x>("x")`), deducing the owning struct and member type
/// from the member-pointer NTTP and generating non-capturing-lambda
/// accessors that cast `(const) void*` back to the owner type. `FieldKind`
/// is deduced from the member's type:
///   - `bool` -> kBool; `int64_t`/`uint64_t` (incl. `Timestamp`) -> kInt64;
///     other arithmetic -> kNumber; `enum`/`enum class` -> kEnum;
///     `std::string` -> kString.
///   - a class type `M` with `HasFieldTable<M>` -> kStruct.
///   - `std::vector<E>` -> kList (growable; see `list_emplace`/`list_clear`),
///     with `nested`/`element_kind` set from `E` the same way (kStruct +
///     nested table when `HasFieldTable<E>`, otherwise the scalar
///     `FieldKind` of `E`). For a scalar `E`, the matching get_*/set_* pair
///     is ALSO populated, but operating on an ELEMENT address (as returned
///     by `list_at`/`list_emplace`), not on `owner->*Member` — the same
///     accessor a struct field of kind `E` would carry, repurposed for the
///     list's element type.
///   - `std::array<E, N>` -> kList (fixed-size; see `list_replace`), `E`
///     restricted to a scalar (no struct-element fixed arrays today). Reads
///     the same way as a `std::vector<E>` list (`list_size`/`list_at`); the
///     element count never changes, so writes go through `list_replace`
///     instead of `list_emplace`/`list_clear`.
///   - `std::optional<E>` (`E` a non-bool arithmetic type) -> kOptionalNumber,
///     a nullable number: `has_value()` reports presence, `get_number`/
///     `set_number` read/write the value like `kNumber` when present.
/// Any other member type fails to compile with a `static_assert`.
template <auto Member>
constexpr FieldDescriptor field(std::string_view name) {
  using MemberPtr = decltype(Member);
  using Owner = typename detail::MemberPointerTraits<MemberPtr>::Owner;
  using Value = typename detail::MemberPointerTraits<MemberPtr>::Value;

  FieldDescriptor d{};
  d.name = name;

  if constexpr (detail::kIsSupportedScalar<Value>) {
    d.kind = detail::setScalarAccessors<Value, detail::MemberValueAccess<Member>>(d);
  } else if constexpr (detail::kIsVector<Value>) {
    using Element = typename Value::value_type;
    d.kind = FieldKind::kList;
    d.list_size = [](const void* p) -> size_t { return (static_cast<const Owner*>(p)->*Member).size(); };
    d.list_at = [](const void* p, size_t i) -> const void* { return &(static_cast<const Owner*>(p)->*Member)[i]; };
    d.list_emplace = [](void* p) -> void* {
      auto& vec = static_cast<Owner*>(p)->*Member;
      vec.emplace_back();
      return &vec.back();
    };
    d.list_clear = [](void* p) { (static_cast<Owner*>(p)->*Member).clear(); };

    // A struct element links to its own table; a scalar element instead gets
    // the matching get_*/set_* pair (see setScalarAccessors's doc comment),
    // operating on an ELEMENT address (as returned by list_at/list_emplace)
    // rather than on `owner->*Member`. Either way a generic consumer copies
    // the list the same way: list_clear + list_emplace, then either recurse
    // through `nested` or call the matching set_* on the element address.
    if constexpr (HasFieldTable<Element>) {
      d.element_kind = FieldKind::kStruct;
      d.nested = &FieldTable<Element>::view;
    } else if constexpr (detail::kIsSupportedScalar<Element>) {
      d.element_kind = detail::setScalarAccessors<Element, detail::DirectValueAccess<Element>>(d);
    } else {
      static_assert(detail::kAlwaysFalse<Element>, "field<Member>: unsupported std::vector element type");
    }
  } else if constexpr (detail::kIsArray<Value>) {
    using Element = typename Value::value_type;
    static_assert(detail::kIsSupportedScalar<Element>, "field<Member>: unsupported std::array element type");
    constexpr size_t kSize = std::tuple_size_v<Value>;
    d.kind = FieldKind::kList;
    d.list_size = [](const void*) -> size_t { return kSize; };
    d.list_at = [](const void* p, size_t i) -> const void* { return &(static_cast<const Owner*>(p)->*Member)[i]; };
    d.list_replace = [](void* p, size_t i) -> void* { return &(static_cast<Owner*>(p)->*Member)[i]; };
    // Same scalar accessor pair a std::vector<Element> list would carry
    // (see field<>()'s std::vector branch), operating on an element address.
    d.element_kind = detail::setScalarAccessors<Element, detail::DirectValueAccess<Element>>(d);
  } else if constexpr (detail::kIsOptional<Value>) {
    using Inner = typename Value::value_type;
    static_assert(
        std::is_arithmetic_v<Inner> && !std::is_same_v<Inner, bool>,
        "field<Member>: unsupported std::optional value type (must be non-bool arithmetic)");
    d.kind = FieldKind::kOptionalNumber;
    d.has_value = [](const void* p) -> bool { return (static_cast<const Owner*>(p)->*Member).has_value(); };
    d.get_number = [](const void* p) -> double {
      return static_cast<double>(*(static_cast<const Owner*>(p)->*Member));
    };
    d.set_number = [](void* p, double v) { (static_cast<Owner*>(p)->*Member) = static_cast<Inner>(v); };
  } else if constexpr (HasFieldTable<Value>) {
    d.kind = FieldKind::kStruct;
    d.nested = &FieldTable<Value>::view;
    d.struct_ptr = [](const void* p) -> const void* { return &(static_cast<const Owner*>(p)->*Member); };
    d.struct_ptr_mut = [](void* p) -> void* { return &(static_cast<Owner*>(p)->*Member); };
  } else {
    static_assert(
        detail::kAlwaysFalse<Value>,
        "field<Member>: unsupported member type (no FieldTable<T> and not a recognized scalar/vector/array/optional)");
  }

  return d;
}

}  // namespace PJ::sdk
