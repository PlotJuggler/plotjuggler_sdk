// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/data_processors_host.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/span.hpp"
#include "pj_plugins/sdk/evaluation_report.hpp"

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

  bool check_called = false;
  bool check_should_fail = false;                 // the call itself cannot be made
  uint32_t check_status = PJ_SCRIPT_CHECK_VALID;  // what the fake reports, as the raw ABI value
  std::string check_reason;                       // written to out_error for INVALID / UNCHECKED
  std::string last_check_kind;
  std::string last_check_language;
  std::string last_check_script;
  std::string last_check_params;

  // --- create_data_processor_v2 / submit_evaluation / poll_evaluation / release_evaluation ---
  // Both request slots decode through sdk::readDataProcessorRequest, like a real host,
  // and the fake enforces the few per-kind rules a client must be able to rely on.

  bool create_v2_called = false;
  bool submit_called = false;
  uint32_t last_request_struct_size = 0;  // as sent, before the decoder normalizes it
  // The decoded request. Its string views dangle once the call returns: read the copies below.
  PJ_data_processor_request_t last_request{};
  std::string last_request_id;
  std::vector<std::pair<std::string, std::string>> last_request_outputs;  // (name, type)
  PJ_evaluation_budget_t last_budget{};
  std::set<std::string> installed_on_demand;  // ids created with kind "on_demand"

  std::vector<std::string> v2_resolved_storage;  // host storage out_topics point into
  uint32_t poll_state = PJ_EVALUATION_STATE_COMPLETED;
  std::set<uint64_t> live_handles;
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
  if (sdk::toStringView(kind) == "on_demand") {
    sdk::fillError(out_error, PJ_ERROR_CODE_REJECTED, "data_processors", "on_demand needs create_data_processor_v2");
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

bool dpCheck(
    void* ctx, PJ_string_view_t kind, PJ_string_view_t language, PJ_string_view_t script, PJ_string_view_t params_json,
    uint32_t* out_status, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  self->check_called = true;
  self->last_check_kind = std::string(sdk::toStringView(kind));
  self->last_check_language = std::string(sdk::toStringView(language));
  self->last_check_script = std::string(sdk::toStringView(script));
  self->last_check_params = std::string(sdk::toStringView(params_json));
  if (self->check_should_fail) {
    sdk::fillError(out_error, 1, "data_processors", "unknown language");
    return false;
  }
  *out_status = self->check_status;
  if (!self->check_reason.empty()) {
    sdk::fillError(out_error, 1, "data_processors", self->check_reason);
  }
  return true;
}

bool reject(PJ_error_t* out_error, const std::string& reason) {
  sdk::fillError(out_error, PJ_ERROR_CODE_REJECTED, "data_processors", reason);
  return false;
}

// The fake's terminal reports, in the one shape poll_evaluation promises.
constexpr std::string_view kCompletedReport =
    R"({"coverage":{"candidates":1,"evaluated":1,"stopped":"complete","gaps":[]},)"
    R"("bundles":[{"requested_ns":10,"inputs":{"lidar":8},"outputs":{"count":{"status":"ok","value":3}}}]})";
constexpr std::string_view kFailedReport =
    R"({"coverage":{"candidates":1,"evaluated":0,"stopped":"error","error":"script error","gaps":[]},"bundles":[]})";

// Decodes `request` for `slot` and copies what the tests read out of it.
bool recordRequest(
    FakeDataProcessorsHost& self, const PJ_data_processor_request_t* request, sdk::DataProcessorSlot slot,
    PJ_error_t* out_error) {
  self.last_request_struct_size = request != nullptr ? request->struct_size : 0;
  auto decoded = sdk::readDataProcessorRequest(request, slot);
  if (!decoded) {
    return reject(out_error, decoded.error());
  }
  self.last_request = *decoded;
  self.last_request_id = std::string(sdk::toStringView(decoded->id));
  self.last_request_outputs.clear();
  for (uint64_t i = 0; i < decoded->output_count; ++i) {
    self.last_request_outputs.emplace_back(
        std::string(sdk::toStringView(decoded->outputs[i].name)),
        std::string(sdk::toStringView(decoded->outputs[i].type)));
  }
  return true;
}

// An on_demand object output is published as "<owner>/<id>/<name>"; number and string
// outputs keep their bare name, so a client that rebuilds paths from names is caught.
bool dpCreateV2(
    void* ctx, const PJ_data_processor_request_t* request, PJ_string_view_t* out_topics, uint64_t out_topics_capacity,
    uint64_t* out_topics_count, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  if (!recordRequest(*self, request, sdk::DataProcessorSlot::kCreateV2, out_error)) {
    return false;
  }
  const bool on_demand = sdk::toStringView(self->last_request.kind) == "on_demand";
  if (on_demand && (self->last_request.time_flags & PJ_DATA_PROCESSOR_TIME_FLAG_WINDOW) != 0) {
    return reject(out_error, "on_demand takes no WINDOW on create: use submit_evaluation");
  }
  if (on_demand) {
    self->installed_on_demand.insert(self->last_request_id);
  }
  self->create_v2_called = true;
  self->v2_resolved_storage.clear();
  for (const auto& [name, type] : self->last_request_outputs) {
    const bool object = on_demand && type != "number" && type != "string";
    self->v2_resolved_storage.push_back(object ? "fake/" + self->last_request_id + "/" + name : name);
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
    void* ctx, const PJ_data_processor_request_t* request, const PJ_evaluation_budget_t* budget, uint64_t* out_handle,
    PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  if (!recordRequest(*self, request, sdk::DataProcessorSlot::kSubmitEvaluation, out_error)) {
    return false;
  }
  if (sdk::toStringView(self->last_request.kind) != "on_demand") {
    return reject(out_error, "only kind 'on_demand' is evaluated");
  }
  if (self->last_request.script.size == 0 && self->installed_on_demand.count(self->last_request_id) == 0) {
    return reject(out_error, "unknown data processor id '" + self->last_request_id + "'");
  }
  auto decoded_budget = sdk::readEvaluationBudget(budget);
  if (!decoded_budget) {
    return reject(out_error, decoded_budget.error());
  }
  self->submit_called = true;
  self->last_budget = *decoded_budget;
  *out_handle = self->next_handle++;
  self->live_handles.insert(*out_handle);
  return true;
}

// Reports by `poll_state`: nothing while pending, then one of the two terminal reports.
bool dpPollEvaluation(
    void* ctx, uint64_t handle, uint32_t* out_state, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeDataProcessorsHost*>(ctx);
  if (self->live_handles.count(handle) == 0) {
    return reject(out_error, "unknown evaluation handle");
  }
  *out_state = self->poll_state;
  const std::string_view report = self->poll_state == PJ_EVALUATION_STATE_PENDING  ? std::string_view{}
                                  : self->poll_state == PJ_EVALUATION_STATE_FAILED ? kFailedReport
                                                                                   : kCompletedReport;
  *out_json = sdk::toAbiString(report);
  return true;
}

bool dpReleaseEvaluation(void* ctx, uint64_t handle, PJ_error_t* out_error) noexcept {
  if (static_cast<FakeDataProcessorsHost*>(ctx)->live_handles.erase(handle) == 0) {
    return reject(out_error, "unknown evaluation handle");
  }
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
      .check_data_processor_script = dpCheck,
  };
}

// The recipe of the on_demand node "finding": a point cloud and a count.
sdk::DataProcessorRequest onDemandRecipe() {
  sdk::DataProcessorRequest request;
  request.id = "finding";
  request.kind = "on_demand";
  request.language = "luau";
  request.script = "return {}";
  request.inputs = {"lidar/points"};
  request.outputs = {{"cloud", "kPointCloud"}, {"count", "number"}};
  return request;
}

// An evaluation of the installed node "finding" at t=10: no recipe fields.
sdk::DataProcessorRequest installedSubmit() {
  sdk::DataProcessorRequest request;
  request.id = "finding";
  request.kind = "on_demand";
  request.instant_ns = 10;
  return request;
}

// A raw scripted request the decoder accepts on both slots: one typed output, INSTANT.
struct RawRequest {
  PJ_string_view_t input = sdk::toAbiString("lidar/points");
  PJ_data_processor_output_t output{sdk::toAbiString("cloud"), sdk::toAbiString("kPointCloud"), {0, 0}};
  PJ_data_processor_request_t raw{};

  RawRequest() {
    raw.struct_size = sizeof(PJ_data_processor_request_t);
    raw.id = sdk::toAbiString("finding");
    raw.kind = sdk::toAbiString("on_demand");
    raw.language = sdk::toAbiString("luau");
    raw.script = sdk::toAbiString("return {}");
    raw.params_json = sdk::toAbiString("{}");
    raw.inputs = &input;
    raw.input_count = 1;
    raw.outputs = &output;
    raw.output_count = 1;
    raw.time_flags = PJ_DATA_PROCESSOR_TIME_FLAG_INSTANT;
    raw.time_ns = 42;
  }
  RawRequest(const RawRequest&) = delete;  // `raw` points into this object
  RawRequest& operator=(const RawRequest&) = delete;

  // Turns it into an evaluation of the installed node `id`: recipe fields cleared.
  void makeInstalledSubmit(std::string_view id) {
    raw.id = sdk::toAbiString(id);
    raw.language = {};
    raw.script = {};
    raw.params_json = {};
    raw.inputs = nullptr;
    raw.input_count = 0;
    raw.outputs = nullptr;
    raw.output_count = 0;
  }
};

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

// --- checkScript ------------------------------------------------------------------

// check_data_processor_script forwards its arguments and reports all three outcomes;
// the reason travels with INVALID and UNCHECKED, and a VALID script has none.
TEST(DataProcessorsApiTest, CheckScriptForwardsAllThreeStates) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  struct Case {
    uint32_t raw;
    sdk::ScriptCheckStatus expected;
    const char* reason;
  };
  const Case cases[] = {
      {PJ_SCRIPT_CHECK_VALID, sdk::ScriptCheckStatus::kValid, ""},
      {PJ_SCRIPT_CHECK_INVALID, sdk::ScriptCheckStatus::kInvalid, "syntax boom"},
      {PJ_SCRIPT_CHECK_UNCHECKED, sdk::ScriptCheckStatus::kUnchecked, "helper still starting"},
  };
  for (const Case& test_case : cases) {
    host.check_status = test_case.raw;
    host.check_reason = test_case.reason;
    auto check = view.checkScript("on_demand", "python", "return {}", R"({"k":1})");
    ASSERT_TRUE(check) << check.error();
    EXPECT_EQ(check->status, test_case.expected);
    if (test_case.expected == sdk::ScriptCheckStatus::kValid) {
      EXPECT_TRUE(check->message.empty());
    } else {
      EXPECT_NE(check->message.find(test_case.reason), std::string::npos) << check->message;
    }
  }
  EXPECT_TRUE(host.check_called);
  EXPECT_FALSE(host.validate_called);  // a host with the slot is never asked the two-way question
  EXPECT_EQ(host.last_check_kind, "on_demand");
  EXPECT_EQ(host.last_check_language, "python");
  EXPECT_EQ(host.last_check_script, "return {}");
  EXPECT_EQ(host.last_check_params, R"({"k":1})");
}

