#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file module.hpp
 * @brief Umbrella authoring API and complete functional-module export macro.
 */

#include <array>
#if !defined(__wasm__)
#include <atomic>
#endif
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

#include "pj_base/parser_module/cdr_field_locator.hpp"
#include "pj_base/parser_module/cdr_reader.hpp"
#include "pj_base/parser_module/core.hpp"
#include "pj_base/parser_module/object_writer.hpp"
#include "pj_base/parser_module/proto_field_locator.hpp"
#include "pj_base/parser_module/proto_reader.hpp"
#include "pj_base/parser_module/time.hpp"

#if defined(PJ_PARSER_MODULE_MANIFEST_HEADER)
#include PJ_PARSER_MODULE_MANIFEST_HEADER
#endif

#if !defined(PJ_PARSER_MODULE_CLAIM_COUNT)
#define PJ_PARSER_MODULE_CLAIM_COUNT 0
#endif

#if !defined(PJ_PARSER_MODULE_HAS_MANIFEST)
namespace pj {
namespace detail {
inline constexpr char kBuiltManifest[] = "{}";
}
}  // namespace pj
#endif

#if defined(_WIN32)
#define PJ_PARSER_MODULE_EXPORT __declspec(dllexport)
#else
#define PJ_PARSER_MODULE_EXPORT __attribute__((visibility("default")))
#endif

namespace pj {

inline constexpr uint32_t kModuleAbiVersion = 1;
inline constexpr int32_t kModuleOk = 0;
inline constexpr int32_t kModuleDecline = 1;
inline constexpr int32_t kModuleError = -1;
inline constexpr int32_t kModuleBadToken = -2;
inline constexpr int32_t kModuleMalformedInput = -3;
inline constexpr int32_t kModuleBadClaimIndex = -4;
inline constexpr int32_t kModuleAllocationFailure = -5;
inline constexpr uint64_t kCreationErrorToken = 0;
inline constexpr size_t kErrorBufferSize = 512;

enum class Route : uint16_t {
  kScalar = 1,
  kObject = 2,
};

/// Optional timestamp supplied with one parse input. This is a per-message
/// value; it is never retained in BindingInfo.
struct Timestamp {
  bool has_value = false;
  int64_t nanoseconds = 0;
};

class OwnedBindingInfo {
 public:
  [[nodiscard]] Route route() const noexcept {
    return route_;
  }
  [[nodiscard]] uint32_t claimIndex() const noexcept {
    return claim_index_;
  }
  [[nodiscard]] uint16_t expectedObjectType() const noexcept {
    return expected_object_type_;
  }
  [[nodiscard]] ByteView encoding() const noexcept {
    return encoding_.view();
  }
  [[nodiscard]] ByteView typeName() const noexcept {
    return type_name_.view();
  }
  [[nodiscard]] ByteView schema() const noexcept {
    return schema_.view();
  }
  [[nodiscard]] ByteView claimId() const noexcept {
    return claim_id_.view();
  }
  [[nodiscard]] ByteView configJson() const noexcept {
    return config_json_.view();
  }
  [[nodiscard]] ByteView schemaDigest() const noexcept {
    return schema_digest_.view();
  }
  [[nodiscard]] std::string_view schemaText() const noexcept {
    return std::string_view(reinterpret_cast<const char*>(schema_.data()), schema_.size());
  }

 private:
  Route route_ = Route::kScalar;
  uint32_t claim_index_ = 0;
  uint16_t expected_object_type_ = 0;
  Blob encoding_;
  Blob type_name_;
  Blob schema_;
  Blob claim_id_;
  Blob config_json_;
  Blob schema_digest_;

  friend class BindingInfo;
};

/// Borrowed view over one bind call's encoded block. Every returned ByteView
/// expires when bind() returns. Modules that retain metadata use owningCopy().
class BindingInfo {
 public:
  [[nodiscard]] Route route() const noexcept {
    return route_;
  }
  [[nodiscard]] uint32_t claimIndex() const noexcept {
    return claim_index_;
  }
  [[nodiscard]] uint16_t expectedObjectType() const noexcept {
    return expected_object_type_;
  }
  [[nodiscard]] ByteView encoding() const noexcept {
    return encoding_;
  }
  [[nodiscard]] ByteView typeName() const noexcept {
    return type_name_;
  }
  [[nodiscard]] ByteView schema() const noexcept {
    return schema_;
  }
  [[nodiscard]] ByteView claimId() const noexcept {
    return claim_id_;
  }
  [[nodiscard]] ByteView configJson() const noexcept {
    return config_json_;
  }
  [[nodiscard]] ByteView schemaDigest() const noexcept {
    return schema_digest_;
  }
  [[nodiscard]] std::string_view schemaText() const noexcept {
    return std::string_view(reinterpret_cast<const char*>(schema_.data), schema_.size);
  }

