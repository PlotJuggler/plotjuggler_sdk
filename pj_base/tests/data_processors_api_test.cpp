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

// Fake host: records the last create/remove call and serves list/config from
// host-owned storage (so the borrowed-string lifetime contract can be tested).
struct FakeDataProcessorsHost {
  bool create_called = false;
  bool create_should_fail = false;
  std::string last_id;
  std::vector<std::string> last_inputs;
  std::vector<std::string> last_outputs;
  std::string last_script;
  std::string last_params;

  std::string removed_id;

  std::vector<std::string> stored_ids;  // host storage the listed views point into
  std::string recipe_storage;           // host storage the recipe view points into
};

bool dpCreate(
    void* ctx, PJ_string_view_t id, const PJ_string_view_t* inputs, uint64_t input_count,
    const PJ_string_view_t* outputs, uint64_t output_count, PJ_string_view_t script, PJ_string_view_t params_json,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  if (self->create_should_fail) {
    if (out_error != nullptr) {
      sdk::fillError(out_error, 1, "data_processors", "create boom");
    }
    return false;
  }
  self->create_called = true;
  self->last_id = std::string(sdk::toStringView(id));
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
  return true;
}

bool dpRemove(void* ctx, PJ_string_view_t id, PJ_error_t* /*out_error*/) noexcept {
  static_cast<FakeDataProcessorsHost*>(ctx)->removed_id = std::string(sdk::toStringView(id));
  return true;
}

bool dpList(
    void* ctx, PJ_string_view_t* out_ids, uint64_t capacity, uint64_t* out_count, PJ_error_t* /*out_error*/) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  *out_count = self->stored_ids.size();
  const uint64_t n = std::min<uint64_t>(capacity, self->stored_ids.size());
  for (uint64_t i = 0; i < n; ++i) {
    out_ids[i] = sdk::toAbiString(self->stored_ids[i]);
  }
  return true;
}

bool dpConfig(
    void* ctx, PJ_string_view_t /*id*/, PJ_string_view_t* out_recipe_json, PJ_error_t* /*out_error*/) noexcept {
  out_recipe_json[0] = sdk::toAbiString(static_cast<FakeDataProcessorsHost*>(ctx)->recipe_storage);
  return true;
}

PJ_data_processors_host_vtable_t makeVtable() {
  return PJ_data_processors_host_vtable_t{
      .protocol_version = 1,
      .struct_size = sizeof(PJ_data_processors_host_vtable_t),
      .create_data_processor = dpCreate,
      .remove_data_processor = dpRemove,
      .list_data_processor_ids = dpList,
      .data_processor_config = dpConfig,
  };
}

TEST(DataProcessorsApiTest, CreateForwardsAllArgsIntact) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  const std::string_view inputs[] = {"pose/orientation/x", "pose/orientation/y"};
  const std::string_view outputs[] = {"pose/rpy/roll", "pose/rpy/pitch"};
  auto status = view.createTransform(
      "quat_rpy", PJ::Span<const std::string_view>(inputs), PJ::Span<const std::string_view>(outputs),
      "-- pj-script: lua\nreturn {}", R"({"window":10})");

  ASSERT_TRUE(status) << status.error();
  EXPECT_TRUE(host.create_called);
  EXPECT_EQ(host.last_id, "quat_rpy");
  ASSERT_EQ(host.last_inputs.size(), 2u);
  EXPECT_EQ(host.last_inputs[0], "pose/orientation/x");
  EXPECT_EQ(host.last_inputs[1], "pose/orientation/y");
  ASSERT_EQ(host.last_outputs.size(), 2u);
  EXPECT_EQ(host.last_outputs[0], "pose/rpy/roll");
  EXPECT_EQ(host.last_outputs[1], "pose/rpy/pitch");
  EXPECT_EQ(host.last_script, "-- pj-script: lua\nreturn {}");
  EXPECT_EQ(host.last_params, R"({"window":10})");
}