// A call the host could not make (e.g. an unknown language) is an error, not a status.
TEST(DataProcessorsApiTest, CheckScriptCallFailureIsAnError) {
  FakeDataProcessorsHost host;
  host.check_should_fail = true;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto check = view.checkScript("on_demand", "cobol", "x");
  ASSERT_FALSE(check);
  EXPECT_NE(check.error().find("unknown language"), std::string::npos);
}

// A status this SDK does not know is an error, never read as VALID.
TEST(DataProcessorsApiTest, CheckScriptUnknownStatusIsAnError) {
  FakeDataProcessorsHost host;
  host.check_status = 3;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto check = view.checkScript("on_demand", "luau", "return {}");
  ASSERT_FALSE(check);
  EXPECT_NE(check.error().find("unknown script check status 3"), std::string::npos);
}

// A host that predates the slot is asked validate_data_processor_script instead: the
// answer is VALID or INVALID, never UNCHECKED.
TEST(DataProcessorsApiTest, CheckScriptFallsBackToValidateOnOldHost) {
  FakeDataProcessorsHost host;
  auto vtable = makeVtable();
  vtable.struct_size = offsetof(PJ_data_processors_host_vtable_t, check_data_processor_script);
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto valid = view.checkScript("markers", "luau", "createPointMarker(0.0)");
  ASSERT_TRUE(valid) << valid.error();
  EXPECT_EQ(valid->status, sdk::ScriptCheckStatus::kValid);
  EXPECT_TRUE(valid->message.empty());
  EXPECT_TRUE(host.validate_called);
  EXPECT_EQ(host.last_validate_kind, "markers");
  EXPECT_EQ(host.last_validate_script, "createPointMarker(0.0)");

  host.validate_should_fail = true;
  auto invalid = view.checkScript("markers", "luau", "this is not lua");
  ASSERT_TRUE(invalid) << invalid.error();
  EXPECT_EQ(invalid->status, sdk::ScriptCheckStatus::kInvalid);
  EXPECT_NE(invalid->message.find("syntax boom"), std::string::npos);
  EXPECT_FALSE(host.check_called);
}