  [[nodiscard]] Expected<OwnedBindingInfo> owningCopy() const noexcept {
    OwnedBindingInfo owned;
    owned.route_ = route_;
    owned.claim_index_ = claim_index_;
    owned.expected_object_type_ = expected_object_type_;
    const auto copy = [](Blob& destination, ByteView source) {
      Status resized = destination.resize(source.size);
      if (!resized.isOk()) {
        return resized;
      }
      if (source.size != 0) {
        if (source.data == nullptr) {
          return Status::error("BindingInfo field has null storage");
        }
        std::memcpy(destination.data(), source.data, source.size);
      }
      return Status::ok();
    };
    for (const auto& field : {
             std::pair<Blob*, ByteView>{&owned.encoding_, encoding_},
             {&owned.type_name_, type_name_},
             {&owned.schema_, schema_},
             {&owned.claim_id_, claim_id_},
             {&owned.config_json_, config_json_},
             {&owned.schema_digest_, schema_digest_},
         }) {
      Status copied = copy(*field.first, field.second);
      if (!copied.isOk()) {
        return copied;
      }
    }
    return owned;
  }

 private:
  Route route_ = Route::kScalar;
  uint32_t claim_index_ = 0;
  uint16_t expected_object_type_ = 0;
  ByteView encoding_;
  ByteView type_name_;
  ByteView schema_;
  ByteView claim_id_;
  ByteView config_json_;
  ByteView schema_digest_;

  friend Expected<BindingInfo> readBindingInfo(ByteView);
};

struct ParseInput {
  bool has_timestamp = false;
  int64_t timestamp_ns = 0;
  PayloadView payload;
};

namespace detail {

class LittleEndianReader {
 public:
  explicit LittleEndianReader(ByteView bytes) : bytes_(bytes) {}

  template <typename UInt>
  [[nodiscard]] bool read(UInt& value) {
    static_assert(std::is_unsigned<UInt>::value, "little-endian values must be unsigned");
    if (position_ > bytes_.size || sizeof(UInt) > bytes_.size - position_) {
      return false;
    }
    value = 0;
    for (size_t index = 0; index < sizeof(UInt); ++index) {
      value |= static_cast<UInt>(bytes_.data[position_ + index]) << (index * 8U);
    }
    position_ += sizeof(UInt);
    return true;
  }

  [[nodiscard]] bool skip(size_t size) {
    if (position_ > bytes_.size || size > bytes_.size - position_) {
      return false;
    }
    position_ += size;
    return true;
  }

  [[nodiscard]] size_t position() const noexcept {
    return position_;
  }

