/**
 * @file arrow_holders_test.cpp
 * @brief Unit tests for PJ::sdk::Arrow*Holder RAII wrappers (Phase 1c).
 *
 * These holders are pure stdlib; no nanoarrow needed. We verify the
 * release-callback semantics with a simple instrumented struct.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <utility>

#include "pj_base/sdk/arrow.hpp"

namespace PJ::sdk {
namespace {

// ---------------------------------------------------------------------------
// Instrumented Arrow structs that count release() invocations.
// ---------------------------------------------------------------------------

int& schemaReleaseCount() {
  static int count = 0;
  return count;
}

int& arrayReleaseCount() {
  static int count = 0;
  return count;
}

int& streamReleaseCount() {
  static int count = 0;
  return count;
}

void schema_release(::ArrowSchema* s) {
  ++schemaReleaseCount();
  std::memset(s, 0, sizeof(*s));  // spec: release sets fields to null/0
}

void array_release(::ArrowArray* a) {
  ++arrayReleaseCount();
  std::memset(a, 0, sizeof(*a));
}

void stream_release(::ArrowArrayStream* s) {
  ++streamReleaseCount();
  std::memset(s, 0, sizeof(*s));
}

::ArrowSchema makeLiveSchema() {
  ::ArrowSchema s{};
  s.release = schema_release;
  return s;
}

::ArrowArray makeLiveArray() {
  ::ArrowArray a{};
  a.release = array_release;
  return a;
}

::ArrowArrayStream makeLiveStream() {
  ::ArrowArrayStream s{};
  s.release = stream_release;
  return s;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class ArrowHoldersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    schemaReleaseCount() = 0;
    arrayReleaseCount() = 0;
    streamReleaseCount() = 0;
  }
};

TEST_F(ArrowHoldersTest, EmptyHolderDoesNotRelease) {
  {
    ArrowSchemaHolder s;
    EXPECT_FALSE(s.valid());
    EXPECT_EQ(s.get()->release, nullptr);
  }
  EXPECT_EQ(schemaReleaseCount(), 0);
}

TEST_F(ArrowHoldersTest, DestructorReleasesOwnedSchema) {
  {
    ArrowSchemaHolder s(makeLiveSchema());
    EXPECT_TRUE(s.valid());
    EXPECT_EQ(schemaReleaseCount(), 0);
  }
  EXPECT_EQ(schemaReleaseCount(), 1);
}

TEST_F(ArrowHoldersTest, MoveConstructionTransfersOwnership) {
  ArrowSchemaHolder a(makeLiveSchema());
  {
    ArrowSchemaHolder b = std::move(a);
    EXPECT_TRUE(b.valid());
    EXPECT_FALSE(a.valid());  // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(schemaReleaseCount(), 0);
  }
  // `b` goes out of scope first and releases once.
  EXPECT_EQ(schemaReleaseCount(), 1);
}

TEST_F(ArrowHoldersTest, MoveAssignmentReleasesPrevious) {
  ArrowSchemaHolder a(makeLiveSchema());
  ArrowSchemaHolder b(makeLiveSchema());
  b = std::move(a);
  // Assignment releases the old `b`.
  EXPECT_EQ(schemaReleaseCount(), 1);
  EXPECT_TRUE(b.valid());
}

TEST_F(ArrowHoldersTest, ResetReleases) {
  ArrowSchemaHolder s(makeLiveSchema());
  s.reset();
  EXPECT_EQ(schemaReleaseCount(), 1);
  EXPECT_FALSE(s.valid());
  // Second reset is a no-op.
  s.reset();
  EXPECT_EQ(schemaReleaseCount(), 1);
}

TEST_F(ArrowHoldersTest, OutResetsBeforeOverwrite) {
  ArrowSchemaHolder s(makeLiveSchema());
  // out() drops the previously-held struct before returning the pointer
  // — this is the read-API pattern where the host fills into out().
  auto* p = s.out();
  EXPECT_EQ(schemaReleaseCount(), 1);
  EXPECT_FALSE(s.valid());

  // Simulate a host producer populating the struct.
  *p = makeLiveSchema();
  EXPECT_TRUE(s.valid());
}

TEST_F(ArrowHoldersTest, ReleaseTransfersOwnershipWithoutCalling) {
  ArrowSchemaHolder s(makeLiveSchema());
  ::ArrowSchema raw = s.release();
  EXPECT_EQ(schemaReleaseCount(), 0);  // not released yet
  EXPECT_FALSE(s.valid());
  // Caller is now responsible:
  raw.release(&raw);
  EXPECT_EQ(schemaReleaseCount(), 1);
}

TEST_F(ArrowHoldersTest, ArrayAndStreamHoldersWorkTheSame) {
  {
    ArrowArrayHolder a(makeLiveArray());
    ArrowStreamHolder s(makeLiveStream());
  }
  EXPECT_EQ(arrayReleaseCount(), 1);
  EXPECT_EQ(streamReleaseCount(), 1);
}

TEST_F(ArrowHoldersTest, StreamHolderInertAfterHostTakesOwnership) {
  // Simulates the appendArrowStream success path: plugin hands stream to
  // the host; host calls release internally (setting release = nullptr).
  // When the plugin's ArrowStreamHolder later destructs, it must be inert.
  ArrowStreamHolder s(makeLiveStream());
  // Host "takes ownership":
  s.get()->release(s.get());
  EXPECT_EQ(streamReleaseCount(), 1);
  EXPECT_FALSE(s.valid());
  // Destructor now: no second release.
  {
    auto tmp = std::move(s);  // just to force destructor call in this scope
    (void)tmp;
  }
  EXPECT_EQ(streamReleaseCount(), 1);
}

}  // namespace
}  // namespace PJ::sdk