// A host whose vtable is large enough for the slot but leaves it NULL is treated like
// one that predates it.
TEST(DataProcessorsApiTest, CheckScriptWithNullSlotFallsBackToValidate) {
  FakeDataProcessorsHost host;
  auto vtable = makeVtable();
  vtable.check_data_processor_script = nullptr;
  ASSERT_EQ(vtable.struct_size, sizeof(PJ_data_processors_host_vtable_t));
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto valid = view.checkScript("markers", "luau", "createPointMarker(0.0)");
  ASSERT_TRUE(valid) << valid.error();
  EXPECT_EQ(valid->status, sdk::ScriptCheckStatus::kValid);

  host.validate_should_fail = true;
  auto invalid = view.checkScript("markers", "luau", "this is not lua");
  ASSERT_TRUE(invalid) << invalid.error();
  EXPECT_EQ(invalid->status, sdk::ScriptCheckStatus::kInvalid);
  EXPECT_NE(invalid->message.find("syntax boom"), std::string::npos);
  EXPECT_FALSE(host.check_called);
}

// A host that leaves out_error untouched for UNCHECKED gives no reason: the message is
// empty, not the "unspecified error" placeholder of an error with nothing in it.
TEST(DataProcessorsApiTest, CheckScriptUncheckedWithoutReasonHasEmptyMessage) {
  FakeDataProcessorsHost host;
  host.check_status = PJ_SCRIPT_CHECK_UNCHECKED;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto check = view.checkScript("on_demand", "python", "return {}");
  ASSERT_TRUE(check) << check.error();
  EXPECT_EQ(check->status, sdk::ScriptCheckStatus::kUnchecked);
  EXPECT_TRUE(check->message.empty()) << check->message;
}

TEST(DataProcessorsApiTest, CheckScriptWithoutAnyValidationSlotIsAnError) {
  FakeDataProcessorsHost host;
  auto vtable = makeVtable();
  vtable.struct_size = offsetof(PJ_data_processors_host_vtable_t, validate_data_processor_script);
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  EXPECT_FALSE(view.checkScript("markers", "luau", "x"));
}

// --- createV2 / submitEvaluation / pollEvaluation / releaseEvaluation ------------

TEST(DataProcessorsApiTest, CreateV2ForwardsTypedOutputsAndReturnsTheirPhysicalPaths) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  sdk::DataProcessorRequest request = onDemandRecipe();
  request.instant_ns = 123456789;

  auto topics = view.createV2(request);
  ASSERT_TRUE(topics) << topics.error();
  EXPECT_TRUE(host.create_v2_called);
  EXPECT_EQ(host.last_request_id, "finding");
  ASSERT_EQ(host.last_request_outputs.size(), 2u);
  EXPECT_EQ(host.last_request_outputs[0], std::make_pair(std::string("cloud"), std::string("kPointCloud")));
  EXPECT_EQ(host.last_request_outputs[1], std::make_pair(std::string("count"), std::string("number")));
  EXPECT_EQ(host.last_request.time_flags, static_cast<uint32_t>(PJ_DATA_PROCESSOR_TIME_FLAG_INSTANT));
  EXPECT_EQ(host.last_request.time_ns, 123456789);
  EXPECT_EQ(host.last_request_struct_size, sizeof(PJ_data_processor_request_t));
  EXPECT_EQ(host.last_request.dataset_id, 0u);  // default: inputs resolve by name
  ASSERT_EQ(topics->size(), 2u);
  EXPECT_EQ((*topics)[0], "fake/finding/cloud");  // object output: the published topic
  EXPECT_EQ((*topics)[1], "count");               // number output: the bare name
}

TEST(DataProcessorsApiTest, CreateV2ForwardsANonzeroDatasetId) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  sdk::DataProcessorRequest request = onDemandRecipe();
  request.dataset_id = 7;

  ASSERT_TRUE(view.createV2(request));
  EXPECT_EQ(host.last_request.dataset_id, 7u);
}