 private:
  ByteView bytes_;
  size_t position_ = 0;
};

inline uint64_t addressOf(const void* pointer) noexcept {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

inline void recordError(std::array<char, kErrorBufferSize>& destination, std::string_view message) noexcept {
  copyMessage(destination.data(), destination.size(), message);
}

}  // namespace detail

[[nodiscard]] inline Expected<BindingInfo> readBindingInfo(ByteView bytes) {
  if (bytes.data == nullptr || bytes.size < 16) {
    return Status::error("truncated BindingInfo v1 header");
  }
  detail::LittleEndianReader reader(bytes);
  uint16_t version = 0;
  uint16_t route = 0;
  uint32_t claim_index = 0;
  uint16_t expected_type = 0;
  uint16_t reserved = 0;
  uint32_t field_count = 0;
  if (!reader.read(version) || !reader.read(route) || !reader.read(claim_index) || !reader.read(expected_type) ||
      !reader.read(reserved) || !reader.read(field_count)) {
    return Status::error("truncated BindingInfo v1 header");
  }
  (void)reserved;
  if (version != 1 || (route != 1 && route != 2)) {
    return Status::error("unsupported BindingInfo version or route");
  }
  if (field_count < 6 || static_cast<uint64_t>(field_count) > (bytes.size - reader.position()) / 8) {
    return Status::error("truncated BindingInfo field table");
  }
  std::array<ByteView, 6> fields{};
  for (uint32_t index = 0; index < field_count; ++index) {
    uint32_t offset = 0;
    uint32_t length = 0;
    if (!reader.read(offset) || !reader.read(length)) {
      return Status::error("truncated BindingInfo field descriptor");
    }
    if (static_cast<size_t>(offset) > bytes.size || static_cast<size_t>(length) > bytes.size - offset) {
      return Status::error("BindingInfo field range is outside the block");
    }
    if (index < fields.size()) {
      fields[index] = ByteView(bytes.data + offset, length);
    }
  }
  BindingInfo info;
  info.route_ = static_cast<Route>(route);
  info.claim_index_ = claim_index;
  info.expected_object_type_ = expected_type;
  info.encoding_ = fields[0];
  info.type_name_ = fields[1];
  info.schema_ = fields[2];
  info.claim_id_ = fields[3];
  info.config_json_ = fields[4];
  info.schema_digest_ = fields[5];
  return info;
}

[[nodiscard]] inline Expected<ParseInput> readParseInput(ByteView bytes) {
  if (bytes.data == nullptr || bytes.size < 24) {
    return Status::error("truncated ParseInput v1 header");
  }
  const uint8_t flags = bytes.data[0];
  if ((flags & UINT8_C(0xFE)) != 0) {
    return Status::error("ParseInput v1 has unknown flag bits");
  }
  detail::LittleEndianReader reader(bytes);
  if (!reader.skip(8)) {
    return Status::error("truncated ParseInput v1 flags");
  }
  uint64_t timestamp_bits = 0;
  uint64_t payload_length = 0;
  if (!reader.read(timestamp_bits) || !reader.read(payload_length)) {
    return Status::error("truncated ParseInput v1 header");
  }
  if (payload_length > std::numeric_limits<size_t>::max() || payload_length != bytes.size - reader.position()) {
    return Status::error("ParseInput payload length does not match the block");
  }
  int64_t timestamp = 0;
  std::memcpy(&timestamp, &timestamp_bits, sizeof(timestamp));
  ParseInput input;
  input.has_timestamp = (flags & 1U) != 0;
  input.timestamp_ns = timestamp;
  input.payload = PayloadView(bytes.data + reader.position(), static_cast<size_t>(payload_length));
  return input;
}

class FunctionalParser {
 public:
  virtual ~FunctionalParser() = default;

  [[nodiscard]] virtual Status bind(const BindingInfo& info) = 0;

  [[nodiscard]] virtual Status parseObject(PayloadView, Timestamp, ObjectWriter&) {
    return Status::error("object route is not implemented by this parser module");
  }

  [[nodiscard]] virtual Status parseScalars(PayloadView, Timestamp, ScalarWriter&) {
    return Status::error("scalar route is not implemented by this parser module");
  }
};

namespace detail {

template <typename Parser>
class ModuleExports {
 public:
  struct Instance {
    explicit Instance(uint32_t index) : claim_index(index) {}

    Parser parser;
    uint32_t claim_index = 0;
    Route route = Route::kScalar;
    bool bound = false;
    Blob output;
    std::array<char, kErrorBufferSize> error{};
  };

  struct Slot {
    Instance* instance = nullptr;
    uint32_t generation = 1;
  };

  class TableLock {
   public:
    void lock() noexcept {
#if !defined(__wasm__)
      while (flag_.test_and_set(std::memory_order_acquire)) {}
#endif
    }
    void unlock() noexcept {
#if !defined(__wasm__)
      flag_.clear(std::memory_order_release);
#endif
    }

   private:
#if !defined(__wasm__)
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
#endif
  };

  class LockGuard {
   public:
    explicit LockGuard(TableLock& lock) noexcept : lock_(lock) {
      lock_.lock();
    }
    ~LockGuard() {
      lock_.unlock();
    }

   private:
    TableLock& lock_;
  };

  struct Table {
    ~Table() {
      delete[] slots;
    }

