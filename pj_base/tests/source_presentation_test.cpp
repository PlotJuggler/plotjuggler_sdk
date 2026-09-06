// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>
#include <pj_base/sdk/settings_store_host.hpp>
#include <string>

#include "pj_base/sdk/source/presentation.hpp"

namespace {

using PJ::sdk::source::SourcePresentation;

class CountingSettingsBackend : public PJ::sdk::InMemorySettingsBackend {
 public:
  void setString(std::string_view key, std::string_view value) override {
    ++string_writes;
    InMemorySettingsBackend::setString(key, value);
  }

  int string_writes = 0;
};

struct Fixture {
  CountingSettingsBackend backend;
  PJ::sdk::SettingsStoreHost host{backend};

  PJ::sdk::SettingsView view() {
    return PJ::sdk::SettingsView{host.view()};
  }
};

SourcePresentation descriptor() {
  SourcePresentation presentation;
  presentation.origin = "demo.mosaico.dev:6726";
  presentation.fallback_name = "sequence-name";
  presentation.display_name = "Friendly sequence";
  return presentation;
}

std::string readString(PJ::sdk::SettingsBackend& backend, const std::string& key) {
  return backend.getString(key).value_or("");
}

TEST(SourcePresentation, DerivesQtCompatibleBase64UrlGroupWithoutPadding) {
  EXPECT_EQ(
      PJ::sdk::source::sourcePresentationSettingsGroup("mosaico:v1:sha256/128:0123456789abcdef0123456789abcdef"),
      "source_presentation/v1/"
      "bW9zYWljbzp2MTpzaGEyNTYvMTI4OjAxMjM0NTY3ODlhYmNkZWYwMTIzNDU2Nzg5YWJjZGVm");
  EXPECT_EQ(PJ::sdk::source::sourcePresentationSettingsGroup("id:\xc3\xa9"), "source_presentation/v1/aWQ6w6k");
}

TEST(SourcePresentation, WritesDisplayNameAndOrigin) {
  Fixture fx;
  const SourcePresentation presentation = descriptor();
  const std::string identity = "mosaico:v1:sha256/128:0123456789abcdef0123456789abcdef";
  const std::string group = PJ::sdk::source::sourcePresentationSettingsGroup(identity);

  PJ::sdk::source::recordSourcePresentation(fx.view(), identity, presentation);

  EXPECT_EQ(readString(fx.backend, group + "/display_name"), "Friendly sequence");
  EXPECT_EQ(readString(fx.backend, group + "/origin"), "demo.mosaico.dev:6726");
  EXPECT_EQ(fx.backend.string_writes, 2);

  // Repeating an identical query is a read-only fast path.
  PJ::sdk::source::recordSourcePresentation(fx.view(), identity, presentation);
  EXPECT_EQ(fx.backend.string_writes, 2);
}

TEST(SourcePresentation, FallsBackToSequenceAndCapsValuesAtTwoHundredCharacters) {
  Fixture fx;
  SourcePresentation presentation = descriptor();
  presentation.display_name.clear();
  presentation.fallback_name = std::string(205, 's');
  presentation.origin = std::string(205, 'h') + ":6726";
  const std::string identity = "identity";
  const std::string group = PJ::sdk::source::sourcePresentationSettingsGroup(identity);

  PJ::sdk::source::recordSourcePresentation(fx.view(), identity, presentation);

  EXPECT_EQ(readString(fx.backend, group + "/display_name"), std::string(200, 's'));
  EXPECT_EQ(readString(fx.backend, group + "/origin"), std::string(200, 'h'));
}

// Control/format code points in layout-supplied text must never reach the
// UI beside a trust verdict: C0/C1, zero-width and bidi controls are
// stripped, multi-byte code points survive.
TEST(SourcePresentation, StripsControlAndBidiFormatCodePoints) {
  Fixture fx;
  SourcePresentation presentation = descriptor();
  presentation.display_name = std::string("A\x01") + "\xe2\x80\xae" +  // U+202E RLO
                              "B\xc3\xa9" + "\xe2\x80\x8b" +           // U+200B ZWSP
                              "\xd8\x9c" + "\xe2\x81\xa0";             // U+061C ALM, U+2060 WJ
  const std::string group = PJ::sdk::source::sourcePresentationSettingsGroup("identity");

  PJ::sdk::source::recordSourcePresentation(fx.view(), "identity", presentation);

  EXPECT_EQ(readString(fx.backend, group + "/display_name"), "AB\xc3\xa9");
}

TEST(SourcePresentation, EmptyIdentityWritesNothingAndEmptyOriginSkipsTheOriginKey) {
  Fixture fx;
  SourcePresentation presentation = descriptor();

  PJ::sdk::source::recordSourcePresentation(fx.view(), "", presentation);
  EXPECT_EQ(fx.backend.string_writes, 0);

  presentation.origin.clear();
  PJ::sdk::source::recordSourcePresentation(fx.view(), "identity", presentation);

  const std::string group = PJ::sdk::source::sourcePresentationSettingsGroup("identity");
  EXPECT_EQ(readString(fx.backend, group + "/display_name"), "Friendly sequence");
  EXPECT_FALSE(fx.backend.getString(group + "/origin").has_value());
  EXPECT_EQ(fx.backend.string_writes, 1);
}

}  // namespace

