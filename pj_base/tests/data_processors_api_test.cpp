// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/span.hpp"

namespace PJ {
namespace {

// Fake host for the UNIFIED pj.data_processors.v1 service: records the last
// create/remove/validate call and serves list/config from host-owned storage (so the
// borrowed-string lifetime contract can be tested). create_data_processor resolves
// output sink names — echoing provided outputs, or an auto-named topic when outputs is
// empty (ephemeral preview).
struct FakeDataProcessorsHost {
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

  std::vector<std::string> resolved_storage;                   // host storage out_topics point into
  std::string auto_topic = "__markers__/__preview__/anomaly";  // host-named ephemeral sink

  bool validate_called = false;
  bool validate_should_fail = false;
  std::string last_validate_kind;
  std::string last_validate_script;

  // --- create_data_processor_v2 / submit_evaluation / poll_evaluation / release_evaluation ---

  bool create_v2_called = false;
  uint32_t poll_state = PJ_EVALUATION_STATE_COMPLETED;  // what poll_evaluation reports
  bool submit_called = false;
  uint32_t last_request_struct_size = 0;
  uint32_t last_request_flags = 0;
  uint32_t last_request_time_flags = 0;
  uint32_t last_request_reserved = 0;
  int64_t last_request_window_start_ns = 0;
  int64_t last_request_window_end_ns = 0;
  int64_t last_request_time_ns = 0;
  std::string last_request_id;
  std::string last_request_kind;
  std::string last_request_language;
  std::string last_request_script;
  std::string last_request_params;
  std::string last_request_label;
  std::vector<std::string> last_request_inputs;
  std::vector<std::pair<std::string, std::string>> last_request_outputs;  // (name, type)

  std::vector<std::string> v2_resolved_storage;  // host storage out_topics point into
  std::string v2_auto_topic = "__on_demand__/__preview__/finding";