    [[nodiscard]] bool grow() noexcept {
      const size_t next_capacity = capacity == 0 ? size_t{8} : capacity * 2;
      if (next_capacity < capacity || next_capacity > UINT32_MAX) {
        return false;
      }
      auto* allocation = new (std::nothrow) Slot[next_capacity];
      if (allocation == nullptr) {
        return false;
      }
      for (size_t index = 0; index < size; ++index) {
        allocation[index] = slots[index];
      }
      delete[] slots;
      slots = allocation;
      capacity = next_capacity;
      return true;
    }

    Slot* slots = nullptr;
    size_t size = 0;
    size_t capacity = 0;
    TableLock lock;
  };

  [[nodiscard]] static uint64_t create(uint32_t claim_index) noexcept {
    if (claim_index >= static_cast<uint32_t>(PJ_PARSER_MODULE_CLAIM_COUNT)) {
      recordCreationError("claim index is outside the module manifest");
      return kCreationErrorToken;
    }
    PJ_PARSER_MODULE_TRY {
      auto* instance = new (std::nothrow) Instance(claim_index);
      if (instance == nullptr) {
        recordCreationError("parser-module instance allocation failed");
        return kCreationErrorToken;
      }
      auto& table = instances();
      LockGuard guard(table.lock);
      size_t slot_index = 0;
      while (slot_index < table.size &&
             (table.slots[slot_index].instance != nullptr || table.slots[slot_index].generation == 0)) {
        ++slot_index;
      }
      if (slot_index == table.size) {
        if (table.size == table.capacity && !table.grow()) {
          delete instance;
          recordError(creationError(), "parser-module instance-table allocation failed");
          return kCreationErrorToken;
        }
        ++table.size;
      }
      auto& slot = table.slots[slot_index];
      slot.instance = instance;
      if (slot_index >= UINT32_MAX) {
        slot.instance = nullptr;
        delete instance;
        recordError(creationError(), "parser-module instance table is full");
        return kCreationErrorToken;
      }
      return tokenFor(slot_index, slot.generation);
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      recordCreationError("parser-module constructor failed");
      return kCreationErrorToken;
    }
  }

  static void destroy(uint64_t token) noexcept {
    Instance* instance = nullptr;
    {
      auto& table = instances();
      LockGuard guard(table.lock);
      Slot* slot = findSlotLocked(table, token);
      if (slot == nullptr) {
        recordError(badTokenError(), "pj_module_destroy received a stale or unknown instance token");
        return;
      }
      instance = slot->instance;
      slot->instance = nullptr;
      ++slot->generation;
      // A wrapped generation retires the slot permanently so no stale token
      // can become valid again.
    }
    PJ_PARSER_MODULE_TRY {
      delete instance;
    }
    PJ_PARSER_MODULE_CATCH_ALL {}
  }

  [[nodiscard]] static int32_t bind(uint64_t token, uint64_t address, uint64_t length) noexcept {
    Instance* instance = find(token);
    if (instance == nullptr) {
      return failBadToken("pj_module_bind received a stale or unknown instance token");
    }
    if (address == 0 || length > std::numeric_limits<size_t>::max()) {
      return fail(*instance, kModuleMalformedInput, "BindingInfo buffer is unreadable");
    }
    PJ_PARSER_MODULE_TRY {
      auto info = readBindingInfo(
          ByteView(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(address)), static_cast<size_t>(length)));
      if (!info) {
        return fail(*instance, kModuleMalformedInput, info.status().message());
      }
      if (info->claimIndex() != instance->claim_index) {
        return fail(*instance, kModuleBadClaimIndex, "BindingInfo claim index does not match the instance");
      }
      const Status status = instance->parser.bind(*info);
      instance->bound = status.isOk();
      instance->route = info->route();
      if (status.isOk()) {
        instance->error[0] = '\0';
        return kModuleOk;
      }
      recordError(instance->error, status.message());
      return status.isDecline() ? kModuleDecline : kModuleError;
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return fail(*instance, kModuleError, "parser-module bind threw an exception");
    }
  }