TEST(DataProcessorsApiTest, CreateV2OnOldHostReportsNotSupported) {
  FakeDataProcessorsHost host;
  auto vtable = makeVtable();
  vtable.struct_size = offsetof(PJ_data_processors_host_vtable_t, create_data_processor_v2);
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  auto topics = view.createV2(onDemandRecipe());
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

  // A full-sized vtable with any one of the four tail slots NULL is not a typed-request host.
  const auto with_null = [&](auto PJ_data_processors_host_vtable_t::* slot) {
    auto holey = vtable;
    holey.*slot = nullptr;
    return sdk::DataProcessorsHostView(PJ_data_processors_host_t{.ctx = &host, .vtable = &holey}).hasTypedRequests();
  };
  EXPECT_FALSE(with_null(&PJ_data_processors_host_vtable_t::create_data_processor_v2));
  EXPECT_FALSE(with_null(&PJ_data_processors_host_vtable_t::submit_evaluation));
  EXPECT_FALSE(with_null(&PJ_data_processors_host_vtable_t::poll_evaluation));
  EXPECT_FALSE(with_null(&PJ_data_processors_host_vtable_t::release_evaluation));

  // A host whose struct_size ends before the typed-request tail.
  vtable.struct_size = offsetof(PJ_data_processors_host_vtable_t, create_data_processor_v2);
  sdk::DataProcessorsHostView old_host(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  EXPECT_FALSE(old_host.hasTypedRequests());

  // A host that covers the tail but stops short of the last slot.
  vtable.struct_size = offsetof(PJ_data_processors_host_vtable_t, release_evaluation);
  sdk::DataProcessorsHostView partial(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  EXPECT_FALSE(partial.hasTypedRequests());
}

TEST(DataProcessorsApiTest, WindowAndInstantTogetherAreRejected) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  ASSERT_TRUE(view.createV2(onDemandRecipe()));

  sdk::DataProcessorRequest request = installedSubmit();
  request.window = std::make_pair(int64_t{0}, int64_t{100});

  auto handle = view.submitEvaluation(request);
  ASSERT_FALSE(handle);
  EXPECT_NE(handle.error().find("mutually exclusive"), std::string::npos) << handle.error();
  EXPECT_FALSE(host.submit_called);
}

TEST(DataProcessorsApiTest, SubmitPollReleaseRoundTrip) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  ASSERT_TRUE(view.createV2(onDemandRecipe()));

  auto handle = view.submitEvaluation(installedSubmit(), sdk::EvaluationBudget{.max_millis = 50});
  ASSERT_TRUE(handle) << handle.error();
  EXPECT_TRUE(host.submit_called);
  EXPECT_EQ(host.last_budget.max_millis, 50u);
  EXPECT_EQ(host.last_budget.reserved, 0u);

  ASSERT_TRUE(view.pollEvaluation(*handle));
  ASSERT_TRUE(view.releaseEvaluation(*handle));
  EXPECT_FALSE(view.releaseEvaluation(*handle));
  EXPECT_FALSE(view.pollEvaluation(*handle));
}

// A scripted submit is ephemeral by definition: it needs no EPHEMERAL bit, and it
// installs nothing, so HISTORY_EXEMPT (a property of an installed node) is refused.
TEST(DataProcessorsApiTest, ScriptedSubmitImpliesEphemeralAndRejectsHistoryExempt) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});

  sdk::DataProcessorRequest request = onDemandRecipe();
  request.id = "trial";
  request.instant_ns = 10;
  ASSERT_TRUE(view.submitEvaluation(request));

  request.flags = PJ_DATA_PROCESSOR_FLAG_HISTORY_EXEMPT;
  host.submit_called = false;
  auto handle = view.submitEvaluation(request);
  ASSERT_FALSE(handle);
  EXPECT_NE(handle.error().find("HISTORY_EXEMPT"), std::string::npos) << handle.error();
  EXPECT_FALSE(host.submit_called);
}

// The installed node's stored recipe is authoritative: a submit that also carries
// recipe fields is refused instead of half-honoured.
TEST(DataProcessorsApiTest, InstalledSubmitRejectsRecipeFields) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  ASSERT_TRUE(view.createV2(onDemandRecipe()));

  sdk::DataProcessorRequest request = installedSubmit();
  request.language = "luau";
  auto handle = view.submitEvaluation(request);
  ASSERT_FALSE(handle);
  EXPECT_NE(handle.error().find("recipe"), std::string::npos) << handle.error();
}

TEST(DataProcessorsApiTest, PollReturnsNoReportWhilePendingAndOneParsableShapeOnceTerminal) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  ASSERT_TRUE(view.createV2(onDemandRecipe()));
  auto handle = view.submitEvaluation(installedSubmit());
  ASSERT_TRUE(handle) << handle.error();

  host.poll_state = PJ_EVALUATION_STATE_PENDING;
  auto pending = view.pollEvaluation(*handle);
  ASSERT_TRUE(pending) << pending.error();
  EXPECT_EQ(pending->state, sdk::EvaluationState::kPending);
  EXPECT_TRUE(pending->json.empty());

  host.poll_state = PJ_EVALUATION_STATE_COMPLETED;
  auto completed = view.pollEvaluation(*handle);
  ASSERT_TRUE(completed) << completed.error();
  EXPECT_EQ(completed->state, sdk::EvaluationState::kCompleted);
  auto completed_report = sdk::parseEvaluationReport(completed->json);
  ASSERT_TRUE(completed_report) << completed_report.error();
  EXPECT_EQ(completed_report->stopped, sdk::EvaluationStop::kComplete);
  ASSERT_EQ(completed_report->bundles.size(), 1u);
  EXPECT_EQ(completed_report->bundles[0].stamp_ns, 8);

  host.poll_state = PJ_EVALUATION_STATE_FAILED;
  auto failed = view.pollEvaluation(*handle);
  ASSERT_TRUE(failed) << failed.error();
  EXPECT_EQ(failed->state, sdk::EvaluationState::kFailed);
  auto failed_report = sdk::parseEvaluationReport(failed->json);
  ASSERT_TRUE(failed_report) << failed_report.error();
  EXPECT_EQ(failed_report->stopped, sdk::EvaluationStop::kError);
  EXPECT_EQ(failed_report->error, "script error");
  EXPECT_TRUE(failed_report->bundles.empty());
}