  struct Evaluation {
    std::string json;
  };
  std::unordered_map<uint64_t, Evaluation> evaluations;  // handle -> stored report
  uint64_t next_handle = 1;
};

bool dpCreate(
    void* ctx, PJ_string_view_t id, PJ_string_view_t kind, PJ_string_view_t language, const PJ_string_view_t* inputs,
    uint64_t input_count, const PJ_string_view_t* outputs, uint64_t output_count, PJ_string_view_t script,
    PJ_string_view_t params_json, uint32_t flags, PJ_string_view_t* out_topics, uint64_t out_topics_capacity,
    uint64_t* out_topics_count, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  if (self->create_should_fail) {
    if (out_error != nullptr) {
      sdk::fillError(out_error, 1, "data_processors", "create boom");
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

bool dpValidate(
    void* ctx, PJ_string_view_t kind, PJ_string_view_t /*language*/, PJ_string_view_t script,
    PJ_string_view_t /*params_json*/, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  self->validate_called = true;
  self->last_validate_kind = std::string(sdk::toStringView(kind));
  self->last_validate_script = std::string(sdk::toStringView(script));
  if (self->validate_should_fail) {
    if (out_error != nullptr) {
      sdk::fillError(out_error, 1, "data_processors", "syntax boom");
    }
    return false;
  }
  return true;
}

bool dpCreateV2(
    void* ctx, const PJ_data_processor_request_t* request, PJ_string_view_t* out_topics, uint64_t out_topics_capacity,
    uint64_t* out_topics_count, PJ_error_t* /*out_error*/) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  self->create_v2_called = true;
  // Copy every borrowed string/array out of `request` immediately: the SDK contract
  // says they are borrowed for the duration of the call only.
  self->last_request_struct_size = request->struct_size;
  self->last_request_flags = request->flags;
  self->last_request_time_flags = request->time_flags;
  self->last_request_reserved = request->reserved;
  self->last_request_window_start_ns = request->window_start_ns;
  self->last_request_window_end_ns = request->window_end_ns;
  self->last_request_time_ns = request->time_ns;
  self->last_request_id = std::string(sdk::toStringView(request->id));
  self->last_request_kind = std::string(sdk::toStringView(request->kind));
  self->last_request_language = std::string(sdk::toStringView(request->language));
  self->last_request_script = std::string(sdk::toStringView(request->script));
  self->last_request_params = std::string(sdk::toStringView(request->params_json));
  self->last_request_label = std::string(sdk::toStringView(request->label));
  self->last_request_inputs.clear();
  for (uint64_t i = 0; i < request->input_count; ++i) {
    self->last_request_inputs.emplace_back(sdk::toStringView(request->inputs[i]));
  }
  self->last_request_outputs.clear();
  for (uint64_t i = 0; i < request->output_count; ++i) {
    self->last_request_outputs.emplace_back(
        std::string(sdk::toStringView(request->outputs[i].name)),
        std::string(sdk::toStringView(request->outputs[i].type)));
  }

  // Resolve sink names: echo provided output names, else host-named (auto preview).
  self->v2_resolved_storage.clear();
  if (request->output_count > 0) {
    for (uint64_t i = 0; i < request->output_count; ++i) {
      self->v2_resolved_storage.emplace_back(sdk::toStringView(request->outputs[i].name));
    }
  } else {
    self->v2_resolved_storage.push_back(self->v2_auto_topic);
  }
  if (out_topics_count != nullptr) {
    *out_topics_count = self->v2_resolved_storage.size();
  }
  if (out_topics != nullptr) {
    const uint64_t n = std::min<uint64_t>(out_topics_capacity, self->v2_resolved_storage.size());
    for (uint64_t i = 0; i < n; ++i) {
      out_topics[i] = sdk::toAbiString(self->v2_resolved_storage[i]);
    }
  }
  return true;
}

bool dpSubmitEvaluation(
    void* ctx, const PJ_data_processor_request_t* request, const PJ_evaluation_budget_t* /*budget*/,
    uint64_t* out_handle, PJ_error_t* /*out_error*/) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  self->submit_called = true;
  self->last_request_id = std::string(sdk::toStringView(request->id));
  self->last_request_time_flags = request->time_flags;
  self->last_request_time_ns = request->time_ns;

  // "Phase 0" fake: completes inline, one canned bundle.
  const uint64_t handle = self->next_handle++;
  self->evaluations[handle] = FakeDataProcessorsHost::Evaluation{
      .json = R"({"coverage":{"start_ns":0,"end_ns":0,"evaluated_until_ns":null,"candidates":1,)"
              R"("evaluated":1,"cache_hits":0,"complete":true,"stopped":"complete"},)"
              R"("bundles":[{"requested_ns":0,"stamp_ns":0,"from_cache":false,"revision":1,)"
              R"("inputs":[],"outputs":{}}]})"};
  *out_handle = handle;
  return true;
}

bool dpPollEvaluation(
    void* ctx, uint64_t handle, uint32_t* out_state, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  auto it = self->evaluations.find(handle);
  if (it == self->evaluations.end()) {
    if (out_error != nullptr) {
      sdk::fillError(out_error, 1, "data_processors", "unknown evaluation handle");
    }
    return false;
  }
  *out_state = self->poll_state;
  *out_json = sdk::toAbiString(it->second.json);
  return true;
}

bool dpReleaseEvaluation(void* ctx, uint64_t handle, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  auto it = self->evaluations.find(handle);
  if (it == self->evaluations.end()) {
    if (out_error != nullptr) {
      sdk::fillError(out_error, 1, "data_processors", "unknown evaluation handle");
    }
    return false;
  }
  self->evaluations.erase(it);
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
      .validate_data_processor_script = dpValidate,
      .create_data_processor_v2 = dpCreateV2,
      .submit_evaluation = dpSubmitEvaluation,
      .poll_evaluation = dpPollEvaluation,
      .release_evaluation = dpReleaseEvaluation,
  };
}

// --- Unified create() + kind="transform" shim ------------------------------------

TEST(DataProcessorsApiTest, CreateTransformForwardsAllArgsIntact) {
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
  EXPECT_EQ(host.last_kind, "transform");
  EXPECT_EQ(host.last_language, "luau");
  EXPECT_EQ(host.last_flags, 0u);
  ASSERT_EQ(host.last_inputs.size(), 2u);
  EXPECT_EQ(host.last_inputs[0], "pose/orientation/x");
  EXPECT_EQ(host.last_inputs[1], "pose/orientation/y");
  ASSERT_EQ(host.last_outputs.size(), 2u);
  EXPECT_EQ(host.last_outputs[0], "pose/rpy/roll");
  EXPECT_EQ(host.last_outputs[1], "pose/rpy/pitch");
  EXPECT_EQ(host.last_script, "-- pj-script: lua\nreturn {}");
  EXPECT_EQ(host.last_params, R"({"window":10})");
}

// --- kind="markers" via create() and the createMarkers shim ----------------------

TEST(DataProcessorsApiTest, CreateMarkersForwardsKindAndReturnsResolvedTopics) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  const std::string_view inputs[] = {"imu/accel/x", "imu/accel/y"};
  const std::string_view outputs[] = {"/anomaly/region"};
  auto topics = view.create(
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
TEST(DataProcessorsApiTest, EphemeralMarkerPreviewAutoNamesAndReturnsTopic) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  const std::string_view inputs[] = {"in"};
  auto topics = view.createMarkers(
      "anomaly/__preview__", PJ::Span<const std::string_view>(inputs), /*output_marker_topic=*/"",
      "createPointMarker(0.0)", "{}", PJ_DATA_PROCESSOR_FLAG_EPHEMERAL);

  ASSERT_TRUE(topics) << topics.error();
  EXPECT_EQ(host.last_kind, "markers");
  EXPECT_EQ(host.last_flags, PJ_DATA_PROCESSOR_FLAG_EPHEMERAL);
  EXPECT_TRUE(host.last_outputs.empty());
  ASSERT_EQ(topics->size(), 1u);
  const std::string expected = host.auto_topic;
  host.auto_topic = "CLOBBERED";  // owned copy survives
  EXPECT_EQ((*topics)[0], expected);
}

// createEphemeralTransform shim sets kind="transform" + the EPHEMERAL flag.
TEST(DataProcessorsApiTest, EphemeralTransformSetsFlag) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  const std::string_view outputs[] = {"preview/out"};
  auto status = view.createEphemeralTransform(
      "preview", PJ::Span<const std::string_view>{}, PJ::Span<const std::string_view>(outputs), "s", "{}");