  [[nodiscard]] static int32_t parse(
      uint64_t token, uint64_t input_address, uint64_t input_length, uint64_t output_address_pointer,
      uint64_t output_length_pointer) noexcept {
    Instance* instance = find(token);
    if (instance == nullptr) {
      return failBadToken("pj_module_parse received a stale or unknown instance token");
    }
    if (!instance->bound) {
      return fail(*instance, kModuleError, "parser-module instance is not bound");
    }
    if (input_address == 0 || output_address_pointer == 0 || output_length_pointer == 0 ||
        input_length > std::numeric_limits<size_t>::max()) {
      return fail(*instance, kModuleMalformedInput, "ParseInput buffer is unreadable");
    }
    PJ_PARSER_MODULE_TRY {
      auto input = readParseInput(ByteView(
          reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(input_address)), static_cast<size_t>(input_length)));
      if (!input) {
        return fail(*instance, kModuleMalformedInput, input.status().message());
      }

      Expected<Blob> output = Status::error("parser route is invalid");
      Status parsed = Status::error("parser route is invalid");
      if (instance->route == Route::kObject) {
        ObjectWriter writer(input->payload);
        parsed =
            instance->parser.parseObject(input->payload, Timestamp{input->has_timestamp, input->timestamp_ns}, writer);
        if (parsed.isOk()) {
          output = writer.finish();
        }
      } else {
        ScalarWriter writer;
        parsed =
            instance->parser.parseScalars(input->payload, Timestamp{input->has_timestamp, input->timestamp_ns}, writer);
        if (parsed.isOk()) {
          output = writer.finish();
        }
      }
      if (!parsed.isOk()) {
        return fail(*instance, kModuleError, parsed.message());
      }
      if (!output) {
        return fail(*instance, kModuleError, output.status().message());
      }
      instance->output = std::move(*output);
      const uint64_t output_address = addressOf(instance->output.data());
      const uint64_t output_length = instance->output.size();
      std::memcpy(
          reinterpret_cast<void*>(static_cast<uintptr_t>(output_address_pointer)), &output_address,
          sizeof(output_address));
      std::memcpy(
          reinterpret_cast<void*>(static_cast<uintptr_t>(output_length_pointer)), &output_length,
          sizeof(output_length));
      instance->error[0] = '\0';
      return kModuleOk;
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return fail(*instance, kModuleError, "parser-module parse threw an exception");
    }
  }

  [[nodiscard]] static uint64_t lastError(uint64_t token, uint64_t address, uint64_t capacity) noexcept {
    if (address == 0 || capacity == 0 || capacity > std::numeric_limits<size_t>::max()) {
      return 0;
    }
    std::array<char, kErrorBufferSize> source{};
    {
      auto& table = instances();
      LockGuard guard(table.lock);
      if (token == kCreationErrorToken) {
        source = creationError();
      } else if (Slot* slot = findSlotLocked(table, token)) {
        source = slot->instance->error;
      } else {
        source = badTokenError();
      }
    }
    size_t length = 0;
    while (length < source.size() && source[length] != '\0') {
      ++length;
    }
    const size_t written = length < static_cast<size_t>(capacity) ? length : static_cast<size_t>(capacity);
    if (written != 0) {
      std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(address)), source.data(), written);
    }
    return written;
  }

 private:
  [[nodiscard]] static Table& instances() {
    static Table value;
    return value;
  }

  [[nodiscard]] static std::array<char, kErrorBufferSize>& creationError() noexcept {
    static std::array<char, kErrorBufferSize> value{};
    return value;
  }

  [[nodiscard]] static std::array<char, kErrorBufferSize>& badTokenError() noexcept {
    static std::array<char, kErrorBufferSize> value{};
    return value;
  }

  static void recordCreationError(std::string_view message) noexcept {
    auto& table = instances();
    LockGuard guard(table.lock);
    recordError(creationError(), message);
  }

  [[nodiscard]] static uint64_t tokenFor(size_t index, uint32_t generation) noexcept {
    return (static_cast<uint64_t>(generation) << 32U) | static_cast<uint64_t>(index + 1);
  }

  [[nodiscard]] static Slot* findSlotLocked(Table& table, uint64_t token) noexcept {
    const uint32_t encoded_index = static_cast<uint32_t>(token);
    const uint32_t generation = static_cast<uint32_t>(token >> 32U);
    if (encoded_index == 0 || generation == 0) {
      return nullptr;
    }
    const size_t index = static_cast<size_t>(encoded_index - 1);
    if (index >= table.size) {
      return nullptr;
    }
    Slot& slot = table.slots[index];
    return slot.instance != nullptr && slot.generation == generation ? &slot : nullptr;
  }

  [[nodiscard]] static Instance* find(uint64_t token) noexcept {
    // The table lock makes create/destroy safe alongside calls on different
    // instances. Calls and lifecycle changes for the same token remain
    // serialized by the host so the returned instance stays alive.
    auto& table = instances();
    LockGuard guard(table.lock);
    Slot* slot = findSlotLocked(table, token);
    return slot == nullptr ? nullptr : slot->instance;
  }

  [[nodiscard]] static int32_t failBadToken(std::string_view message) noexcept {
    auto& table = instances();
    LockGuard guard(table.lock);
    recordError(badTokenError(), message);
    return kModuleBadToken;
  }

  [[nodiscard]] static int32_t fail(Instance& instance, int32_t code, std::string_view message) noexcept {
    recordError(instance.error, message);
    return code;
  }
};

}  // namespace detail
}  // namespace pj

