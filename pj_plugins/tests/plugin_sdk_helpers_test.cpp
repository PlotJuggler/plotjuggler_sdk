// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// The header-only plugin-authoring helpers: parser array policy, the
// streaming handoff containers + delegated-ingest cache, endpoint text
// helpers, and the streaming dialog helpers.

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "pj_plugins/sdk/endpoint.hpp"
#include "pj_plugins/sdk/parser_array_policy.hpp"
#include "pj_plugins/sdk/streaming_dialog.hpp"
#include "pj_plugins/sdk/streaming_source.hpp"

namespace {

using PJ::sdk::ArrayLimit;
using PJ::sdk::arrayLimitFromJson;
using PJ::sdk::arrayLimitToJson;
using PJ::sdk::ArrayPolicy;

// ---------------------------------------------------------------------------
// Parser array policy
// ---------------------------------------------------------------------------

TEST(ArrayLimitTest, DefaultsAreClamp500) {
  ArrayLimit limit;
  EXPECT_EQ(limit.max_size, 500u);
  EXPECT_EQ(limit.policy, ArrayPolicy::kClamp);
  EXPECT_TRUE(limit.clamp());
}

TEST(ArrayLimitTest, EmptyOrNonObjectConfigYieldsDefaults) {
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json::object()).max_size, 500u);
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json::object()).policy, ArrayPolicy::kClamp);
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json::array()).max_size, 500u);
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json()).policy, ArrayPolicy::kClamp);
}

TEST(ArrayLimitTest, CanonicalKeysAreRead) {
  auto cfg = nlohmann::json{{"max_array_size", 32}, {"array_policy", "skip"}};
  auto limit = arrayLimitFromJson(cfg);
  EXPECT_EQ(limit.max_size, 32u);
  EXPECT_EQ(limit.policy, ArrayPolicy::kSkip);

  cfg["array_policy"] = "clamp";
  EXPECT_EQ(arrayLimitFromJson(cfg).policy, ArrayPolicy::kClamp);
}

TEST(ArrayLimitTest, ZeroMeansUnlimited) {
  auto cfg = nlohmann::json{{"max_array_size", 0}};
  EXPECT_EQ(arrayLimitFromJson(cfg).max_size, 0u);
}

TEST(ArrayLimitTest, LegacyDiscardKeyMapsToSkip) {
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json{{"discard_large_arrays", true}}).policy, ArrayPolicy::kSkip);
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json{{"discard_large_arrays", false}}).policy, ArrayPolicy::kClamp);
}

TEST(ArrayLimitTest, LegacyClampKeyMapsToPolicy) {
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json{{"clamp_large_arrays", true}}).policy, ArrayPolicy::kClamp);
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json{{"clamp_large_arrays", false}}).policy, ArrayPolicy::kSkip);
}

TEST(ArrayLimitTest, CanonicalPolicyKeyWinsOverLegacy) {
  auto cfg = nlohmann::json{{"array_policy", "skip"}, {"clamp_large_arrays", true}};
  EXPECT_EQ(arrayLimitFromJson(cfg).policy, ArrayPolicy::kSkip);
}

TEST(ArrayLimitTest, RoundTripThroughJson) {
  ArrayLimit original{.max_size = 128, .policy = ArrayPolicy::kSkip};
  nlohmann::json cfg;
  arrayLimitToJson(cfg, original);

  auto restored = arrayLimitFromJson(cfg);
  EXPECT_EQ(restored.max_size, original.max_size);
  EXPECT_EQ(restored.policy, original.policy);

  EXPECT_FALSE(cfg.value("clamp_large_arrays", true));
  EXPECT_TRUE(cfg.value("discard_large_arrays", false));
}

TEST(ArrayLimitTest, ToJsonMirrorsClampCaseForLegacyReaders) {
  // An old plugin reads only the legacy bools; a Clamp limit must serialize
  // them both consistently (clamp=true, discard=false).
  nlohmann::json cfg;
  arrayLimitToJson(cfg, ArrayLimit{.max_size = 500, .policy = ArrayPolicy::kClamp});
  EXPECT_TRUE(cfg.value("clamp_large_arrays", false));
  EXPECT_FALSE(cfg.value("discard_large_arrays", true));
  EXPECT_EQ(cfg.value("array_policy", std::string{}), "clamp");
}

