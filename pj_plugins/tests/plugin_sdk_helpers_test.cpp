// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// The header-only plugin-authoring helpers: parser array policy, the
// streaming handoff containers + delegated-ingest cache, endpoint text
// helpers, and the streaming dialog helpers.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "pj_plugins/sdk/endpoint.hpp"
#include "pj_plugins/sdk/parser_array_policy.hpp"
#include "pj_plugins/sdk/streaming_dialog.hpp"
#include "pj_plugins/sdk/streaming_source.hpp"
#include "pj_plugins/sdk/timestamp_policy.hpp"

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

// ---------------------------------------------------------------------------
// Timestamp policy
// ---------------------------------------------------------------------------

TEST(TimestampPolicyTest, NativeTimestampTypePassWinsOverPreferredName) {
  const PJ::sdk::TimestampCandidate candidates[] = {
      {.name = "timestamp_ns", .kind = PJ::sdk::TimeKind::kInt64},
      {.name = "foo", .kind = PJ::sdk::TimeKind::kNativeTimestamp},
  };

  EXPECT_EQ(PJ::sdk::detectTimestampColumn(candidates), std::optional<std::size_t>{1});
}

TEST(TimestampPolicyTest, NamePriorityWinsOverCandidateOrder) {
  const PJ::sdk::TimestampCandidate candidates[] = {
      {.name = "time", .kind = PJ::sdk::TimeKind::kInt64},
      {.name = "recording_timestamp_ns", .kind = PJ::sdk::TimeKind::kInt64},
  };

  EXPECT_EQ(PJ::sdk::detectTimestampColumn(candidates), std::optional<std::size_t>{1});
}

TEST(TimestampPolicyTest, NarrowIntegerNameIsSkippedForPlausibleInt64) {
  const PJ::sdk::TimestampCandidate candidates[] = {
      {.name = "timestamp_ns", .kind = PJ::sdk::TimeKind::kNarrowInt},
      {.name = "time", .kind = PJ::sdk::TimeKind::kInt64},
  };

  EXPECT_EQ(PJ::sdk::detectTimestampColumn(candidates), std::optional<std::size_t>{1});
}

TEST(TimestampPolicyTest, UInt32AndFloat32NamesAreSkippedForFloat64) {
  const PJ::sdk::TimestampCandidate candidates[] = {
      {.name = "timestamp_ns", .kind = PJ::sdk::TimeKind::kUInt32},
      {.name = "recording_timestamp_ns", .kind = PJ::sdk::TimeKind::kFloat32},
      {.name = "time", .kind = PJ::sdk::TimeKind::kFloat64},
  };

  EXPECT_EQ(PJ::sdk::detectTimestampColumn(candidates), std::optional<std::size_t>{2});
}

TEST(TimestampPolicyTest, ListElementIsNeverSelected) {
  const PJ::sdk::TimestampCandidate candidates[] = {
      {.name = "timestamp", .kind = PJ::sdk::TimeKind::kNativeTimestamp, .is_list_element = true},
  };

  EXPECT_FALSE(PJ::sdk::detectTimestampColumn(candidates));
}

TEST(TimestampPolicyTest, CanonicalNamesMatchAsciiCaseInsensitively) {
  const PJ::sdk::TimestampCandidate title_case[] = {
      {.name = "Timestamp", .kind = PJ::sdk::TimeKind::kInt64},
  };
  const PJ::sdk::TimestampCandidate upper_case[] = {
      {.name = "DATETIME", .kind = PJ::sdk::TimeKind::kFloat64},
  };

  EXPECT_EQ(PJ::sdk::detectTimestampColumn(title_case), std::optional<std::size_t>{0});
  EXPECT_EQ(PJ::sdk::detectTimestampColumn(upper_case), std::optional<std::size_t>{0});
}

TEST(TimestampPolicyTest, ExactCaseWinsWithinPreferredNameRegardlessOfOrder) {
  const PJ::sdk::TimestampCandidate candidates[] = {
      {.name = "Timestamp", .kind = PJ::sdk::TimeKind::kInt64},
      {.name = "timestamp", .kind = PJ::sdk::TimeKind::kInt64},
  };

  EXPECT_EQ(PJ::sdk::detectTimestampColumn(candidates), std::optional<std::size_t>{1});
}

TEST(TimestampPolicyTest, CaseSensitiveCustomPolicyRejectsFoldedMatch) {
  const std::string_view names[] = {"timestamp"};
  const PJ::sdk::TimestampPolicy policy{.names = names, .case_insensitive = false};
  const PJ::sdk::TimestampCandidate candidates[] = {
      {.name = "Timestamp", .kind = PJ::sdk::TimeKind::kInt64},
  };

  EXPECT_FALSE(PJ::sdk::detectTimestampColumn(candidates, policy));
}

TEST(TimestampPolicyTest, MatchesTimestampNameReturnsCanonicalPriority) {
  EXPECT_EQ(PJ::sdk::matchesTimestampName("timestamp_ns"), std::optional<std::size_t>{0});
}

