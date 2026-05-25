// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/platform.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace PJ::sdk {
namespace {

// Thin RAII wrapper so a test can set/unset an env var and restore the
// previous value on scope exit. Uses setenv/unsetenv on POSIX and
// _putenv_s on Windows.
class ScopedEnv {
 public:
  ScopedEnv(const char* name, const char* value) : name_(name) {
    if (auto prev = getEnv(name)) {
      had_prev_ = true;
      prev_ = *prev;
    }
#if defined(_WIN32)
    _putenv_s(name_.c_str(), value);
#else
    ::setenv(name_.c_str(), value, 1);
#endif
  }

  ~ScopedEnv() {
#if defined(_WIN32)
    _putenv_s(name_.c_str(), had_prev_ ? prev_.c_str() : "");
#else
    if (had_prev_) {
      ::setenv(name_.c_str(), prev_.c_str(), 1);
    } else {
      ::unsetenv(name_.c_str());
    }
#endif
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

TEST(UserDataDirTest, EndsWithPlotjuggler) {
  auto dir = userDataDir();
  EXPECT_EQ(dir.filename(), "plotjuggler");
}

TEST(UserDataDirTest, IsAbsolute) {
  auto dir = userDataDir();
  EXPECT_TRUE(dir.is_absolute());
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