TEST(ArrayLimitTest, WrongTypeMaxArraySizeFallsBackToDefault) {
  for (const auto& bad :
       {nlohmann::json("500"), nlohmann::json(nullptr), nlohmann::json::array({1, 2}),
        nlohmann::json::object({{"k", 1}}), nlohmann::json(true), nlohmann::json(3.7)}) {
    auto cfg = nlohmann::json::object();
    cfg["max_array_size"] = bad;
    ArrayLimit limit;
    EXPECT_NO_THROW(limit = arrayLimitFromJson(cfg)) << "input: " << bad.dump();
    EXPECT_EQ(limit.max_size, 500u) << "input: " << bad.dump();
  }
}

TEST(ArrayLimitTest, NegativeMaxArraySizeFallsBackToDefault) {
  EXPECT_NO_THROW((void)arrayLimitFromJson(nlohmann::json{{"max_array_size", -1}}));
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json{{"max_array_size", -1}}).max_size, 500u);
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json{{"max_array_size", -500}}).max_size, 500u);
}

TEST(ArrayLimitTest, OutOfRangeMaxArraySizeFallsBackToDefault) {
  // > UINT32_MAX must not be truncated modulo 2^32.
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json{{"max_array_size", 5000000000LL}}).max_size, 500u);
}

TEST(ArrayLimitTest, WrongTypeLegacyBoolsFallBackToDefault) {
  EXPECT_NO_THROW((void)arrayLimitFromJson(nlohmann::json{{"discard_large_arrays", "true"}}));
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json{{"discard_large_arrays", "true"}}).policy, ArrayPolicy::kClamp);
  EXPECT_EQ(arrayLimitFromJson(nlohmann::json{{"clamp_large_arrays", 1}}).policy, ArrayPolicy::kClamp);
}

// ---------------------------------------------------------------------------
// Endpoint helpers
// ---------------------------------------------------------------------------

TEST(EndpointTest, ParsesOnlyCompletePortsInRange) {
  EXPECT_EQ(PJ::sdk::parsePort("1883"), 1883);
  EXPECT_FALSE(PJ::sdk::parsePort(""));
  EXPECT_FALSE(PJ::sdk::parsePort("0"));
  EXPECT_FALSE(PJ::sdk::parsePort("65536"));
  EXPECT_FALSE(PJ::sdk::parsePort("1883x"));
}

TEST(EndpointTest, FormatsIpv4DnsAndIpv6Authorities) {
  EXPECT_EQ(PJ::sdk::composeEndpoint("ws", "localhost", uint16_t{8765}), "ws://localhost:8765");
  EXPECT_EQ(PJ::sdk::composeEndpoint("http://", "127.0.0.1", "8889", "camera"), "http://127.0.0.1:8889/camera");
  EXPECT_EQ(PJ::sdk::composeEndpoint("ws", "::1", uint16_t{8765}), "ws://[::1]:8765");
  EXPECT_EQ(PJ::sdk::composeEndpoint("ws", "[::1]", uint16_t{8765}), "ws://[::1]:8765");
  EXPECT_EQ(PJ::sdk::composeHostPort("::1", uint16_t{1}), "[::1]:1");
}

// ---------------------------------------------------------------------------
// Streaming handoff containers
// ---------------------------------------------------------------------------

TEST(LatestValueSlotTest, CoalescesAndDrains) {
  PJ::sdk::LatestValueSlot<std::set<std::string>> slot;
  EXPECT_FALSE(slot.take());
  slot.set({"/first"});
  slot.set({"/last"});
  const auto value = slot.take();
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, (std::set<std::string>{"/last"}));
  EXPECT_FALSE(slot.take());
}