TEST(TimestampPolicyTest, MatchesTimestampNameHonorsCaseSensitivity) {
  const PJ::sdk::TimestampPolicy case_sensitive_policy{
      .names = PJ::sdk::kCanonicalTimestampNames,
      .case_insensitive = false,
  };

  EXPECT_EQ(PJ::sdk::matchesTimestampName("Timestamp"), std::optional<std::size_t>{2});
  EXPECT_FALSE(PJ::sdk::matchesTimestampName("Timestamp", case_sensitive_policy));
}

TEST(TimestampPolicyTest, MatchesTimestampNameRejectsUnrelatedName) {
  EXPECT_FALSE(PJ::sdk::matchesTimestampName("speed"));
}

TEST(TimestampPolicyTest, MatchesTimestampNamePrefersExactCaseAcrossPolicyNames) {
  const std::string_view names[] = {"timestamp", "Timestamp"};
  const PJ::sdk::TimestampPolicy policy{.names = names, .case_insensitive = true};

  EXPECT_EQ(PJ::sdk::matchesTimestampName("Timestamp", policy), std::optional<std::size_t>{1});
}

TEST(TimestampPolicyTest, SupportAndWarningsCoverEveryTimeKind) {
  struct SupportCase {
    PJ::sdk::TimeKind kind;
    PJ::sdk::AxisSupport support;
  };
  constexpr std::array<SupportCase, 8> cases = {{
      {PJ::sdk::TimeKind::kNativeTimestamp, PJ::sdk::AxisSupport::kPlausible},
      {PJ::sdk::TimeKind::kInt64, PJ::sdk::AxisSupport::kPlausible},
      {PJ::sdk::TimeKind::kUInt64, PJ::sdk::AxisSupport::kPlausible},
      {PJ::sdk::TimeKind::kFloat64, PJ::sdk::AxisSupport::kPlausible},
      {PJ::sdk::TimeKind::kUInt32, PJ::sdk::AxisSupport::kAcceptedWithWarning},
      {PJ::sdk::TimeKind::kNarrowInt, PJ::sdk::AxisSupport::kAcceptedWithWarning},
      {PJ::sdk::TimeKind::kFloat32, PJ::sdk::AxisSupport::kAcceptedWithWarning},
      {PJ::sdk::TimeKind::kOther, PJ::sdk::AxisSupport::kUnsupported},
  }};

  for (const SupportCase& test_case : cases) {
    EXPECT_EQ(PJ::sdk::axisSupport(test_case.kind), test_case.support);
    EXPECT_EQ(
        PJ::sdk::explicitAxisWarning(test_case.kind).empty(),
        test_case.support != PJ::sdk::AxisSupport::kAcceptedWithWarning);
  }
}

TEST(TimestampPolicyTest, TimestampUnitsReadEverySpellingAndRoundTrip) {
  struct UnitCase {
    const char* spelling;
    PJ::TimeUnit unit;
    int64_t nanoseconds_per_unit;
  };
  const UnitCase cases[] = {
      {"ns", PJ::TimeUnit::kNanoseconds, 1},
      {"us", PJ::TimeUnit::kMicroseconds, 1'000},
      {"ms", PJ::TimeUnit::kMilliseconds, 1'000'000},
      {"s", PJ::TimeUnit::kSeconds, 1'000'000'000},
  };

  for (const UnitCase& test_case : cases) {
    const nlohmann::json input = {{"timestamp_unit", test_case.spelling}};
    EXPECT_EQ(PJ::sdk::timestampUnitFromJson(input), std::optional<PJ::TimeUnit>{test_case.unit});
    EXPECT_EQ(PJ::nanosecondsPer(test_case.unit), test_case.nanoseconds_per_unit);

    nlohmann::json output;
    PJ::sdk::timestampUnitToJson(output, test_case.unit);
    EXPECT_EQ(output.at("timestamp_unit"), test_case.spelling);
    EXPECT_EQ(PJ::sdk::timestampUnitFromJson(output), std::optional<PJ::TimeUnit>{test_case.unit});
  }
}

TEST(TimestampPolicyTest, MissingUnitDefaultsToNanosecondsAndUnknownIsRejected) {
  EXPECT_EQ(
      PJ::sdk::timestampUnitFromJson(nlohmann::json::object()),
      std::optional<PJ::TimeUnit>{PJ::TimeUnit::kNanoseconds});
  EXPECT_FALSE(PJ::sdk::timestampUnitFromJson(nlohmann::json{{"timestamp_unit", "minutes"}}));
  EXPECT_EQ(PJ::sdk::kTimestampColumnKey, "timestamp_column");
  EXPECT_EQ(PJ::sdk::kTimestampUnitKey, "timestamp_unit");
  EXPECT_EQ(PJ::sdk::kSyntheticIntervalKey, "synthetic_interval_ns");
  EXPECT_EQ(PJ::sdk::kFlattenStructsKey, "flatten_structs");
}

}  // namespace
