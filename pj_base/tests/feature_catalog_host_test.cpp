// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/feature_catalog_host.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "pj_base/sdk/plugin_data_api.hpp"

namespace {

TEST(FeatureCatalogHost, RoundTripThroughView) {
  PJ::sdk::FeatureCatalogHost host([] { return std::string(R"({"schema":1})"); });
  PJ::sdk::FeatureCatalogView view{host.view()};
  ASSERT_TRUE(view.valid());
  auto json = view.json();
  ASSERT_TRUE(json.has_value());
  EXPECT_EQ(*json, R"({"schema":1})");
}

TEST(FeatureCatalogHost, UnboundViewReportsError) {
  PJ::sdk::FeatureCatalogView view;
  EXPECT_FALSE(view.valid());
  EXPECT_FALSE(view.json().has_value());
}

TEST(FeatureCatalogHost, NullOutParamIsRejected) {
  PJ::sdk::FeatureCatalogHost host([] { return std::string("x"); });
  const PJ_feature_catalog_t raw = host.view();
  PJ_error_t err{};
  EXPECT_FALSE(raw.vtable->get_feature_catalog(raw.ctx, nullptr, &err));
  PJ_string_view_t out{};
  EXPECT_FALSE(
      raw.vtable->get_feature_catalog(nullptr, &out, nullptr));  // null ctx rejected even without an error out-param
}

TEST(FeatureCatalogHost, ThrowingProviderBecomesError) {
  PJ::sdk::FeatureCatalogHost host([]() -> std::string { throw std::runtime_error("boom"); });
  PJ::sdk::FeatureCatalogView view{host.view()};
  auto json = view.json();
  ASSERT_FALSE(json.has_value());
  EXPECT_NE(json.error().find("boom"), std::string::npos);
}

TEST(FeatureCatalogHost, ViewValidUntilNextCall) {
  int calls = 0;
  PJ::sdk::FeatureCatalogHost host([&calls] { return "call-" + std::to_string(++calls); });
  const PJ_feature_catalog_t raw = host.view();
  PJ_string_view_t first{};
  PJ_error_t err{};
  ASSERT_TRUE(raw.vtable->get_feature_catalog(raw.ctx, &first, &err));
  EXPECT_EQ(PJ::sdk::toStringView(first), "call-1");
  PJ_string_view_t second{};
  ASSERT_TRUE(raw.vtable->get_feature_catalog(raw.ctx, &second, &err));
  EXPECT_EQ(PJ::sdk::toStringView(second), "call-2");
}

}  // namespace