TEST(SourcePresentation, Utf16BoundaryPreservesWholeCodePoints) {
  Fixture fixture;
  auto presentation = descriptor();
  const auto group = PJ::sdk::source::sourcePresentationSettingsGroup("boundary");
  presentation.display_name = std::string(199, 'x') + "\xf0\x9f\x98\x80";
  PJ::sdk::source::recordSourcePresentation(fixture.view(), "boundary", presentation);
  EXPECT_EQ(readString(fixture.backend, group + "/display_name"), std::string(199, 'x'));
  presentation.display_name = std::string(198, 'x') + "\xf0\x9f\x98\x80" + "z";
  PJ::sdk::source::recordSourcePresentation(fixture.view(), "boundary", presentation);
  EXPECT_EQ(readString(fixture.backend, group + "/display_name"), std::string(198, 'x') + "\xf0\x9f\x98\x80");
  presentation.display_name = std::string(199, 'x') + "\xc3\xa9z";
  PJ::sdk::source::recordSourcePresentation(fixture.view(), "boundary", presentation);
  EXPECT_EQ(readString(fixture.backend, group + "/display_name"), std::string(199, 'x') + "\xc3\xa9");
}

TEST(SourcePresentation, StripsTheRemainingHostControlRanges) {
  Fixture fixture;
  auto presentation = descriptor();
  presentation.display_name =
      "A\x7f\xc2\x80\xc2\x9f\xe2\x80\x8c\xe2\x80\x8f"
      "\xe2\x80\xa8\xe2\x80\xac\xe2\x81\xa1\xe2\x81\xa2"
      "\xe2\x81\xa3\xe2\x81\xa4\xe2\x81\xa6\xe2\x81\xa9\xef\xbb\xbfZ";
  PJ::sdk::source::recordSourcePresentation(fixture.view(), "controls", presentation);
  const auto group = PJ::sdk::source::sourcePresentationSettingsGroup("controls");
  EXPECT_EQ(readString(fixture.backend, group + "/display_name"), "AZ");
  EXPECT_EQ(PJ::sdk::source::sourcePresentationSettingsGroup("a"), "source_presentation/v1/YQ");
  EXPECT_EQ(PJ::sdk::source::sourcePresentationSettingsGroup("ab"), "source_presentation/v1/YWI");
  EXPECT_EQ(PJ::sdk::source::sourcePresentationSettingsGroup("abc"), "source_presentation/v1/YWJj");
}
