// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/settings_store_host.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
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

// Happy-path read helper: asserts the host did not fault, returns the value.
PJ::sdk::SettingsValue read(const PJ::sdk::SettingsView& view, std::string_view key) {
  auto result = view.value(key);
  EXPECT_TRUE(result.has_value()) << "value(" << key << ") unexpectedly faulted";
  return result.has_value() ? *result : PJ::sdk::SettingsValue{};
}

TEST(SettingsStoreHost, ScalarRoundTrip) {
  Fixture f;
  EXPECT_TRUE(f.view.valid());

  ASSERT_TRUE(f.view.setValue("name", "demo"));
  ASSERT_TRUE(f.view.setValue("count", std::int64_t{42}));
  ASSERT_TRUE(f.view.setValue("ratio", 3.14159));
  ASSERT_TRUE(f.view.setValue("enabled", true));

  EXPECT_EQ(read(f.view, "name").toString(), "demo");
  EXPECT_EQ(read(f.view, "count").toInt(), 42);
  EXPECT_DOUBLE_EQ(read(f.view, "ratio").toDouble(), 3.14159);
  EXPECT_TRUE(read(f.view, "enabled").toBool());
}

TEST(SettingsStoreHost, DefaultsOnMissingKey) {
  Fixture f;
  EXPECT_TRUE(read(f.view, "missing").isNull());
  EXPECT_EQ(read(f.view, "missing").toString("fallback"), "fallback");
  EXPECT_EQ(read(f.view, "missing").toInt(7), 7);
  EXPECT_DOUBLE_EQ(read(f.view, "missing").toDouble(1.5), 1.5);
  EXPECT_TRUE(read(f.view, "missing").toBool(true));
  ASSERT_TRUE(f.view.setValue("word", "abc"));
  EXPECT_EQ(read(f.view, "word").toInt(-1), -1);  // non-numeric → default
}

TEST(SettingsStoreHost, BoolParsing) {
  Fixture f;
  for (const char* truthy : {"true", "1", "on"}) {
    ASSERT_TRUE(f.view.setValue("b", truthy));
    EXPECT_TRUE(read(f.view, "b").toBool()) << truthy;
  }
  for (const char* falsy : {"false", "0", "off"}) {
    ASSERT_TRUE(f.view.setValue("b", falsy));
    EXPECT_FALSE(read(f.view, "b").toBool(true)) << falsy;
  }
  ASSERT_TRUE(f.view.setValue("b", "maybe"));
  EXPECT_TRUE(read(f.view, "b").toBool(true));  // unrecognized → default
  EXPECT_FALSE(read(f.view, "b").toBool(false));
}

TEST(SettingsStoreHost, StringList) {
  Fixture f;
  const std::vector<std::string> servers = {"alpha:1", "beta:2", "gamma:3"};
  ASSERT_TRUE(f.view.setValue("servers", servers));

  auto round_trip = f.view.valueStringList("servers");
  ASSERT_TRUE(round_trip.has_value());
  EXPECT_EQ(*round_trip, servers);

  auto absent = f.view.valueStringList("absent");
  ASSERT_TRUE(absent.has_value());
  EXPECT_TRUE(absent->empty());
}

TEST(SettingsStoreHost, EmptyStringListIsStoredAndDistinctFromAbsent) {
  Fixture f;
  ASSERT_TRUE(f.view.setValue("empty", std::vector<std::string>{}));

  auto stored = f.view.valueStringList("empty");
  ASSERT_TRUE(stored.has_value());
  EXPECT_TRUE(stored->empty());

  // A stored-empty list and an absent key both read as an empty vector, but
  // contains() distinguishes them.
  auto has_empty = f.view.contains("empty");
  ASSERT_TRUE(has_empty.has_value());
  EXPECT_TRUE(*has_empty);
  auto has_absent = f.view.contains("absent");
  ASSERT_TRUE(has_absent.has_value());
  EXPECT_FALSE(*has_absent);
}

TEST(SettingsStoreHost, OverwriteReplacesValue) {
  Fixture f;
  ASSERT_TRUE(f.view.setValue("k", "first"));
  EXPECT_EQ(read(f.view, "k").toString(), "first");
  ASSERT_TRUE(f.view.setValue("k", "second"));
  EXPECT_EQ(read(f.view, "k").toString(), "second");
}

TEST(SettingsStoreHost, ContainsAndRemove) {
  Fixture f;
  auto absent = f.view.contains("k");
  ASSERT_TRUE(absent.has_value());
  EXPECT_FALSE(*absent);

  ASSERT_TRUE(f.view.setValue("k", "v"));
  auto present = f.view.contains("k");
  ASSERT_TRUE(present.has_value());
  EXPECT_TRUE(*present);

  ASSERT_TRUE(f.view.remove("k"));
  auto removed = f.view.contains("k");
  ASSERT_TRUE(removed.has_value());
  EXPECT_FALSE(*removed);
  EXPECT_TRUE(read(f.view, "k").isNull());
}