TEST(DrainQueueTest, PreservesFifoAndEmptiesSharedQueue) {
  PJ::sdk::DrainQueue<std::string> queue;
  queue.push("first");
  queue.push("second");
  auto batch = queue.drain();
  ASSERT_EQ(batch.size(), 2U);
  EXPECT_EQ(batch.front(), "first");
  batch.pop();
  EXPECT_EQ(batch.front(), "second");
  EXPECT_TRUE(queue.drain().empty());
}

TEST(StringSetFromViewsTest, CopiesEveryView) {
  struct View {
    const char* data;
    size_t size;
  };
  const View views[] = {{"/a", 2}, {"/bb", 3}, {"/a", 2}};
  EXPECT_EQ(PJ::sdk::stringSetFromViews(views, 3), (std::set<std::string>{"/a", "/bb"}));
}

// ---------------------------------------------------------------------------
// Streaming dialog helpers
// ---------------------------------------------------------------------------

TEST(StreamingDialogTest, MergesOnlyTheVisibleSelection) {
  const std::vector<std::string> previous{"hidden", "visible-old"};
  const std::vector<std::string> reported{"visible-new"};
  const auto merged = PJ::sdk::mergeVisibleSelection(
      previous, reported, [](const std::string& value) { return value != "hidden"; }, [](const auto&) { return true; });
  EXPECT_EQ(merged, (std::vector<std::string>{"hidden", "visible-new"}));
}

TEST(StreamingDialogTest, EncodingIndexAndLookupFallBack) {
  const std::vector<std::string> available{"json", "protobuf"};
  EXPECT_EQ(PJ::sdk::encodingIndex("protobuf", available), 1);
  EXPECT_EQ(PJ::sdk::encodingIndex("missing", available), 0);
  EXPECT_EQ(PJ::sdk::encodingAt(1, available), "protobuf");
  // volatile: keeps GCC from constant-folding the out-of-range index into a
  // (false-positive) -Warray-bounds diagnosis of the guarded access.
  volatile int out_of_range = 7;
  EXPECT_EQ(PJ::sdk::encodingAt(out_of_range, available), "json");
  EXPECT_EQ(PJ::sdk::encodingAt(0, {}, "cbor"), "cbor");
  EXPECT_EQ(PJ::sdk::lowerAscii("JSON-Ä"), "json-Ä");
}

TEST(StreamingDialogTest, SelectionFilterPassesWhenEmptyOrMatching) {
  const std::vector<std::string> selection{"a/x", "b/y"};
  const auto project = [](const std::string& value) { return value.substr(0, 1); };
  EXPECT_TRUE(PJ::sdk::passesSelectionFilter("a", selection, project));
  EXPECT_FALSE(PJ::sdk::passesSelectionFilter("c", selection, project));
  EXPECT_TRUE(PJ::sdk::passesSelectionFilter("c", std::vector<std::string>{}, project));
}

// ---------------------------------------------------------------------------
// Delegated ingest
// ---------------------------------------------------------------------------

TEST(DelegatedIngestTest, ReadsOnlyStringParserOverrides) {
  EXPECT_EQ(PJ::sdk::parserConfigOverride(R"({"_parser_config":"schema"})"), "schema");
  EXPECT_TRUE(PJ::sdk::parserConfigOverride(R"({"_parser_config":42})").empty());
  EXPECT_TRUE(PJ::sdk::parserConfigOverride("not json").empty());
}

TEST(DelegatedIngestTest, BindingFailureIsANonFatalDisposition) {
  PJ::sdk::DelegatedIngestCache cache;
  const PJ::DataSourceRuntimeHostView unbound_host;
  const PJ::ParserBindingRequest request{
      .topic_name = "topic",
      .parser_encoding = "json",
      .type_name = {},
      .schema = {},
      .parser_config_json = {},
  };

  const auto result = cache.push(unbound_host, "topic", request, PJ::Timestamp{0}, {1, 2, 3});

  ASSERT_TRUE(result);
  EXPECT_EQ(*result, PJ::sdk::DelegatedIngestDisposition::kBindingUnavailable);
}

}  // namespace
