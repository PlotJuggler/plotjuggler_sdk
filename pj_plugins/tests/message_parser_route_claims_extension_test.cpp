// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <utility>

#include "pj_base/parser_route_claims_protocol.h"
#include "pj_plugins/host/message_parser_handle.hpp"
#include "pj_plugins/sdk/message_parser_plugin_base.hpp"

namespace {

class RouteClaimsParser final : public PJ::MessageParserPluginBase {
 public:
  RouteClaimsParser() {
    PJ::sdk::SchemaHandler both;
    both.object_type = PJ::sdk::BuiltinObjectType::kImage;
    both.parse_scalars = [](PJ::Timestamp, PJ::Span<const uint8_t>) -> PJ::Expected<PJ::sdk::ScalarRecord> {
      return PJ::sdk::ScalarRecord{};
    };
    both.parse_object = [](PJ::Timestamp, PJ::sdk::PayloadView) -> PJ::Expected<PJ::sdk::ObjectRecord> {
      return PJ::unexpected("not invoked by classification");
    };
    registerSchemaHandler("example/Both", std::move(both));

    PJ::sdk::SchemaHandler scalar;
    scalar.object_type = PJ::sdk::BuiltinObjectType::kPointCloud;
    scalar.parse_scalars = [](PJ::Timestamp, PJ::Span<const uint8_t>) -> PJ::Expected<PJ::sdk::ScalarRecord> {
      return PJ::sdk::ScalarRecord{};
    };
    registerSchemaHandler("example/Scalar", std::move(scalar));

    PJ::sdk::SchemaHandler object;
    object.object_type = PJ::sdk::BuiltinObjectType::kDepthImage;
    object.parse_object = [](PJ::Timestamp, PJ::sdk::PayloadView) -> PJ::Expected<PJ::sdk::ObjectRecord> {
      return PJ::unexpected("not invoked by classification");
    };
    registerSchemaHandler("example/Object", std::move(object));
  }
};

class EmptyParser final : public PJ::MessageParserPluginBase {};

template <typename Parser>
const PJ_message_parser_vtable_t* parserVtable() {
  static const auto* vtable = PJ::MessageParserPluginBase::vtableWithCreate(
      []() noexcept -> void* {
        try {
          return new Parser();
        } catch (...) {
          return nullptr;
        }
      },
      R"({"id":"route-claims-test","name":"Route Claims Test","version":"1.0.0","encoding":["test"]})");
  return vtable;
}

const PJ_parser_route_claims_v1_t* claimsExtension(PJ::MessageParserHandle& handle) {
  return static_cast<const PJ_parser_route_claims_v1_t*>(
      handle.getPluginExtension(PJ_PARSER_ROUTE_CLAIMS_EXTENSION_V1));
}

PJ_route_classification_v1_t classify(
    PJ::MessageParserHandle& handle, const PJ_parser_route_claims_v1_t& extension, std::string_view type_name) {
  PJ_route_classification_v1_t result{};
  PJ_error_t error{};
  const PJ_string_view_t name{type_name.data(), type_name.size()};
  EXPECT_TRUE(extension.classify_routes(handle.context(), name, PJ_bytes_view_t{}, &result, &error)) << error.message;
  return result;
}

TEST(MessageParserRouteClaimsExtension, RegisteredHandlersProduceExactPerRouteClaims) {
  PJ::MessageParserHandle handle(parserVtable<RouteClaimsParser>());
  const auto* extension = claimsExtension(handle);
  ASSERT_NE(extension, nullptr);
  ASSERT_GE(extension->struct_size, PJ_PARSER_ROUTE_CLAIMS_V1_MIN_SIZE);

  const auto both = classify(handle, *extension, "example/Both");
  EXPECT_EQ(both.route_flags, PJ_PARSER_ROUTE_FLAG_SCALAR_V1 | PJ_PARSER_ROUTE_FLAG_OBJECT_V1);
  EXPECT_EQ(both.match, PJ_PARSER_ROUTE_MATCH_EXACT_V1);
  EXPECT_EQ(both.status, PJ_PARSER_ROUTE_STATUS_CLAIMED_V1);
  EXPECT_EQ(both.object_type, PJ_BUILTIN_OBJECT_TYPE_IMAGE);

  const auto scalar = classify(handle, *extension, "example/Scalar");
  EXPECT_EQ(scalar.route_flags, PJ_PARSER_ROUTE_FLAG_SCALAR_V1);
  EXPECT_EQ(scalar.match, PJ_PARSER_ROUTE_MATCH_EXACT_V1);
  EXPECT_EQ(scalar.status, PJ_PARSER_ROUTE_STATUS_CLAIMED_V1);
  EXPECT_EQ(scalar.object_type, PJ_BUILTIN_OBJECT_TYPE_NONE);

  const auto object = classify(handle, *extension, "example/Object");
  EXPECT_EQ(object.route_flags, PJ_PARSER_ROUTE_FLAG_OBJECT_V1);
  EXPECT_EQ(object.match, PJ_PARSER_ROUTE_MATCH_EXACT_V1);
  EXPECT_EQ(object.status, PJ_PARSER_ROUTE_STATUS_CLAIMED_V1);
  EXPECT_EQ(object.object_type, PJ_BUILTIN_OBJECT_TYPE_DEPTH_IMAGE);
}

TEST(MessageParserRouteClaimsExtension, UnknownTypeDeclinesWithoutExpressingWildcardCoverage) {
  PJ::MessageParserHandle handle(parserVtable<RouteClaimsParser>());
  const auto* extension = claimsExtension(handle);
  ASSERT_NE(extension, nullptr);

  const auto result = classify(handle, *extension, "example/Unknown");
  EXPECT_EQ(result.route_flags, 0);
  EXPECT_EQ(result.match, PJ_PARSER_ROUTE_MATCH_EXACT_V1);
  EXPECT_EQ(result.status, PJ_PARSER_ROUTE_STATUS_DECLINED_V1);
  EXPECT_EQ(result.object_type, PJ_BUILTIN_OBJECT_TYPE_NONE);
}

TEST(MessageParserRouteClaimsExtension, RebuiltParserWithoutHandlersStillExposesDecliningClassifier) {
  PJ::MessageParserHandle handle(parserVtable<EmptyParser>());
  const auto* extension = claimsExtension(handle);
  ASSERT_NE(extension, nullptr);

  const auto result = classify(handle, *extension, "anything");
  EXPECT_EQ(result.route_flags, 0);
  EXPECT_EQ(result.match, PJ_PARSER_ROUTE_MATCH_EXACT_V1);
  EXPECT_EQ(result.status, PJ_PARSER_ROUTE_STATUS_DECLINED_V1);
}

TEST(MessageParserRouteClaimsExtension, InvalidCallsAreFailuresNotDeclineStatuses) {
  PJ::MessageParserHandle handle(parserVtable<RouteClaimsParser>());
  const auto* extension = claimsExtension(handle);
  ASSERT_NE(extension, nullptr);

  PJ_error_t error{};
  const PJ_string_view_t name{"example/Both", 12};
  EXPECT_FALSE(extension->classify_routes(handle.context(), name, PJ_bytes_view_t{}, nullptr, &error));
  EXPECT_NE(std::string_view(error.message).find("null output"), std::string_view::npos);

  error = {};
  PJ_route_classification_v1_t result{
      .route_flags = 99,
      .match = 99,
      .status = 99,
      .object_type = 99,
  };
  EXPECT_FALSE(extension->classify_routes(handle.context(), name, PJ_bytes_view_t{nullptr, 1}, &result, &error));
  EXPECT_NE(std::string_view(error.message).find("invalid borrowed view"), std::string_view::npos);
  EXPECT_EQ(result.status, 99);
}

}  // namespace