  ASSERT_TRUE(status) << status.error();
  EXPECT_EQ(host.last_kind, "transform");
  EXPECT_EQ(host.last_flags, PJ_DATA_PROCESSOR_FLAG_EPHEMERAL);
}

TEST(DataProcessorsApiTest, CreateFailureSurfacesError) {
  FakeDataProcessorsHost host;
  host.create_should_fail = true;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto topics = view.createMarkers("x", PJ::Span<const std::string_view>{}, "__global__", "s", "{}");

  EXPECT_FALSE(topics);
  EXPECT_NE(topics.error().find("create boom"), std::string::npos);
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
  host.recipe_storage = R"({"kind":"markers","inputs":["a"],"outputs":["__global__"],"params":{}})";
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

// The createTransform shim fails fast on an empty output list: a "transform" creates
// NAMED catalog topics, so >= 1 output is mandatory. The guard lives in the view so a
// misuse never reaches the vtable (the host enforces it authoritatively too).
TEST(DataProcessorsApiTest, CreateTransformRejectsEmptyOutputs) {
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
  auto topics = view.create(
      "fft", "transform", "luau", PJ::Span<const std::string_view>{}, PJ::Span<const std::string_view>(outputs),
      std::string_view(blob.data(), blob.size()), "{}");

  ASSERT_TRUE(topics) << topics.error();
  EXPECT_EQ(host.last_script.size(), blob.size());  // not truncated at the first NUL
  EXPECT_EQ(host.last_script, blob);
}

// validate_data_processor_script forwards kind + script and reports success; a compile
// failure surfaces the host's error message (drives the editor red/green semaphore).
TEST(DataProcessorsApiTest, ValidateForwardsKindAndSucceeds) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.validateScript("markers", "luau", "createPointMarker(0.0)"));
  EXPECT_TRUE(host.validate_called);
  EXPECT_EQ(host.last_validate_kind, "markers");
  EXPECT_EQ(host.last_validate_script, "createPointMarker(0.0)");
}

TEST(DataProcessorsApiTest, ValidateFailureSurfacesError) {
  FakeDataProcessorsHost host;
  host.validate_should_fail = true;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto status = view.validateScript("markers", "luau", "this is not lua");
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("syntax boom"), std::string::npos);
}

// --- createV2 / submitEvaluation / pollEvaluation / releaseEvaluation ------------

TEST(DataProcessorsApiTest, CreateV2ForwardsTypedOutputsAndInstant) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  sdk::DataProcessorRequest request;
  request.id = "nearest_cloud";
  request.kind = "on_demand";
  request.language = "luau";
  request.script = "return {}";
  request.params_json = "{}";
  request.label = "Nearest cloud";
  request.inputs = {"lidar/points"};
  request.outputs = {sdk::DataProcessorOutput{"cloud", "kPointCloud"}};
  request.instant_ns = 123456789;

  auto topics = view.createV2(request);
  ASSERT_TRUE(topics) << topics.error();
  EXPECT_TRUE(host.create_v2_called);
  EXPECT_EQ(host.last_request_id, "nearest_cloud");
  EXPECT_EQ(host.last_request_kind, "on_demand");
  EXPECT_EQ(host.last_request_label, "Nearest cloud");
  ASSERT_EQ(host.last_request_outputs.size(), 1u);
  EXPECT_EQ(host.last_request_outputs[0].first, "cloud");
  EXPECT_EQ(host.last_request_outputs[0].second, "kPointCloud");
  EXPECT_EQ(host.last_request_time_flags, static_cast<uint32_t>(PJ_DATA_PROCESSOR_TIME_FLAG_INSTANT));
  EXPECT_EQ(host.last_request_time_ns, 123456789);
  ASSERT_EQ(topics->size(), 1u);
  EXPECT_EQ((*topics)[0], "cloud");
}

