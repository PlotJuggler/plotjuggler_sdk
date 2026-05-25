// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/sdk/object_ingest_policy.hpp"

#include <gtest/gtest.h>

using PJ::sdk::BuiltinObjectType;
using PJ::sdk::ObjectIngestPolicy;
using PJ::sdk::ObjectIngestPolicyResolver;

TEST(ObjectIngestPolicyResolverTest, DefaultPolicyIsLazyObjectsEagerScalars) {
  ObjectIngestPolicyResolver r;
  EXPECT_EQ(
      r.resolve("any_source", "/any/topic", BuiltinObjectType::kImage), ObjectIngestPolicy::kLazyObjectsEagerScalars);
}

TEST(ObjectIngestPolicyResolverTest, SetDefaultIsRespected) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kEager);
  EXPECT_EQ(r.resolve("any_source", "/any/topic", BuiltinObjectType::kImage), ObjectIngestPolicy::kEager);
}

TEST(ObjectIngestPolicyResolverTest, TypeOverrideFiresOnMatch) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForType(BuiltinObjectType::kPointCloud, ObjectIngestPolicy::kPureLazy);

  EXPECT_EQ(r.resolve("src", "/lidar/points", BuiltinObjectType::kPointCloud), ObjectIngestPolicy::kPureLazy);
  // Different type falls through to default.
  EXPECT_EQ(r.resolve("src", "/cam/image", BuiltinObjectType::kImage), ObjectIngestPolicy::kLazyObjectsEagerScalars);
}

TEST(ObjectIngestPolicyResolverTest, SourceOverridesType) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForType(BuiltinObjectType::kPointCloud, ObjectIngestPolicy::kPureLazy);
  r.setForDataSource("mcap_source", ObjectIngestPolicy::kEager);

  // Source matches → kEager beats the kPointCloud type override.
  EXPECT_EQ(r.resolve("mcap_source", "/lidar/points", BuiltinObjectType::kPointCloud), ObjectIngestPolicy::kEager);
  // Different source → type override fires.
  EXPECT_EQ(r.resolve("ros2_stream", "/lidar/points", BuiltinObjectType::kPointCloud), ObjectIngestPolicy::kPureLazy);
}

TEST(ObjectIngestPolicyResolverTest, TopicOverridesEverything) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForType(BuiltinObjectType::kPointCloud, ObjectIngestPolicy::kPureLazy);
  r.setForDataSource("mcap_source", ObjectIngestPolicy::kEager);
  r.setForTopic("/diagnostics/lidar", ObjectIngestPolicy::kPureLazy);

  // Topic match wins over source and type.
  EXPECT_EQ(
      r.resolve("mcap_source", "/diagnostics/lidar", BuiltinObjectType::kPointCloud), ObjectIngestPolicy::kPureLazy);
  // Different topic → source override fires.
  EXPECT_EQ(r.resolve("mcap_source", "/other/lidar", BuiltinObjectType::kPointCloud), ObjectIngestPolicy::kEager);
}

TEST(ObjectIngestPolicyResolverTest, TypicalApplicationSetup) {
  // Mirror the recommended setup: large blobs lazy by default, images keep
  // their metadata as columns. PointCloud is always pure-lazy; specific
  // compressed-image topics can be demoted via per-topic overrides when
  // their scalars aren't worth materializing.
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForType(BuiltinObjectType::kPointCloud, ObjectIngestPolicy::kPureLazy);
  r.setForTopic("/cam/jpeg", ObjectIngestPolicy::kPureLazy);

  EXPECT_EQ(r.resolve("mcap", "/cam/raw", BuiltinObjectType::kImage), ObjectIngestPolicy::kLazyObjectsEagerScalars);
  EXPECT_EQ(r.resolve("mcap", "/cam/jpeg", BuiltinObjectType::kImage), ObjectIngestPolicy::kPureLazy);
  EXPECT_EQ(r.resolve("mcap", "/lidar", BuiltinObjectType::kPointCloud), ObjectIngestPolicy::kPureLazy);
  // Scalar-only topic (no builtin object) takes the default.
  EXPECT_EQ(r.resolve("mcap", "/diagnostics", BuiltinObjectType::kNone), ObjectIngestPolicy::kLazyObjectsEagerScalars);
}

TEST(ObjectIngestPolicyResolverTest, LastWriteWinsForSameKey) {
  ObjectIngestPolicyResolver r;
  r.setForType(BuiltinObjectType::kImage, ObjectIngestPolicy::kEager);
  r.setForType(BuiltinObjectType::kImage, ObjectIngestPolicy::kPureLazy);
  EXPECT_EQ(r.resolve("src", "/topic", BuiltinObjectType::kImage), ObjectIngestPolicy::kPureLazy);

  r.setForTopic("/x", ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForTopic("/x", ObjectIngestPolicy::kEager);
  EXPECT_EQ(r.resolve("src", "/x", BuiltinObjectType::kImage), ObjectIngestPolicy::kEager);
}