// State 3 is reserved and never emitted: like any state this SDK does not know, it is
// an error, never "pending" (the caller would poll forever).
TEST(DataProcessorsApiTest, PollRejectsAStateThisSdkDoesNotKnow) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  sdk::DataProcessorsHostView view(PJ_data_processors_host_t{.ctx = &host, .vtable = &vtable});
  ASSERT_TRUE(view.createV2(onDemandRecipe()));
  auto handle = view.submitEvaluation(installedSubmit());
  ASSERT_TRUE(handle) << handle.error();

  for (const uint32_t raw : {3u, 99u}) {
    host.poll_state = raw;
    auto poll = view.pollEvaluation(*handle);
    ASSERT_FALSE(poll);
    EXPECT_NE(poll.error().find("unknown evaluation state " + std::to_string(raw)), std::string::npos);
  }
}

// --- The fake host's refusals, through the raw C slots ------------------------------

void expectRejected(bool accepted, const PJ_error_t& error, std::string_view reason) {
  EXPECT_FALSE(accepted);
  EXPECT_EQ(error.code, PJ_ERROR_CODE_REJECTED);
  EXPECT_NE(std::string_view(error.message).find(reason), std::string_view::npos) << error.message;
}

TEST(DataProcessorsFakeHostTest, RefusesWhatTheContractRefusesWithRejected) {
  FakeDataProcessorsHost host;
  const auto vtable = makeVtable();
  const PJ_string_view_t output = sdk::toAbiString("cloud");

  PJ_error_t v1_error{};
  expectRejected(
      vtable.create_data_processor(
          &host, sdk::toAbiString("finding"), sdk::toAbiString("on_demand"), sdk::toAbiString("luau"), nullptr, 0,
          &output, 1, sdk::toAbiString("return {}"), sdk::toAbiString("{}"), 0, nullptr, 0, nullptr, &v1_error),
      v1_error, "create_data_processor_v2");

  RawRequest window;
  window.raw.time_flags = PJ_DATA_PROCESSOR_TIME_FLAG_WINDOW;
  PJ_error_t window_error{};
  expectRejected(
      vtable.create_data_processor_v2(&host, &window.raw, nullptr, 0, nullptr, &window_error), window_error,
      "submit_evaluation");

  RawRequest transform;
  transform.raw.kind = sdk::toAbiString("transform");
  uint64_t handle = 0;
  PJ_error_t kind_error{};
  expectRejected(
      vtable.submit_evaluation(&host, &transform.raw, nullptr, &handle, &kind_error), kind_error, "on_demand");

  RawRequest missing;
  missing.makeInstalledSubmit("missing");
  PJ_error_t id_error{};
  expectRejected(vtable.submit_evaluation(&host, &missing.raw, nullptr, &handle, &id_error), id_error, "missing");
  EXPECT_FALSE(host.submit_called);
}

// --- Host-side decoders (data_processors_host.hpp) --------------------------------

TEST(DataProcessorsHostDecoderTest, ReadsAV1SizedRequestAndThePrefixOfALongerOne) {
  RawRequest request;
  request.raw.struct_size = PJ_DATA_PROCESSOR_REQUEST_V1_MIN_SIZE;
  request.raw.dataset_id = 3;
  auto v1 = sdk::readDataProcessorRequest(&request.raw, sdk::DataProcessorSlot::kCreateV2);
  ASSERT_TRUE(v1) << v1.error();
  EXPECT_EQ(v1->dataset_id, 3u);
  EXPECT_EQ(v1->time_ns, 42);

  // A plugin built against a later SDK sends a larger struct; the host reads its prefix.
  struct LongerRequest {
    PJ_data_processor_request_t known;
    uint64_t appended;
  } longer{request.raw, ~uint64_t{0}};
  longer.known.struct_size = sizeof(LongerRequest);
  auto read = sdk::readDataProcessorRequest(&longer.known, sdk::DataProcessorSlot::kSubmitEvaluation);
  ASSERT_TRUE(read) << read.error();
  EXPECT_EQ(read->struct_size, sizeof(PJ_data_processor_request_t));  // the copy is the known prefix
  EXPECT_EQ(sdk::toStringView(read->id), "finding");
  EXPECT_EQ(read->output_count, 1u);
  EXPECT_EQ(read->time_ns, 42);
}

// Only the bytes struct_size claims exist (on the heap, so ASAN sees a read past them).
TEST(DataProcessorsHostDecoderTest, RejectsATruncatedStructWithoutReadingPastIt) {
  for (const uint32_t size : {uint32_t{sizeof(uint32_t)}, uint32_t{PJ_DATA_PROCESSOR_REQUEST_V1_MIN_SIZE - 1}}) {
    const auto storage = std::make_unique<uint8_t[]>(size);
    std::memcpy(storage.get(), &size, sizeof(size));
    EXPECT_FALSE(
        sdk::readDataProcessorRequest(
            reinterpret_cast<const PJ_data_processor_request_t*>(storage.get()), sdk::DataProcessorSlot::kCreateV2));
  }
  for (const uint32_t size : {uint32_t{sizeof(uint32_t)}, uint32_t{PJ_EVALUATION_BUDGET_V1_MIN_SIZE - 1}}) {
    const auto storage = std::make_unique<uint8_t[]>(size);
    std::memcpy(storage.get(), &size, sizeof(size));
    EXPECT_FALSE(sdk::readEvaluationBudget(reinterpret_cast<const PJ_evaluation_budget_t*>(storage.get())));
  }
}