TEST(DataProcessorsApiTest, CreateFailureSurfacesError) {
  FakeDataProcessorsHost host;
  host.create_should_fail = true;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  const std::string_view outputs[] = {"out"};
  auto status = view.createTransform(
      "x", PJ::Span<const std::string_view>{}, PJ::Span<const std::string_view>(outputs), "s", "{}");

  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("create boom"), std::string::npos);
  EXPECT_FALSE(host.create_called);
}

TEST(DataProcessorsApiTest, RemoveForwardsId) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.remove("quat_rpy"));
  EXPECT_EQ(host.removed_id, "quat_rpy");
}

TEST(DataProcessorsApiTest, ListCountThenFillReturnsOwnedCopies) {
  FakeDataProcessorsHost host;
  host.stored_ids = {"alpha", "beta", "gamma"};
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto ids = view.list();
  ASSERT_TRUE(ids) << ids.error();
  ASSERT_EQ(ids->size(), 3u);
  EXPECT_EQ((*ids)[0], "alpha");
  EXPECT_EQ((*ids)[2], "gamma");

  // Owned copies: mutating host storage must not change the returned vector.
  host.stored_ids[0] = "clobbered";
  EXPECT_EQ((*ids)[0], "alpha");
}

TEST(DataProcessorsApiTest, RecipeOfCopiesBorrowedJson) {
  FakeDataProcessorsHost host;
  host.recipe_storage = R"({"inputs":["a"],"outputs":["b"],"params":{}})";
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto recipe = view.recipeOf("id");
  ASSERT_TRUE(recipe) << recipe.error();
  const std::string expected = host.recipe_storage;

  // The returned string is an owned copy: clobbering the host buffer is invisible.
  host.recipe_storage = "CLOBBERED";
  EXPECT_EQ(*recipe, expected);
}

TEST(DataProcessorsApiTest, UnboundViewReportsNotBound) {
  sdk::DataProcessorsHostView view;  // default-constructed = not bound
  EXPECT_FALSE(view.valid());

  const std::string_view outputs[] = {"out"};
  auto status = view.createTransform(
      "x", PJ::Span<const std::string_view>{}, PJ::Span<const std::string_view>(outputs), "s", "{}");

  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("not bound"), std::string::npos);
}

// The view fails fast on an empty output list: this service creates NAMED catalog
// topics (transforms), so >= 1 output is mandatory. The guard lives in the view so
// a misuse never reaches the vtable (the host enforces it authoritatively too).
TEST(DataProcessorsApiTest, CreateRejectsEmptyOutputs) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto status = view.createTransform(
      "x", PJ::Span<const std::string_view>{}, PJ::Span<const std::string_view>{}, "-- pj-script: lua\nreturn {}",
      "{}");

  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("output"), std::string::npos);
  EXPECT_FALSE(host.create_called);  // rejected before the ABI call
}

// Regression guard: `script` / `params_json` are PJ_string_view_t {data, size} --
// binary-safe, NOT NUL-terminated. A payload carrying embedded NULs and the WASM
// "\0asm" magic round-trips byte-for-byte. This keeps the future WASM/Python
// backend door open: a script slot may carry a binary module, so nothing on the
// path may "optimize" the marshalling to strlen.
TEST(DataProcessorsApiTest, BinarySafePayloadRoundTrips) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  const char wasm_bytes[] = {'\0', 'a', 's', 'm', '\x01', '\0', '\0', '\0', 'X', 'Y'};
  const std::string blob(wasm_bytes, sizeof(wasm_bytes));  // 10 bytes, 3 embedded NULs
  ASSERT_EQ(blob.size(), 10u);

  const std::string_view outputs[] = {"spectrum"};
  auto status = view.createTransform(
      "fft", PJ::Span<const std::string_view>{}, PJ::Span<const std::string_view>(outputs),
      std::string_view(blob.data(), blob.size()), "{}");

  ASSERT_TRUE(status) << status.error();
  EXPECT_EQ(host.last_script.size(), blob.size());  // not truncated at the first NUL
  EXPECT_EQ(host.last_script, blob);
}

}  // namespace
}  // namespace PJ
