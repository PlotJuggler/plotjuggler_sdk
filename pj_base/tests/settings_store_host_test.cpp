// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/settings_store_host.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "pj_base/sdk/plugin_data_api.hpp"

namespace {

// Build a SettingsView over an in-memory backend (the same end-to-end path a
// host exposes: backend -> SettingsStoreHost -> ABI fat pointer -> SettingsView).
struct Fixture {
  PJ::sdk::InMemorySettingsBackend backend;
  PJ::sdk::SettingsStoreHost host{backend};
  PJ::sdk::SettingsView view{host.view()};
};

TEST(SettingsStoreHost, ScalarRoundTrip) {
  Fixture f;
  EXPECT_TRUE(f.view.valid());

  ASSERT_TRUE(f.view.setValue("name", "demo"));
  ASSERT_TRUE(f.view.setValue("count", std::int64_t{42}));
  ASSERT_TRUE(f.view.setValue("ratio", 3.14159));
  ASSERT_TRUE(f.view.setValue("enabled", true));

  EXPECT_EQ(f.view.value("name").toString(), "demo");
  EXPECT_EQ(f.view.value("count").toInt(), 42);
  EXPECT_DOUBLE_EQ(f.view.value("ratio").toDouble(), 3.14159);
  EXPECT_TRUE(f.view.value("enabled").toBool());
}

TEST(SettingsStoreHost, DefaultsOnMissingKey) {
  Fixture f;
  EXPECT_TRUE(f.view.value("missing").isNull());
  EXPECT_EQ(f.view.value("missing").toString("fallback"), "fallback");
  EXPECT_EQ(f.view.value("missing").toInt(7), 7);
  EXPECT_DOUBLE_EQ(f.view.value("missing").toDouble(1.5), 1.5);
  EXPECT_TRUE(f.view.value("missing").toBool(true));
  ASSERT_TRUE(f.view.setValue("word", "abc"));
  EXPECT_EQ(f.view.value("word").toInt(-1), -1);  // non-numeric → default
}

TEST(SettingsStoreHost, BoolParsing) {
  Fixture f;
  for (const char* truthy : {"true", "1", "on"}) {
    ASSERT_TRUE(f.view.setValue("b", truthy));
    EXPECT_TRUE(f.view.value("b").toBool()) << truthy;
  }
  for (const char* falsy : {"false", "0", "off"}) {
    ASSERT_TRUE(f.view.setValue("b", falsy));
    EXPECT_FALSE(f.view.value("b").toBool(true)) << falsy;
  }
  ASSERT_TRUE(f.view.setValue("b", "maybe"));
  EXPECT_TRUE(f.view.value("b").toBool(true));  // unrecognized → default
  EXPECT_FALSE(f.view.value("b").toBool(false));
}

TEST(SettingsStoreHost, StringList) {
  Fixture f;
  const std::vector<std::string> servers = {"alpha:1", "beta:2", "gamma:3"};
  ASSERT_TRUE(f.view.setValue("servers", servers));
  EXPECT_EQ(f.view.valueStringList("servers"), servers);
  EXPECT_TRUE(f.view.valueStringList("absent").empty());
}

TEST(SettingsStoreHost, ContainsAndRemove) {
  Fixture f;
  EXPECT_FALSE(f.view.contains("k"));
  ASSERT_TRUE(f.view.setValue("k", "v"));
  EXPECT_TRUE(f.view.contains("k"));
  ASSERT_TRUE(f.view.remove("k"));
  EXPECT_FALSE(f.view.contains("k"));
  EXPECT_TRUE(f.view.value("k").isNull());
}

}  // namespace