TEST(DataProcessorsHostDecoderTest, RejectsAMalformedRequest) {
  EXPECT_FALSE(sdk::readDataProcessorRequest(nullptr, sdk::DataProcessorSlot::kCreateV2));

  const auto rejected = [](void (*mutate)(RawRequest&)) {
    RawRequest request;
    mutate(request);
    return !sdk::readDataProcessorRequest(&request.raw, sdk::DataProcessorSlot::kCreateV2).has_value();
  };
  EXPECT_FALSE(rejected([](RawRequest&) {}));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.struct_size = PJ_DATA_PROCESSOR_REQUEST_V1_MIN_SIZE - 1; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.flags = 1u << 2; }));  // unassigned: never reuse
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.time_flags = 1u << 2; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.inputs = nullptr; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.outputs = nullptr; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.output.reserved[1] = 1; }));
  EXPECT_TRUE(rejected([](RawRequest& r) {
    r.raw.time_flags = PJ_DATA_PROCESSOR_TIME_FLAG_WINDOW | PJ_DATA_PROCESSOR_TIME_FLAG_INSTANT;
  }));
  // A NULL view with a nonzero size, in every string the request carries.
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.id.data = nullptr; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.kind.data = nullptr; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.language.data = nullptr; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.script.data = nullptr; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.params_json.data = nullptr; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.input.data = nullptr; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.output.name.data = nullptr; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.output.type.data = nullptr; }));
  // An empty string may be NULL.
  EXPECT_FALSE(rejected([](RawRequest& r) { r.raw.params_json = PJ_string_view_t{nullptr, 0}; }));
}

// A count (or size) whose byte size overflows size_t -- the largest values the uint64 fields
// allow, which overflow a 32-bit size_t just as these overflow a 64-bit one -- is rejected
// before any array or string is read. The arrays hold one element, so a traversal would
// run past them.
TEST(DataProcessorsHostDecoderTest, RejectsAnUnrepresentableCountOrSizeBeforeReadingAnything) {
  constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();
  constexpr uint64_t kMaxObject = std::numeric_limits<std::ptrdiff_t>::max();
  const auto rejected = [](void (*mutate)(RawRequest&)) {
    RawRequest request;
    mutate(request);
    return !sdk::readDataProcessorRequest(&request.raw, sdk::DataProcessorSlot::kCreateV2).has_value();
  };
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.input_count = kMax; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.output_count = kMax; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.input_count = kMaxObject / sizeof(PJ_string_view_t) + 1; }));
  EXPECT_TRUE(
      rejected([](RawRequest& r) { r.raw.output_count = kMaxObject / sizeof(PJ_data_processor_output_t) + 1; }));
  // A string size no object can have, with a non-NULL pointer, in every string.
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.id.size = kMax; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.raw.script.size = kMaxObject + 1; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.input.size = kMax; }));
  EXPECT_TRUE(rejected([](RawRequest& r) { r.output.type.size = kMax; }));

  // A budget carries no count or size to trust: struct_size is clamped to sizeof, and an
  // enormous max_* is a request the host clamps, never a rejection.
  PJ_evaluation_budget_t raw{};
  raw.struct_size = std::numeric_limits<uint32_t>::max();
  raw.max_millis = raw.max_bytes = raw.max_evaluations = raw.max_report_bytes = kMax;
  auto budget = sdk::readEvaluationBudget(&raw);
  ASSERT_TRUE(budget) << budget.error();
  EXPECT_EQ(budget->struct_size, sizeof(PJ_evaluation_budget_t));
  EXPECT_EQ(budget->max_bytes, kMax);
}

TEST(DataProcessorsHostDecoderTest, SubmitRules) {
  const auto accepts = [](const RawRequest& request, sdk::DataProcessorSlot slot) {
    return sdk::readDataProcessorRequest(&request.raw, slot).has_value();
  };
  constexpr auto kCreate = sdk::DataProcessorSlot::kCreateV2;
  constexpr auto kSubmit = sdk::DataProcessorSlot::kSubmitEvaluation;

  RawRequest scripted;  // no EPHEMERAL flag: a scripted submit is ephemeral by definition
  EXPECT_TRUE(accepts(scripted, kSubmit));
  scripted.raw.flags = PJ_DATA_PROCESSOR_FLAG_HISTORY_EXEMPT;
  EXPECT_TRUE(accepts(scripted, kCreate));
  EXPECT_FALSE(accepts(scripted, kSubmit));
  // EPHEMERAL is optional on a submit, but a create at an instant is a persisted finding.
  scripted.raw.flags = PJ_DATA_PROCESSOR_FLAG_EPHEMERAL;
  EXPECT_FALSE(accepts(scripted, kCreate));
  EXPECT_TRUE(accepts(scripted, kSubmit));
  scripted.raw.time_flags = PJ_DATA_PROCESSOR_TIME_FLAG_WINDOW;
  EXPECT_TRUE(accepts(scripted, kCreate));
  scripted.raw.flags = 0;
  scripted.raw.time_flags = 0;
  EXPECT_TRUE(accepts(scripted, kCreate));
  EXPECT_FALSE(accepts(scripted, kSubmit));

  // An installed-node submit carries no recipe field.
  const auto installed_with = [&](void (*mutate)(RawRequest&)) {
    RawRequest request;
    request.makeInstalledSubmit("finding");
    mutate(request);
    return accepts(request, kSubmit);
  };
  EXPECT_TRUE(installed_with([](RawRequest&) {}));
  EXPECT_FALSE(installed_with([](RawRequest& r) { r.raw.language = sdk::toAbiString("luau"); }));
  EXPECT_FALSE(installed_with([](RawRequest& r) { r.raw.params_json = sdk::toAbiString("{}"); }));
  EXPECT_FALSE(installed_with([](RawRequest& r) {
    r.raw.inputs = &r.input;
    r.raw.input_count = 1;
  }));
  EXPECT_FALSE(installed_with([](RawRequest& r) {
    r.raw.outputs = &r.output;
    r.raw.output_count = 1;
  }));
  EXPECT_FALSE(installed_with([](RawRequest& r) { r.raw.dataset_id = 3; }));
}

