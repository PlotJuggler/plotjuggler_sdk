// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "native_parser_module_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "pj_base/builtin/grid_map_codec.hpp"
#include "pj_base/builtin/point_cloud_codec.hpp"
#include "pj_base/builtin_object_abi.h"
#include "pj_base/parser_module_abi.h"
#include "pj_base/span.hpp"

#if defined(_WIN32)
#define PJ_FIXTURE_EXPORT __declspec(dllexport)
#else
#define PJ_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

namespace {

using enum pj_fixture::ClaimIndex;

constexpr char kManifest[] = R"({
  "module_abi":1,
  "id":"org.plotjuggler.test.native-module",
  "name":"Native module test fixture",
  "version":"1.0.0",
  "claims":[
    {"claim_id":"object","encoding":"protobuf","type_name":"fixture.Object","routes":["object"],"object_type":"kPointCloud","priority":0},
    {"claim_id":"scalar","encoding":"protobuf","type_name":"fixture.Scalar","routes":["scalar"],"priority":0},
    {"claim_id":"decline","encoding":"protobuf","type_name":"fixture.Decline","routes":["scalar"],"priority":0},
    {"claim_id":"create-failure","encoding":"protobuf","type_name":"fixture.CreateFailure","routes":["scalar"],"priority":0},
    {"claim_id":"data-error","encoding":"protobuf","type_name":"fixture.DataError","routes":["scalar"],"priority":0},
    {"claim_id":"malformed","encoding":"protobuf","type_name":"fixture.Malformed","routes":["scalar"],"priority":0},
    {"claim_id":"splice","encoding":"protobuf","type_name":"fixture.Splice","routes":["object"],"object_type":"kPointCloud","priority":0},
    {"claim_id":"splice-oob","encoding":"protobuf","type_name":"fixture.SpliceOob","routes":["object"],"object_type":"kPointCloud","priority":0},
    {"claim_id":"splice-ineligible","encoding":"protobuf","type_name":"fixture.SpliceIneligible","routes":["object"],"object_type":"kPointCloud","priority":0},
    {"claim_id":"bad-token","encoding":"protobuf","type_name":"fixture.BadToken","routes":["scalar"],"priority":0},
    {"claim_id":"route-mismatch","encoding":"protobuf","type_name":"fixture.RouteMismatch","routes":["scalar"],"priority":0},
    {"claim_id":"type-mismatch","encoding":"protobuf","type_name":"fixture.TypeMismatch","routes":["object"],"object_type":"kPointCloud","priority":0},
    {"claim_id":"splice-grid-map","encoding":"protobuf","type_name":"fixture.SpliceGridMap","routes":["object"],"object_type":"kGridMap","priority":0}
  ]
})";

struct Instance {
  explicit Instance(uint32_t selected_claim) : claim_index(selected_claim) {}

  uint32_t claim_index = 0;
  PJ::parser_module::Route route = PJ::parser_module::Route::kScalar;
  std::string error;
  std::vector<uint8_t> output;
};

std::unordered_set<Instance*>& liveInstances() {
  static std::unordered_set<Instance*> instances;
  return instances;
}

std::string& creationError() {
  static std::string error;
  return error;
}

uint64_t addressOf(const void* pointer) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

Instance* findInstance(uint64_t token) {
  auto* instance = reinterpret_cast<Instance*>(static_cast<uintptr_t>(token));
  return liveInstances().contains(instance) ? instance : nullptr;
}

int32_t fail(Instance* instance, int32_t code, std::string message) {
  if (instance != nullptr) {
    instance->error = std::move(message);
  }
  return code;
}

int32_t storeOutput(Instance& instance, PJ::parser_module::OutputDescriptorV1 descriptor) {
  auto output = PJ::parser_module::writeOutputDescriptorV1(descriptor);
  if (!output) {
    return fail(&instance, PJ_MODULE_ERR_GENERIC, output.error());
  }
  instance.output = std::move(*output);
  return PJ_MODULE_OK;
}

}  // namespace