// Two consecutive reads must return independent owned copies: the host reuses a
// single scratch buffer per call ("valid until the next call"), and SettingsView
// is responsible for copying out immediately so a later read cannot corrupt an
// earlier result.
TEST(SettingsStoreHost, ConsecutiveScalarReadsAreIndependent) {
  Fixture f;
  ASSERT_TRUE(f.view.setValue("a", "first"));
  ASSERT_TRUE(f.view.setValue("b", "second"));

  auto a = f.view.value("a");
  ASSERT_TRUE(a.has_value());
  const std::string a_text = a->toString();

  auto b = f.view.value("b");  // reuses the host scratch buffer
  ASSERT_TRUE(b.has_value());

  EXPECT_EQ(a_text, "first");  // unaffected by the second read
  EXPECT_EQ(b->toString(), "second");
}

TEST(SettingsStoreHost, ConsecutiveListReadsAreIndependent) {
  Fixture f;
  ASSERT_TRUE(f.view.setValue("a", std::vector<std::string>{"a1", "a2"}));
  ASSERT_TRUE(f.view.setValue("b", std::vector<std::string>{"b1"}));

  auto a = f.view.valueStringList("a");
  ASSERT_TRUE(a.has_value());
  const std::vector<std::string> a_copy = *a;

  auto b = f.view.valueStringList("b");  // reuses scratch_list_/scratch_list_views_
  ASSERT_TRUE(b.has_value());

  EXPECT_EQ(a_copy, (std::vector<std::string>{"a1", "a2"}));
  EXPECT_EQ(*b, (std::vector<std::string>{"b1"}));
}

// A backend that throws on every call: the host trampolines must translate the
// C++ exception into a PJ_error_t (never let it cross the C ABI), and the read
// methods must surface it as a failed Expected rather than masking it as an
// absent key. This is the core safety contract of the ABI boundary.
class ThrowingBackend : public PJ::sdk::SettingsBackend {
 public:
  std::optional<std::string> getString(std::string_view) override {
    throw std::runtime_error("boom-get");
  }
  void setString(std::string_view, std::string_view) override {
    throw std::runtime_error("boom-set");
  }
  std::optional<std::vector<std::string>> getStringList(std::string_view) override {
    throw std::runtime_error("boom-getlist");
  }
  void setStringList(std::string_view, const std::vector<std::string>&) override {
    throw std::runtime_error("boom-setlist");
  }
  bool contains(std::string_view) override {
    throw std::runtime_error("boom-contains");
  }
  void remove(std::string_view) override {
    throw std::runtime_error("boom-remove");
  }
};

TEST(SettingsStoreHost, BackendExceptionSurfacesAsError) {
  ThrowingBackend backend;
  PJ::sdk::SettingsStoreHost host{backend};
  PJ::sdk::SettingsView view{host.view()};

  auto value = view.value("k");
  ASSERT_FALSE(value.has_value());
  EXPECT_NE(value.error().find("boom-get"), std::string::npos) << value.error();

  auto list = view.valueStringList("k");
  ASSERT_FALSE(list.has_value());
  EXPECT_NE(list.error().find("boom-getlist"), std::string::npos) << list.error();

  auto present = view.contains("k");
  ASSERT_FALSE(present.has_value());
  EXPECT_NE(present.error().find("boom-contains"), std::string::npos) << present.error();

  auto set_scalar = view.setValue("k", "v");
  EXPECT_FALSE(set_scalar.has_value());
  EXPECT_NE(set_scalar.error().find("boom-set"), std::string::npos) << set_scalar.error();

  auto set_list = view.setValue("k", std::vector<std::string>{"x"});
  EXPECT_FALSE(set_list.has_value());
  EXPECT_NE(set_list.error().find("boom-setlist"), std::string::npos) << set_list.error();

  auto removed = view.remove("k");
  EXPECT_FALSE(removed.has_value());
  EXPECT_NE(removed.error().find("boom-remove"), std::string::npos) << removed.error();
}

// A default-constructed SettingsView models "host did not bind the optional
// settings service". Reads degrade to successful defaults (so a plugin can read
// unconditionally), while writes report an error (there is nowhere to persist).
TEST(SettingsStoreHost, UnboundViewDegradesGracefully) {
  PJ::sdk::SettingsView view;
  EXPECT_FALSE(view.valid());

  auto value = view.value("k");
  ASSERT_TRUE(value.has_value());
  EXPECT_TRUE(value->isNull());

  auto list = view.valueStringList("k");
  ASSERT_TRUE(list.has_value());
  EXPECT_TRUE(list->empty());

  auto present = view.contains("k");
  ASSERT_TRUE(present.has_value());
  EXPECT_FALSE(*present);

  EXPECT_FALSE(view.setValue("k", "v").has_value());
  EXPECT_FALSE(view.setValue("k", std::vector<std::string>{"x"}).has_value());
  EXPECT_FALSE(view.remove("k").has_value());
}

}  // namespace