#if defined(__wasm__)
#define PJ_PARSER_MODULE_METADATA_EXPORTS
#else
#define PJ_PARSER_MODULE_METADATA_EXPORTS                               \
  PJ_PARSER_MODULE_EXPORT uint64_t pj_module_manifest_addr() noexcept { \
    return ::pj::detail::addressOf(::pj::detail::kBuiltManifest);       \
  }                                                                     \
  PJ_PARSER_MODULE_EXPORT uint64_t pj_module_manifest_len() noexcept {  \
    return sizeof(::pj::detail::kBuiltManifest) - 1;                    \
  }
#endif

#define PJ_FUNCTIONAL_PARSER(ParserClass)                                                                  \
  extern "C" {                                                                                             \
  PJ_PARSER_MODULE_EXPORT uint32_t pj_module_abi() noexcept {                                              \
    return ::pj::kModuleAbiVersion;                                                                        \
  }                                                                                                        \
  PJ_PARSER_MODULE_EXPORT uint64_t pj_module_create(uint32_t claim_index) noexcept {                       \
    return ::pj::detail::ModuleExports<ParserClass>::create(claim_index);                                  \
  }                                                                                                        \
  PJ_PARSER_MODULE_EXPORT void pj_module_destroy(uint64_t instance) noexcept {                             \
    ::pj::detail::ModuleExports<ParserClass>::destroy(instance);                                           \
  }                                                                                                        \
  PJ_PARSER_MODULE_EXPORT int32_t                                                                          \
  pj_module_bind(uint64_t instance, uint64_t info_address, uint64_t info_length) noexcept {                \
    return ::pj::detail::ModuleExports<ParserClass>::bind(instance, info_address, info_length);            \
  }                                                                                                        \
  PJ_PARSER_MODULE_EXPORT int32_t pj_module_parse(                                                         \
      uint64_t instance, uint64_t input_address, uint64_t input_length, uint64_t output_address_pointer,   \
      uint64_t output_length_pointer) noexcept {                                                           \
    return ::pj::detail::ModuleExports<ParserClass>::parse(                                                \
        instance, input_address, input_length, output_address_pointer, output_length_pointer);             \
  }                                                                                                        \
  PJ_PARSER_MODULE_EXPORT uint64_t                                                                         \
  pj_module_last_error(uint64_t instance, uint64_t buffer_address, uint64_t buffer_capacity) noexcept {    \
    return ::pj::detail::ModuleExports<ParserClass>::lastError(instance, buffer_address, buffer_capacity); \
  }                                                                                                        \
  PJ_PARSER_MODULE_EXPORT uint64_t pj_module_alloc(uint64_t size) noexcept {                               \
    if (size > static_cast<uint64_t>(SIZE_MAX)) {                                                          \
      return 0;                                                                                            \
    }                                                                                                      \
    auto* allocation = new (std::nothrow) uint8_t[static_cast<size_t>(size)];                              \
    return ::pj::detail::addressOf(allocation);                                                            \
  }                                                                                                        \
  PJ_PARSER_MODULE_EXPORT void pj_module_free(uint64_t address, uint64_t) noexcept {                       \
    delete[] reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));                                  \
  }                                                                                                        \
  PJ_PARSER_MODULE_METADATA_EXPORTS                                                                        \
  }
