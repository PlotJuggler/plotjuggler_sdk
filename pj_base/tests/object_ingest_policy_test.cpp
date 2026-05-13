#include "pj_base/sdk/object_ingest_policy.hpp"

#include <gtest/gtest.h>

using PJ::sdk::CanonicalObjectKind;
using PJ::sdk::ObjectIngestPolicy;
using PJ::sdk::ObjectIngestPolicyResolver;

TEST(ObjectIngestPolicyResolverTest, DefaultPolicyIsLazyObjectsEagerScalars) {
  ObjectIngestPolicyResolver r;
  EXPECT_EQ(
      r.resolve("any_source", "/any/topic", CanonicalObjectKind::kImage), ObjectIngestPolicy::kLazyObjectsEagerScalars);
}

TEST(ObjectIngestPolicyResolverTest, SetDefaultIsRespected) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kEager);
  EXPECT_EQ(r.resolve("any_source", "/any/topic", CanonicalObjectKind::kImage), ObjectIngestPolicy::kEager);
}

TEST(ObjectIngestPolicyResolverTest, KindOverrideFiresOnMatch) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForKind(CanonicalObjectKind::kPointCloud, ObjectIngestPolicy::kPureLazy);

  EXPECT_EQ(r.resolve("src", "/lidar/points", CanonicalObjectKind::kPointCloud), ObjectIngestPolicy::kPureLazy);
  // Different kind falls through to default.
  EXPECT_EQ(r.resolve("src", "/cam/image", CanonicalObjectKind::kImage), ObjectIngestPolicy::kLazyObjectsEagerScalars);
}

TEST(ObjectIngestPolicyResolverTest, SourceOverridesKind) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForKind(CanonicalObjectKind::kPointCloud, ObjectIngestPolicy::kPureLazy);
  r.setForDataSource("mcap_source", ObjectIngestPolicy::kEager);

  // Source matches → kEager beats the kPointCloud kind override.
  EXPECT_EQ(r.resolve("mcap_source", "/lidar/points", CanonicalObjectKind::kPointCloud), ObjectIngestPolicy::kEager);
  // Different source → kind override fires.
  EXPECT_EQ(r.resolve("ros2_stream", "/lidar/points", CanonicalObjectKind::kPointCloud), ObjectIngestPolicy::kPureLazy);
}

TEST(ObjectIngestPolicyResolverTest, TopicOverridesEverything) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForKind(CanonicalObjectKind::kPointCloud, ObjectIngestPolicy::kPureLazy);
  r.setForDataSource("mcap_source", ObjectIngestPolicy::kEager);
  r.setForTopic("/diagnostics/lidar", ObjectIngestPolicy::kPureLazy);

  // Topic match wins over source and kind.
  EXPECT_EQ(
      r.resolve("mcap_source", "/diagnostics/lidar", CanonicalObjectKind::kPointCloud), ObjectIngestPolicy::kPureLazy);
  // Different topic → source override fires.
  EXPECT_EQ(r.resolve("mcap_source", "/other/lidar", CanonicalObjectKind::kPointCloud), ObjectIngestPolicy::kEager);
}

TEST(ObjectIngestPolicyResolverTest, TypicalApplicationSetup) {
  // Mirror the recommended setup: large blobs lazy by default, raw images keep
  // their metadata as columns.
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForKind(CanonicalObjectKind::kCompressedImage, ObjectIngestPolicy::kPureLazy);
  r.setForKind(CanonicalObjectKind::kPointCloud, ObjectIngestPolicy::kPureLazy);

  EXPECT_EQ(r.resolve("mcap", "/cam/raw", CanonicalObjectKind::kImage), ObjectIngestPolicy::kLazyObjectsEagerScalars);
  EXPECT_EQ(r.resolve("mcap", "/cam/jpeg", CanonicalObjectKind::kCompressedImage), ObjectIngestPolicy::kPureLazy);
  EXPECT_EQ(r.resolve("mcap", "/lidar", CanonicalObjectKind::kPointCloud), ObjectIngestPolicy::kPureLazy);
  // Scalar-only topic (no canonical) takes the default.
  EXPECT_EQ(
      r.resolve("mcap", "/diagnostics", CanonicalObjectKind::kNone), ObjectIngestPolicy::kLazyObjectsEagerScalars);
}

TEST(ObjectIngestPolicyResolverTest, LastWriteWinsForSameKey) {
  ObjectIngestPolicyResolver r;
  r.setForKind(CanonicalObjectKind::kImage, ObjectIngestPolicy::kEager);
  r.setForKind(CanonicalObjectKind::kImage, ObjectIngestPolicy::kPureLazy);
  EXPECT_EQ(r.resolve("src", "/topic", CanonicalObjectKind::kImage), ObjectIngestPolicy::kPureLazy);

  r.setForTopic("/x", ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForTopic("/x", ObjectIngestPolicy::kEager);
  EXPECT_EQ(r.resolve("src", "/x", CanonicalObjectKind::kImage), ObjectIngestPolicy::kEager);
}