TEST(DataProcessorsHostDecoderTest, ANullBudgetTakesEveryHostDefault) {
  auto budget = sdk::readEvaluationBudget(nullptr);
  ASSERT_TRUE(budget) << budget.error();
  EXPECT_EQ(budget->struct_size, sizeof(PJ_evaluation_budget_t));
  EXPECT_EQ(budget->max_millis, 0u);
  EXPECT_EQ(budget->max_bytes, 0u);
  EXPECT_EQ(budget->max_evaluations, 0u);
  EXPECT_EQ(budget->max_report_bytes, 0u);
}

TEST(DataProcessorsHostDecoderTest, RejectsANonzeroBudgetReservedAndAnUndersizedBudget) {
  PJ_evaluation_budget_t raw{};
  raw.struct_size = sizeof(raw);
  raw.max_evaluations = 5;
  auto budget = sdk::readEvaluationBudget(&raw);
  ASSERT_TRUE(budget) << budget.error();
  EXPECT_EQ(budget->max_evaluations, 5u);

  raw.reserved = 1;
  auto with_reserved = sdk::readEvaluationBudget(&raw);
  ASSERT_FALSE(with_reserved);
  EXPECT_NE(with_reserved.error().find("reserved"), std::string::npos) << with_reserved.error();

  raw.reserved = 0;
  raw.struct_size = PJ_EVALUATION_BUDGET_V1_MIN_SIZE - 1;
  EXPECT_FALSE(sdk::readEvaluationBudget(&raw));
}

// --- Evaluation report (pj_plugins/sdk/evaluation_report.hpp) -----------------------

// Above 2^53: a value that went through a double would not compare equal.
constexpr int64_t kLidarNs = 1700000000123456789;
constexpr int64_t kImuNs = 1700000000123456001;

TEST(EvaluationReportTest, ParsesACompletedReportAndDerivesTheStampFromTheNewestInput) {
  const std::string json = R"({"coverage":{"candidates":3,"evaluated":1,"stopped":"budget_evaluations",)"
                           R"("gaps":[{"from_ns":5,"to_ns":9}]},"bundles":[{"requested_ns":1700000000123456999,)"
                           R"("inputs":{"lidar":1700000000123456789,"imu":1700000000123456001},"outputs":{)"
                           R"("count":{"status":"ok","value":12},"cloud":{"status":"empty","summary":{"points":0}},)"
                           R"("label":{"status":"unavailable","reason":"no fix"}}}],)"
                           R"("outputs":[{"name":"count","type":"number"}],"future_key":true})";

  auto report = sdk::parseEvaluationReport(json);
  ASSERT_TRUE(report) << report.error();
  EXPECT_EQ(report->candidates, 3u);
  EXPECT_EQ(report->evaluated, 1u);
  EXPECT_EQ(report->stopped, sdk::EvaluationStop::kBudgetEvaluations);
  EXPECT_TRUE(report->error.empty());
  ASSERT_EQ(report->gaps.size(), 1u);
  EXPECT_EQ(report->gaps[0].from_ns, 5);
  EXPECT_EQ(report->gaps[0].to_ns, 9);

  ASSERT_EQ(report->bundles.size(), 1u);
  const auto& bundle = report->bundles[0];
  EXPECT_EQ(bundle.requested_ns, 1700000000123456999);
  EXPECT_EQ(bundle.inputs.at("lidar"), kLidarNs);
  EXPECT_EQ(bundle.inputs.at("imu"), kImuNs);
  EXPECT_EQ(bundle.stamp_ns, kLidarNs);
  EXPECT_EQ(bundle.outputs.at("count").status, sdk::EvaluationOutputStatus::kOk);
  EXPECT_EQ(bundle.outputs.at("count").value, 12);
  EXPECT_EQ(bundle.outputs.at("cloud").status, sdk::EvaluationOutputStatus::kEmpty);
  EXPECT_EQ(bundle.outputs.at("cloud").summary.at("points"), 0);
  EXPECT_EQ(bundle.outputs.at("label").status, sdk::EvaluationOutputStatus::kUnavailable);
  EXPECT_EQ(bundle.outputs.at("label").reason, "no fix");

  ASSERT_EQ(report->inferred_outputs.size(), 1u);
  EXPECT_EQ(report->inferred_outputs[0].name, "count");
  EXPECT_EQ(report->inferred_outputs[0].type, "number");
}

TEST(EvaluationReportTest, ABundleWithoutInputsIsStampedAtTheRequestedTime) {
  const std::string json = R"({"coverage":{"candidates":1,"evaluated":1,"stopped":"complete","gaps":[]},)"
                           R"("bundles":[{"requested_ns":42,"inputs":{},"outputs":{}}]})";

  auto report = sdk::parseEvaluationReport(json);
  ASSERT_TRUE(report) << report.error();
  ASSERT_EQ(report->bundles.size(), 1u);
  EXPECT_EQ(report->bundles[0].stamp_ns, 42);
}

// A one-bundle report with one output "x"; each part replaceable to make it invalid.
std::string reportWith(
    std::string_view coverage = R"("candidates":1,"evaluated":1,"stopped":"complete","gaps":[])",
    std::string_view output = R"("status":"ok","value":1)", std::string_view requested = "1",
    std::string_view root = "") {
  return R"({"coverage":{)" + std::string(coverage) + R"(},"bundles":[{"requested_ns":)" + std::string(requested) +
         R"(,"inputs":{},"outputs":{"x":{)" + std::string(output) + "}}}]" + std::string(root) + "}";
}