TEST(DataProcessorsApiTest, CreateV2OnOldHostReportsNotSupported) {
  FakeDataProcessorsHost host;
  auto vtable = makeVtable();
  vtable.struct_size = offsetof(PJ_data_processors_host_vtable_t, create_data_processor_v2);
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  sdk::DataProcessorRequest request;
  request.id = "x";
  request.kind = "on_demand";

  auto topics = view.createV2(request);
  EXPECT_FALSE(topics);
  EXPECT_NE(topics.error().find("create_data_processor_v2"), std::string::npos);
  EXPECT_FALSE(host.create_v2_called);
}

TEST(DataProcessorsApiTest, HasTypedRequestsReflectsTailSlots) {
  FakeDataProcessorsHost host;
  auto vtable = makeVtable();
  sdk::DataProcessorsHostView full(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  EXPECT_TRUE(full.hasTypedRequests());
  EXPECT_FALSE(sdk::DataProcessorsHostView{}.hasTypedRequests());

  // A host whose struct_size ends before the typed-request tail.
  vtable.struct_size = offsetof(PJ_data_processors_host_vtable_t, create_data_processor_v2);
  sdk::DataProcessorsHostView old_host(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  EXPECT_FALSE(old_host.hasTypedRequests());

  // A host that covers the tail but stops short of the last slot.
  vtable.struct_size = offsetof(PJ_data_processors_host_vtable_t, release_evaluation);
  sdk::DataProcessorsHostView partial(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  EXPECT_FALSE(partial.hasTypedRequests());
}

TEST(DataProcessorsApiTest, SubmitPollReleaseRoundTrip) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  sdk::DataProcessorRequest request;
  request.id = "finding";
  request.kind = "on_demand";
  request.instant_ns = 10;

  auto handle = view.submitEvaluation(request);
  ASSERT_TRUE(handle) << handle.error();
  EXPECT_TRUE(host.submit_called);

  auto poll = view.pollEvaluation(*handle);
  ASSERT_TRUE(poll) << poll.error();
  EXPECT_EQ(poll->state, sdk::EvaluationState::kCompleted);
  EXPECT_NE(poll->json.find("\"coverage\""), std::string::npos);

  ASSERT_TRUE(view.releaseEvaluation(*handle));
  auto second_release = view.releaseEvaluation(*handle);
  EXPECT_FALSE(second_release);
}

TEST(DataProcessorsApiTest, PollUnknownHandleIsAnError) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto poll = view.pollEvaluation(/*handle=*/999);
  EXPECT_FALSE(poll);
}

TEST(DataProcessorsApiTest, PollMapsEveryKnownStateAndRejectsAnUnknownOne) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  sdk::DataProcessorRequest request;
  request.id = "finding";
  request.kind = "on_demand";
  request.instant_ns = 10;
  auto handle = view.submitEvaluation(request);
  ASSERT_TRUE(handle) << handle.error();

  const std::pair<uint32_t, sdk::EvaluationState> known[] = {
      {PJ_EVALUATION_STATE_PENDING, sdk::EvaluationState::kPending},
      {PJ_EVALUATION_STATE_COMPLETED, sdk::EvaluationState::kCompleted},
      {PJ_EVALUATION_STATE_FAILED, sdk::EvaluationState::kFailed},
      {PJ_EVALUATION_STATE_CANCELLED, sdk::EvaluationState::kCancelled},
  };
  for (const auto& [raw, expected] : known) {
    host.poll_state = raw;
    auto poll = view.pollEvaluation(*handle);
    ASSERT_TRUE(poll) << poll.error();
    EXPECT_EQ(poll->state, expected);
  }

  // An unknown state must not read as "pending": a caller would poll forever.
  host.poll_state = 99;
  auto poll = view.pollEvaluation(*handle);
  ASSERT_FALSE(poll);
  EXPECT_NE(poll.error().find("unknown evaluation state 99"), std::string::npos);
}

TEST(DataProcessorsApiTest, RequestStructSizeAndFlagsAreSet) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  sdk::DataProcessorRequest request;
  request.id = "x";
  request.kind = "on_demand";
  request.instant_ns = 42;

  auto topics = view.createV2(request);
  ASSERT_TRUE(topics) << topics.error();
  EXPECT_EQ(host.last_request_struct_size, sizeof(PJ_data_processor_request_t));
  EXPECT_EQ(host.last_request_time_flags, static_cast<uint32_t>(PJ_DATA_PROCESSOR_TIME_FLAG_INSTANT));
  EXPECT_EQ(host.last_request_reserved, 0u);
}

}  // namespace
}  // namespace PJ