extern "C" {

PJ_FIXTURE_EXPORT uint32_t pj_module_abi() {
#if defined(PJ_FIXTURE_WRONG_ABI)
  return PJ_PARSER_MODULE_ABI_VERSION + 1;
#else
  return PJ_PARSER_MODULE_ABI_VERSION;
#endif
}

PJ_FIXTURE_EXPORT uint64_t pj_module_create(uint32_t claim_index) {
  if (claim_index >= kClaimCount) {
    creationError() = "claim index is outside the fixture manifest";
    return PJ_MODULE_CREATION_ERROR_TOKEN;
  }
  if (claim_index == kCreateFailure) {
    creationError() = "fixture creation failure";
    return PJ_MODULE_CREATION_ERROR_TOKEN;
  }
  const bool claim_is_live = std::any_of(
      liveInstances().begin(), liveInstances().end(),
      [claim_index](const Instance* instance) { return instance->claim_index == claim_index; });
  if (claim_is_live) {
    creationError() = "fixture permits one live instance per claim";
    return PJ_MODULE_CREATION_ERROR_TOKEN;
  }
  auto* instance = new Instance(claim_index);
  liveInstances().insert(instance);
  return addressOf(instance);
}

PJ_FIXTURE_EXPORT void pj_module_destroy(uint64_t token) {
  auto* instance = findInstance(token);
  if (instance != nullptr) {
    liveInstances().erase(instance);
    delete instance;
  }
}

PJ_FIXTURE_EXPORT int32_t pj_module_bind(uint64_t token, uint64_t info_addr, uint64_t info_len) {
  auto* instance = findInstance(token);
  if (instance == nullptr) {
    return PJ_MODULE_ERR_BAD_TOKEN;
  }
  if (info_addr == 0 || info_len > static_cast<uint64_t>(SIZE_MAX)) {
    return fail(instance, PJ_MODULE_ERR_MALFORMED_INPUT, "binding buffer is unreadable");
  }
  const auto* data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(info_addr));
  auto info = PJ::parser_module::readBindingInfoV1(PJ::Span<const uint8_t>(data, static_cast<size_t>(info_len)));
  if (!info || info->claim_index != instance->claim_index) {
    return fail(instance, PJ_MODULE_ERR_MALFORMED_INPUT, "fixture rejected malformed binding info");
  }
  instance->route = info->route;
  if (instance->claim_index == kDecline) {
    return fail(instance, PJ_MODULE_DECLINE, "fixture bind declined");
  }
  instance->error.clear();
  return PJ_MODULE_OK;
}

