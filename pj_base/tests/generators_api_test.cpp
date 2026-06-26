// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/span.hpp"

namespace PJ {
namespace {

// Fake host: records the last create/remove/validate call and serves list/config
// from host-owned storage (so the borrowed-string lifetime contract can be tested).
// create_generator resolves output sink names — echoing provided outputs, or an
// auto-named topic when outputs is empty (ephemeral preview).
struct FakeGeneratorsHost {
  bool create_called = false;
  bool create_should_fail = false;
  std::string last_id;
  std::string last_kind;
  std::string last_language;
  std::vector<std::string> last_inputs;
  std::vector<std::string> last_outputs;
  std::string last_script;
  std::string last_params;
  uint32_t last_flags = 0;

  std::string removed_id;

  std::vector<std::string> stored_ids;  // host storage the listed views point into
  std::string recipe_storage;           // host storage the recipe view points into

  std::vector<std::string> resolved_storage;            // host storage out_topics point into
  std::string auto_topic = "__markers__/__preview__/anomaly";  // host-named ephemeral sink

  bool validate_called = false;
  bool validate_should_fail = false;
  std::string last_validate_kind;
  std::string last_validate_script;
};

bool genCreate(
    void* ctx, PJ_string_view_t id, PJ_string_view_t kind, PJ_string_view_t language, const PJ_string_view_t* inputs,
    uint64_t input_count, const PJ_string_view_t* outputs, uint64_t output_count, PJ_string_view_t script,
    PJ_string_view_t params_json, uint32_t flags, PJ_string_view_t* out_topics, uint64_t out_topics_capacity,
    uint64_t* out_topics_count, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeGeneratorsHost*>(ctx);
  if (self->create_should_fail) {
    if (out_error != nullptr) {
      sdk::fillError(out_error, 1, "generators", "create boom");
    }
    return false;
  }
  self->create_called = true;
  self->last_id = std::string(sdk::toStringView(id));
  self->last_kind = std::string(sdk::toStringView(kind));
  self->last_language = std::string(sdk::toStringView(language));
  self->last_inputs.clear();
  for (uint64_t i = 0; i < input_count; ++i) {
    self->last_inputs.emplace_back(sdk::toStringView(inputs[i]));
  }
  self->last_outputs.clear();
  for (uint64_t i = 0; i < output_count; ++i) {
    self->last_outputs.emplace_back(sdk::toStringView(outputs[i]));
  }
  self->last_script = std::string(sdk::toStringView(script));
  self->last_params = std::string(sdk::toStringView(params_json));
  self->last_flags = flags;

  // Resolve sink names: echo provided outputs, else host-named (ephemeral preview).
  self->resolved_storage.clear();
  if (output_count > 0) {
    for (uint64_t i = 0; i < output_count; ++i) {
      self->resolved_storage.emplace_back(sdk::toStringView(outputs[i]));
    }
  } else {
    self->resolved_storage.push_back(self->auto_topic);
  }
  if (out_topics_count != nullptr) {
    *out_topics_count = self->resolved_storage.size();
  }
  if (out_topics != nullptr) {
    const uint64_t n = std::min<uint64_t>(out_topics_capacity, self->resolved_storage.size());
    for (uint64_t i = 0; i < n; ++i) {
      out_topics[i] = sdk::toAbiString(self->resolved_storage[i]);
    }
  }
  return true;
}

bool genRemove(void* ctx, PJ_string_view_t id, PJ_error_t* /*out_error*/) noexcept {
  static_cast<FakeGeneratorsHost*>(ctx)->removed_id = std::string(sdk::toStringView(id));
  return true;
}

bool genList(
    void* ctx, PJ_string_view_t* out_ids, uint64_t capacity, uint64_t* out_count, PJ_error_t* /*out_error*/) noexcept {
  auto* self = static_cast<FakeGeneratorsHost*>(ctx);
  *out_count = self->stored_ids.size();
  const uint64_t n = std::min<uint64_t>(capacity, self->stored_ids.size());
  for (uint64_t i = 0; i < n; ++i) {
    out_ids[i] = sdk::toAbiString(self->stored_ids[i]);
  }
  return true;
}

bool genConfig(
    void* ctx, PJ_string_view_t /*id*/, PJ_string_view_t* out_recipe_json, PJ_error_t* /*out_error*/) noexcept {
  out_recipe_json[0] = sdk::toAbiString(static_cast<FakeGeneratorsHost*>(ctx)->recipe_storage);
  return true;
}

bool genValidate(
    void* ctx, PJ_string_view_t kind, PJ_string_view_t /*language*/, PJ_string_view_t script,
    PJ_string_view_t /*params_json*/, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeGeneratorsHost*>(ctx);
  self->validate_called = true;
  self->last_validate_kind = std::string(sdk::toStringView(kind));
  self->last_validate_script = std::string(sdk::toStringView(script));
  if (self->validate_should_fail) {
    if (out_error != nullptr) {
      sdk::fillError(out_error, 1, "generators", "syntax boom");
    }
    return false;
  }
  return true;
}

PJ_generators_host_vtable_t makeVtable() {
  return PJ_generators_host_vtable_t{
      .protocol_version = 1,
      .struct_size = sizeof(PJ_generators_host_vtable_t),
      .create_generator = genCreate,
      .remove_generator = genRemove,
      .list_generator_ids = genList,
      .generator_config = genConfig,
      .validate_script = genValidate,
  };
}

TEST(GeneratorsApiTest, CreateForwardsAllArgsAndReturnsResolvedTopics) {
  FakeGeneratorsHost host;
  const auto vtable = makeVtable();
  sdk::GeneratorsHostView view(PJ_generators_host_t{.ctx = &host, .vtable = &vtable});

  const std::string_view inputs[] = {"imu/accel/x", "imu/accel/y"};
  const std::string_view outputs[] = {"/anomaly/region"};
  auto topics = view.createGenerator(
      "vib_detect", "markers", "luau", PJ::Span<const std::string_view>(inputs),
      PJ::Span<const std::string_view>(outputs), "createPointMarker(0, 1)", R"({"threshold":3.0})");

  ASSERT_TRUE(topics) << topics.error();
  EXPECT_TRUE(host.create_called);
  EXPECT_EQ(host.last_id, "vib_detect");
  EXPECT_EQ(host.last_kind, "markers");
  EXPECT_EQ(host.last_language, "luau");
  ASSERT_EQ(host.last_inputs.size(), 2u);
  EXPECT_EQ(host.last_inputs[0], "imu/accel/x");
  ASSERT_EQ(host.last_outputs.size(), 1u);
  EXPECT_EQ(host.last_outputs[0], "/anomaly/region");
  EXPECT_EQ(host.last_params, R"({"threshold":3.0})");
  EXPECT_EQ(host.last_flags, 0u);
  ASSERT_EQ(topics->size(), 1u);
  EXPECT_EQ((*topics)[0], "/anomaly/region");
}

// Ephemeral preview: no outputs supplied + EPHEMERAL flag → host auto-names the sink
// and returns it. The returned name is an owned copy (survives host mutation).
TEST(GeneratorsApiTest, EphemeralPreviewAutoNamesAndReturnsTopic) {
  FakeGeneratorsHost host;
  const auto vtable = makeVtable();
  sdk::GeneratorsHostView view(PJ_generators_host_t{.ctx = &host, .vtable = &vtable});

  const std::string_view inputs[] = {"in"};
  auto topics = view.createMarkerGenerator(
      "anomaly/__preview__", PJ::Span<const std::string_view>(inputs), /*output=*/"", "createPointMarker(0.0)", "{}",
      PJ_GENERATOR_FLAG_EPHEMERAL);

  ASSERT_TRUE(topics) << topics.error();
  EXPECT_EQ(host.last_kind, "markers");
  EXPECT_EQ(host.last_flags, PJ_GENERATOR_FLAG_EPHEMERAL);
  EXPECT_TRUE(host.last_outputs.empty());
  ASSERT_EQ(topics->size(), 1u);
  const std::string expected = host.auto_topic;
  host.auto_topic = "CLOBBERED";  // owned copy survives
  EXPECT_EQ((*topics)[0], expected);
}

TEST(GeneratorsApiTest, CreateFailureSurfacesError) {
  FakeGeneratorsHost host;
  host.create_should_fail = true;
  const auto vtable = makeVtable();
  sdk::GeneratorsHostView view(PJ_generators_host_t{.ctx = &host, .vtable = &vtable});

  auto topics = view.createMarkerGenerator(
      "x", PJ::Span<const std::string_view>{}, "__global__", "s", "{}");

  EXPECT_FALSE(topics);
  EXPECT_NE(topics.error().find("create boom"), std::string::npos);
  EXPECT_FALSE(host.create_called);
}

TEST(GeneratorsApiTest, RemoveForwardsId) {
  FakeGeneratorsHost host;
  const auto vtable = makeVtable();
  sdk::GeneratorsHostView view(PJ_generators_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.remove("vib_detect"));
  EXPECT_EQ(host.removed_id, "vib_detect");
}

TEST(GeneratorsApiTest, ListCountThenFillReturnsOwnedCopies) {
  FakeGeneratorsHost host;
  host.stored_ids = {"alpha", "beta", "gamma"};
  const auto vtable = makeVtable();
  sdk::GeneratorsHostView view(PJ_generators_host_t{.ctx = &host, .vtable = &vtable});

  auto ids = view.list();
  ASSERT_TRUE(ids) << ids.error();
  ASSERT_EQ(ids->size(), 3u);
  EXPECT_EQ((*ids)[0], "alpha");
  EXPECT_EQ((*ids)[2], "gamma");

  host.stored_ids[0] = "clobbered";
  EXPECT_EQ((*ids)[0], "alpha");  // owned copy
}

TEST(GeneratorsApiTest, ConfigOfCopiesBorrowedJson) {
  FakeGeneratorsHost host;
  host.recipe_storage = R"({"kind":"markers","inputs":["a"],"outputs":["__global__"],"params":{}})";
  const auto vtable = makeVtable();
  sdk::GeneratorsHostView view(PJ_generators_host_t{.ctx = &host, .vtable = &vtable});

  auto recipe = view.configOf("id");
  ASSERT_TRUE(recipe) << recipe.error();
  const std::string expected = host.recipe_storage;

  host.recipe_storage = "CLOBBERED";
  EXPECT_EQ(*recipe, expected);  // owned copy
}

TEST(GeneratorsApiTest, UnboundViewReportsNotBound) {
  sdk::GeneratorsHostView view;  // default-constructed = not bound
  EXPECT_FALSE(view.valid());

  auto topics = view.createMarkerGenerator(
      "x", PJ::Span<const std::string_view>{}, "__global__", "s", "{}");

  EXPECT_FALSE(topics);
  EXPECT_NE(topics.error().find("not bound"), std::string::npos);
}

// Regression guard: `script` / `params_json` are PJ_string_view_t {data,size} --
// binary-safe, NOT NUL-terminated. A payload carrying embedded NULs and the WASM
// "\0asm" magic round-trips byte-for-byte (keeps the WASM/Python backend door open).
TEST(GeneratorsApiTest, BinarySafePayloadRoundTrips) {
  FakeGeneratorsHost host;
  const auto vtable = makeVtable();
  sdk::GeneratorsHostView view(PJ_generators_host_t{.ctx = &host, .vtable = &vtable});

  const char wasm_bytes[] = {'\0', 'a', 's', 'm', '\x01', '\0', '\0', '\0', 'X', 'Y'};
  const std::string blob(wasm_bytes, sizeof(wasm_bytes));  // 10 bytes, 3 embedded NULs
  ASSERT_EQ(blob.size(), 10u);

  const std::string_view outputs[] = {"__global__"};
  auto topics = view.createGenerator(
      "fft", "markers", "luau", PJ::Span<const std::string_view>{}, PJ::Span<const std::string_view>(outputs),
      std::string_view(blob.data(), blob.size()), "{}");

  ASSERT_TRUE(topics) << topics.error();
  EXPECT_EQ(host.last_script.size(), blob.size());  // not truncated at the first NUL
  EXPECT_EQ(host.last_script, blob);
}

// validate_script forwards kind + script and reports success; a compile failure
// surfaces the host's error message (drives the editor red/green semaphore).
TEST(GeneratorsApiTest, ValidateForwardsAndSucceeds) {
  FakeGeneratorsHost host;
  const auto vtable = makeVtable();
  sdk::GeneratorsHostView view(PJ_generators_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.validateScript("markers", "luau", "createPointMarker(0.0)"));
  EXPECT_TRUE(host.validate_called);
  EXPECT_EQ(host.last_validate_kind, "markers");
  EXPECT_EQ(host.last_validate_script, "createPointMarker(0.0)");
}

TEST(GeneratorsApiTest, ValidateFailureSurfacesError) {
  FakeGeneratorsHost host;
  host.validate_should_fail = true;
  const auto vtable = makeVtable();
  sdk::GeneratorsHostView view(PJ_generators_host_t{.ctx = &host, .vtable = &vtable});

  auto status = view.validateScript("markers", "luau", "this is not lua");
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("syntax boom"), std::string::npos);
}

}  // namespace
}  // namespace PJ