// An unrecognised value of a known key, a wrong type, or an inconsistent coverage is
// an error, never success.
TEST(EvaluationReportTest, RejectsUnknownValuesWrongTypesAndInconsistentCoverage) {
  ASSERT_TRUE(sdk::parseEvaluationReport(reportWith()));
  // The instant that failed is not counted: one bundle, evaluated 1, the next instant's error.
  ASSERT_TRUE(
      sdk::parseEvaluationReport(reportWith(
          R"("candidates":2,"evaluated":1,"stopped":"error",)"
          R"("error":"boom","gaps":[])")));
  for (const std::string_view coverage : {
           R"("candidates":1,"evaluated":1,"stopped":"paused","gaps":[])",
           R"("candidates":1,"evaluated":1,"stopped":"error","gaps":[])",
           R"("candidates":1,"evaluated":1,"stopped":"complete","error":"boom","gaps":[])",
           R"("candidates":1,"evaluated":1,"stopped":"error","error":7,"gaps":[])",
           R"("candidates":1,"evaluated":2,"stopped":"complete","gaps":[])",
           R"("candidates":2,"evaluated":0,"stopped":"budget_time","gaps":[])",
           R"("candidates":-1,"evaluated":0,"stopped":"complete","gaps":[])",
           R"("candidates":1,"evaluated":1,"stopped":"complete","gaps":{})",
           R"("candidates":1,"evaluated":1,"stopped":"complete","gaps":[{"from_ns":1}])",
       }) {
    EXPECT_FALSE(sdk::parseEvaluationReport(reportWith(coverage))) << coverage;
  }
  const std::string_view valid_coverage = R"("candidates":1,"evaluated":1,"stopped":"complete","gaps":[])";
  for (const std::string_view output : {
           R"("status":"stale")",
           R"("value":1)",
           R"("status":"ok","value":[1])",
           R"("status":"ok","value":true)",
           R"("status":"empty","summary":3)",
           R"("status":"error","reason":5)",
       }) {
    EXPECT_FALSE(sdk::parseEvaluationReport(reportWith(valid_coverage, output))) << output;
  }
  const std::string_view valid_output = R"("status":"ok","value":1)";
  for (const std::string_view requested : {"1.5", "9223372036854775808", R"("1")"}) {
    EXPECT_FALSE(sdk::parseEvaluationReport(reportWith(valid_coverage, valid_output, requested))) << requested;
  }
  for (const std::string_view root : {R"(,"outputs":{})", R"(,"outputs":[{"name":"x"}])"}) {
    EXPECT_FALSE(sdk::parseEvaluationReport(reportWith(valid_coverage, valid_output, "1", root))) << root;
  }
  EXPECT_FALSE(sdk::parseEvaluationReport("{\"coverage\":"));
  EXPECT_FALSE(sdk::parseEvaluationReport(R"({"bundles":[]})"));
}

// `evaluated` is the bundle count, an input resolves at or before its bundle's instant,
// and an inferred output type is one of the documented values.
TEST(EvaluationReportTest, RejectsABundleCountMismatchAFutureInputAndAnUnknownOutputType) {
  const std::string_view valid_coverage = R"("candidates":1,"evaluated":1,"stopped":"complete","gaps":[])";
  const auto with_bundles = [](std::string_view coverage, std::string_view bundles) {
    return R"({"coverage":{)" + std::string(coverage) + R"(},"bundles":[)" + std::string(bundles) + "]}";
  };
  constexpr std::string_view kBundle = R"({"requested_ns":10,"inputs":{"lidar":8},"outputs":{}})";

  EXPECT_TRUE(sdk::parseEvaluationReport(with_bundles(valid_coverage, kBundle)));
  EXPECT_FALSE(sdk::parseEvaluationReport(with_bundles(valid_coverage, "")));  // evaluated 1, no bundle
  EXPECT_FALSE(
      sdk::parseEvaluationReport(
          with_bundles(R"("candidates":2,"evaluated":2,"stopped":"complete","gaps":[])", kBundle)));
  EXPECT_FALSE(
      sdk::parseEvaluationReport(
          with_bundles(R"("candidates":1,"evaluated":0,"stopped":"complete","gaps":[])", kBundle)));

  // The newest sample at or before requested_ns: equal is fine, later is not.
  EXPECT_TRUE(
      sdk::parseEvaluationReport(
          with_bundles(valid_coverage, R"({"requested_ns":10,"inputs":{"lidar":10},"outputs":{}})")));
  auto future = sdk::parseEvaluationReport(
      with_bundles(valid_coverage, R"({"requested_ns":10,"inputs":{"lidar":11},"outputs":{}})"));
  ASSERT_FALSE(future);
  EXPECT_NE(future.error().find("lidar"), std::string::npos) << future.error();

  for (const std::string_view type : {"number", "string", "unknown", "kPointCloud"}) {
    const std::string root = R"(,"outputs":[{"name":"x","type":")" + std::string(type) + R"("}])";
    EXPECT_TRUE(sdk::parseEvaluationReport(reportWith(valid_coverage, R"("status":"ok","value":1)", "1", root)))
        << type;
  }
  for (const std::string_view type : {"banana", "", "Number", "kNotAType"}) {
    const std::string root = R"(,"outputs":[{"name":"x","type":")" + std::string(type) + R"("}])";
    EXPECT_FALSE(sdk::parseEvaluationReport(reportWith(valid_coverage, R"("status":"ok","value":1)", "1", root)))
        << type;
  }
}

// A raw NUL byte is refused wherever it sits; an escaped \u0000 inside a string is JSON.
TEST(EvaluationReportTest, RejectsARawNulButKeepsAnEscapedOne) {
  const std::string valid = reportWith();
  std::string with_nul = valid;
  with_nul += '\0';
  with_nul += "junk";
  ASSERT_EQ(with_nul.size(), valid.size() + 5);
  EXPECT_FALSE(sdk::parseEvaluationReport(with_nul));

  auto escaped = sdk::parseEvaluationReport(reportWith(
      R"("candidates":2,"evaluated":1,"stopped":"error",)"
      R"("error":"a\u0000b","gaps":[])"));
  ASSERT_TRUE(escaped) << escaped.error();
  EXPECT_EQ(escaped->error, std::string("a\0b", 3));
}

}  // namespace
}  // namespace PJ