PJ_FIXTURE_EXPORT int32_t pj_module_parse(
    uint64_t token, uint64_t input_addr, uint64_t input_len, uint64_t output_addr_ptr, uint64_t output_len_ptr) {
  auto* instance = findInstance(token);
  if (instance == nullptr) {
    return PJ_MODULE_ERR_BAD_TOKEN;
  }
  if (instance->claim_index == kBadToken) {
    return fail(instance, PJ_MODULE_ERR_BAD_TOKEN, "fixture forced bad token");
  }
  if (input_addr == 0 || output_addr_ptr == 0 || output_len_ptr == 0 || input_len > static_cast<uint64_t>(SIZE_MAX)) {
    return fail(instance, PJ_MODULE_ERR_MALFORMED_INPUT, "parse buffer is unreadable");
  }
  const auto* input_data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(input_addr));
  auto input = PJ::parser_module::readParseInputV1(PJ::Span<const uint8_t>(input_data, static_cast<size_t>(input_len)));
  if (!input) {
    return fail(instance, PJ_MODULE_ERR_MALFORMED_INPUT, input.error());
  }

  if (instance->claim_index == kDataError) {
    return fail(instance, PJ_MODULE_ERR_MALFORMED_INPUT, "fixture payload decode error");
  }
  if (instance->claim_index == kMalformed) {
    // Deliberately not a decodable output descriptor.
    instance->output = {1, 2, 3};
  } else {
    // ObjectOutputV1::wire is a borrowed span, so its storage must outlive the
    // storeOutput call that serializes it.
    std::vector<uint8_t> wire;
    PJ::parser_module::OutputDescriptorV1 descriptor;
    switch (instance->claim_index) {
      case kScalar:
        descriptor = PJ::parser_module::ScalarOutputV1{
            .has_timestamp = true,
            .timestamp_ns = 42,
            .fields =
                {
                    {.name = "temperature", .value = 21.5},
                    {.name = "status", .value = std::string_view("ready")},
                },
        };
        break;
      case kRouteMismatch:
        descriptor = PJ::parser_module::ObjectOutputV1{
            .object_type = PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD,
            .splice = std::nullopt,
            .wire = {},
        };
        break;
      case kTypeMismatch:
        descriptor = PJ::parser_module::ObjectOutputV1{
            .object_type = PJ_BUILTIN_OBJECT_TYPE_IMAGE,
            .splice = std::nullopt,
            .wire = {},
        };
        break;
      case kSplice:
      case kSpliceOutOfBounds:
      case kSpliceIneligible:
        descriptor = PJ::parser_module::ObjectOutputV1{
            .object_type = PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD,
            .splice =
                PJ::parser_module::ObjectSpliceV1{
                    .field_number = instance->claim_index == kSpliceIneligible ? uint32_t{8} : uint32_t{9},
                    .input_offset = instance->claim_index == kSpliceOutOfBounds
                                        ? static_cast<uint64_t>(input->payload.size()) + 1
                                        : 1,
                    .input_length = 2,
                },
            .wire = {},
        };
        break;
      case kSpliceGridMap: {
        PJ::sdk::GridMap grid;  // header only: the two cell bytes arrive as the splice
        grid.column_count = 2;
        grid.row_count = 1;
        grid.cell_stride = 1;
        grid.row_stride = 2;
        grid.fields.push_back(
            {.name = "cost", .offset = 0, .datatype = PJ::sdk::PointField::Datatype::kUint8, .count = 1});
        wire = PJ::serializeGridMap(grid);
        descriptor = PJ::parser_module::ObjectOutputV1{
            .object_type = PJ_BUILTIN_OBJECT_TYPE_GRID_MAP,
            .splice = PJ::parser_module::ObjectSpliceV1{.field_number = 10, .input_offset = 1, .input_length = 2},
            .wire = wire,
        };
        break;
      }
      default: {
        const std::array<uint8_t, 4> point_data{1, 2, 3, 4};
        PJ::sdk::PointCloud cloud;
        cloud.width = 1;
        cloud.height = 1;
        cloud.point_step = 4;
        cloud.row_step = 4;
        cloud.data = point_data;
        wire = PJ::serializePointCloud(cloud);
        descriptor = PJ::parser_module::ObjectOutputV1{
            .object_type = PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD,
            .splice = std::nullopt,
            .wire = wire,
        };
        break;
      }
    }
    if (const int32_t result = storeOutput(*instance, std::move(descriptor)); result != PJ_MODULE_OK) {
      return result;
    }
  }

  *reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(output_addr_ptr)) = addressOf(instance->output.data());
  *reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(output_len_ptr)) = instance->output.size();
  return PJ_MODULE_OK;
}

PJ_FIXTURE_EXPORT uint64_t pj_module_last_error(uint64_t token, uint64_t buffer_addr, uint64_t buffer_cap) {
  const Instance* instance = findInstance(token);
  const std::string* error =
      token == PJ_MODULE_CREATION_ERROR_TOKEN ? &creationError() : (instance == nullptr ? nullptr : &instance->error);
  if (error == nullptr || buffer_addr == 0 || buffer_cap == 0) {
    return 0;
  }
  const uint64_t size = std::min<uint64_t>(error->size(), buffer_cap);
  std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(buffer_addr)), error->data(), static_cast<size_t>(size));
  return size;
}

PJ_FIXTURE_EXPORT uint64_t pj_module_alloc(uint64_t size) {
  if (size > static_cast<uint64_t>(SIZE_MAX)) {
    return 0;
  }
  auto* allocation = new (std::nothrow) uint8_t[static_cast<size_t>(size)];
  return addressOf(allocation);
}

#if !defined(PJ_FIXTURE_OMIT_FREE)
PJ_FIXTURE_EXPORT void pj_module_free(uint64_t address, uint64_t) {
  delete[] reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
}
#endif

PJ_FIXTURE_EXPORT uint64_t pj_module_manifest_addr() {
#if defined(PJ_FIXTURE_UNREADABLE_MANIFEST)
  return 0;
#else
  return addressOf(kManifest);
#endif
}

PJ_FIXTURE_EXPORT uint64_t pj_module_manifest_len() {
  return sizeof(kManifest) - 1;
}

}  // extern "C"
