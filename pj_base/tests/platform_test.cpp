// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/platform.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace PJ::sdk {
namespace {

// "привет" as UTF-8 bytes: non-ASCII, and not representable in most Windows ANSI code pages.
constexpr const char* kNonAscii = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";

// Set (or, for nullptr, remove) an environment variable whose name and value are UTF-8.
void putEnvUtf8(const std::string& name, const char* value) {
#if defined(_WIN32)
  // The wide API keeps non-ASCII values intact; an empty value removes the variable.
  const std::wstring wide_name = pathFromUtf8(name).native();
  const std::wstring wide_value = pathFromUtf8(value == nullptr ? "" : value).native();
  ::_wputenv_s(wide_name.c_str(), wide_value.c_str());
#else
  if (value != nullptr) {
    ::setenv(name.c_str(), value, 1);
  } else {
    ::unsetenv(name.c_str());
  }
#endif
}

// Thin RAII wrapper so a test can set an env var (value in UTF-8) and restore
// the previous value on scope exit.
class ScopedEnv {
 public:
  ScopedEnv(const char* name, const char* value) : name_(name) {
    if (auto prev = getEnv(name)) {
      had_prev_ = true;
      prev_ = *prev;
    }
    putEnvUtf8(name_, value);
  }

  ~ScopedEnv() {
    putEnvUtf8(name_, had_prev_ ? prev_.c_str() : nullptr);
  }

  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;

 private:
  std::string name_;
  bool had_prev_ = false;
  std::string prev_;
};

TEST(GetEnvTest, ReturnsValueWhenSet) {
  ScopedEnv guard("PJ_BASE_TEST_VAR", "hello");
  auto value = getEnv("PJ_BASE_TEST_VAR");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "hello");
}

TEST(GetEnvTest, ReturnsNonAsciiValueAsUtf8) {
  ScopedEnv guard("PJ_BASE_TEST_NON_ASCII", kNonAscii);
  auto value = getEnv("PJ_BASE_TEST_NON_ASCII");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, kNonAscii);
}

TEST(GetEnvTest, ReturnsNulloptWhenUnset) {
  auto value = getEnv("PJ_BASE_TEST_DEFINITELY_UNSET_XYZZY");
  EXPECT_FALSE(value.has_value());
}

TEST(GetEnvTest, ReturnsNulloptForEmptyValue) {
#if defined(_WIN32)
  _putenv_s("PJ_BASE_TEST_EMPTY", "");
#else
  ::setenv("PJ_BASE_TEST_EMPTY", "", 1);
#endif
  auto value = getEnv("PJ_BASE_TEST_EMPTY");
  EXPECT_FALSE(value.has_value());
#if !defined(_WIN32)
  ::unsetenv("PJ_BASE_TEST_EMPTY");
#endif
}

TEST(PathUtf8Test, RoundTripsNonAscii) {
  const std::string utf8 = std::string("/tmp/") + kNonAscii + "/" + kNonAscii + ".mcap";
  EXPECT_EQ(pathToUtf8(pathFromUtf8(utf8)), utf8);
  EXPECT_EQ(pathToUtf8(pathFromUtf8(utf8).filename()), std::string(kNonAscii) + ".mcap");
}

TEST(PathUtf8Test, DecodesAsUtf8NotTheAnsiCodePage) {
  const std::filesystem::path path = pathFromUtf8(kNonAscii);
#if defined(_WIN32)
  EXPECT_EQ(path.native(), L"\u043f\u0440\u0438\u0432\u0435\u0442");
#else
  EXPECT_EQ(path.native(), kNonAscii);
#endif
}

TEST(PathUtf8Test, AsciiAndEmptyAreUnchanged) {
  EXPECT_EQ(pathFromUtf8("a/b.txt"), std::filesystem::path("a/b.txt"));
  EXPECT_EQ(pathToUtf8(std::filesystem::path("a/b.txt")), "a/b.txt");
  EXPECT_TRUE(pathFromUtf8("").empty());
  EXPECT_TRUE(pathFromUtf8(std::string_view{}).empty());
  EXPECT_EQ(pathToUtf8(std::filesystem::path{}), "");
}

#if defined(_WIN32)
TEST(PathUtf8Test, UnpairedSurrogateBecomesReplacementCharacter) {
  // NTFS allows a name that is not valid UTF-16; encoding it must not throw.
  const std::filesystem::path path(std::wstring{L'a', static_cast<wchar_t>(0xD800)});
  EXPECT_EQ(pathToUtf8(path), "a\xEF\xBF\xBD");
}
#endif

TEST(UserDataDirTest, EndsWithPlotjuggler) {
  auto dir = userDataDir();
  EXPECT_EQ(dir.filename(), "plotjuggler");
}

TEST(UserDataDirTest, IsAbsolute) {
  auto dir = userDataDir();
  EXPECT_TRUE(dir.is_absolute());
}

// The variable userDataDir() reads first on this platform.
#if defined(_WIN32)
constexpr const char* kRootVar = "LOCALAPPDATA";
constexpr const char* kRootPrefix = "C:/tmp/pj_test_";
#elif defined(__APPLE__)
constexpr const char* kRootVar = "HOME";
constexpr const char* kRootPrefix = "/tmp/pj_test_";
#else
constexpr const char* kRootVar = "XDG_DATA_HOME";
constexpr const char* kRootPrefix = "/tmp/pj_test_";
#endif

TEST(UserDataDirTest, KeepsNonAsciiRoot) {
  const std::string root = std::string(kRootPrefix) + kNonAscii;
  ScopedEnv guard(kRootVar, root.c_str());
  const auto dir = userDataDir();
  EXPECT_EQ(dir.filename(), "plotjuggler");
  EXPECT_TRUE(pathToUtf8(dir).starts_with(root)) << pathToUtf8(dir);
}

#if defined(_WIN32)
TEST(UserDataDirTest, PrefersLocalAppDataOnWindows) {
  ScopedEnv guard("LOCALAPPDATA", "C:/tmp/pj_test_localappdata");
  auto dir = userDataDir();
  EXPECT_EQ(dir, std::filesystem::path("C:/tmp/pj_test_localappdata") / "plotjuggler");
}
#elif defined(__APPLE__)
TEST(UserDataDirTest, UsesApplicationSupportOnMac) {
  ScopedEnv guard("HOME", "/tmp/pj_test_home");
  auto dir = userDataDir();
  EXPECT_EQ(dir, std::filesystem::path("/tmp/pj_test_home") / "Library" / "Application Support" / "plotjuggler");
}
#else
TEST(UserDataDirTest, PrefersXdgDataHomeOnLinux) {
  ScopedEnv guard("XDG_DATA_HOME", "/tmp/pj_test_xdg");
  auto dir = userDataDir();
  EXPECT_EQ(dir, std::filesystem::path("/tmp/pj_test_xdg") / "plotjuggler");
}

TEST(UserDataDirTest, FallsBackToHomeLocalShareOnLinux) {
  // Unset XDG_DATA_HOME so the helper falls through to HOME/.local/share.
  ::unsetenv("XDG_DATA_HOME");
  ScopedEnv guard("HOME", "/tmp/pj_test_home");
  auto dir = userDataDir();
  EXPECT_EQ(dir, std::filesystem::path("/tmp/pj_test_home") / ".local" / "share" / "plotjuggler");
}
#endif

}  // namespace
}  // namespace PJ::sdk
