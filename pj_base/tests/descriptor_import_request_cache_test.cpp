// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Request-artifact cache, fully hermetic: a private temp root is injected and
// artifact validation is a STUB — the suite proves the locking / partial /
// atomic-publish / lease / cleanup mechanics independently of any artifact
// format.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "pj_base/sdk/descriptor_import/request_cache.hpp"

namespace {

namespace fs = std::filesystem;
using PJ::sdk::descriptor_import::ArtifactValidator;
using PJ::sdk::descriptor_import::CacheSpec;
using PJ::sdk::descriptor_import::CleanupPolicy;
using PJ::sdk::descriptor_import::IdentityScheme;
using PJ::sdk::descriptor_import::ReadLease;
using PJ::sdk::descriptor_import::RequestArtifactCache;

const std::string kPrefix = "test:v1:sha256/128:";
const std::string kSuffix = ".artifact";
const std::string kHexA(32, 'a');
const std::string kHexB(32, 'b');
const std::string kHexC(32, 'c');

std::string identityFor(const std::string& hex) {
  return kPrefix + hex;
}

CacheSpec specFor(const fs::path& root) {
  return CacheSpec{root, kSuffix, IdentityScheme{kPrefix, 32}};
}

ArtifactValidator acceptAll() {
  return [](const fs::path&, const std::string&, std::string*) { return true; };
}

ArtifactValidator rejectAll(const std::string& reason) {
  return [reason](const fs::path&, const std::string&, std::string* error) {
    if (error != nullptr) {
      *error = reason;
    }
    return false;
  };
}

void writeFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

bool anyPartialIn(const fs::path& root) {
  for (const auto& entry : fs::directory_iterator(root)) {
    if (entry.path().filename().string().find(".partial.") != std::string::npos) {
      return true;
    }
  }
  return false;
}

/// A private temp root per test plus the accept-all cache most tests use;
/// tests needing another validator construct their own cache over `root()`.
class RequestCacheTest : public ::testing::Test {
 protected:
  RequestCacheTest() {
    static std::atomic<int> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    root_ = fs::temp_directory_path() / ("pj-request-cache-" + std::string(info->name()) + "-" + std::to_string(stamp) +
                                         "-" + std::to_string(counter++));
    fs::create_directories(root_);
    cache_.emplace(specFor(root_), acceptAll());
  }
  ~RequestCacheTest() override {
    cache_.reset();
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  [[nodiscard]] const fs::path& root() const {
    return root_;
  }
  [[nodiscard]] RequestArtifactCache& cache() {
    return *cache_;
  }

  // Materialize `hex` through the real lock -> partial -> commit path.
  std::optional<RequestArtifactCache::Hit> materialize(const std::string& hex, const std::string& content) {
    auto txn = cache().beginWrite(identityFor(hex));
    EXPECT_TRUE(txn) << txn.error().message;
    if (!txn) {
      return std::nullopt;
    }
    writeFile(txn->partialPath(), content);
    auto hit = txn->commit();
    EXPECT_TRUE(hit) << hit.error().message;
    if (!hit) {
      return std::nullopt;
    }
    return std::move(*hit);
  }

 private:
  fs::path root_;
  std::optional<RequestArtifactCache> cache_;
};

TEST_F(RequestCacheTest, PathForShapeAndIdentityValidation) {
  EXPECT_EQ(cache().pathFor(identityFor(kHexA)), root() / (kHexA + kSuffix));
  // Malformed identities can never name a file.
  EXPECT_TRUE(cache().pathFor("").empty());
  EXPECT_TRUE(cache().pathFor(kPrefix + "short").empty());
  EXPECT_TRUE(cache().pathFor("other:v1:sha256/128:" + kHexA).empty());
  EXPECT_TRUE(cache().pathFor(identityFor(std::string(32, 'A'))).empty());  // uppercase
  EXPECT_TRUE(cache().pathFor(identityFor(std::string(31, 'a') + "g")).empty());
  EXPECT_TRUE(cache().pathFor(identityFor(kHexA) + "x").empty());  // trailing junk
  RequestArtifactCache rootless(CacheSpec{{}, kSuffix, IdentityScheme{kPrefix, 32}}, acceptAll());
  EXPECT_TRUE(rootless.pathFor(identityFor(kHexA)).empty());
}

TEST_F(RequestCacheTest, WriteCommitLookupRoundTrip) {
  auto hit = materialize(kHexA, "artifact-bytes");
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->path, root() / (kHexA + kSuffix));
  EXPECT_TRUE(fs::is_regular_file(hit->path));
  EXPECT_TRUE(hit->lease.held());  // the exclusive lock became the shared lease
  EXPECT_FALSE(anyPartialIn(root()));
  EXPECT_TRUE(fs::is_regular_file(fs::path(hit->path.string() + ".touch")));

  auto found = cache().lookup(identityFor(kHexA));
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->path, hit->path);
  EXPECT_TRUE(found->lease.held());  // leases stack
  std::string miss_reason;
  EXPECT_FALSE(cache().lookup(identityFor(kHexB), &miss_reason).has_value());  // absent = miss
  EXPECT_NE(miss_reason.find("no artifact"), std::string::npos);
}

TEST_F(RequestCacheTest, ValidatorSeesTheDigestOnCommitAndLookup) {
  std::vector<std::string> seen;
  RequestArtifactCache recording(specFor(root()), [&seen](const fs::path&, const std::string& hex, std::string*) {
    seen.push_back(hex);
    return true;
  });
  auto txn = recording.beginWrite(identityFor(kHexA));
  ASSERT_TRUE(txn) << txn.error().message;
  writeFile(txn->partialPath(), "bytes");
  ASSERT_TRUE(txn->commit());
  EXPECT_TRUE(recording.lookup(identityFor(kHexA)).has_value());
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0], kHexA);
  EXPECT_EQ(seen[1], kHexA);
}

TEST_F(RequestCacheTest, CommitRejectionDeletesPartialAndReportsReason) {
  RequestArtifactCache rejecting(specFor(root()), rejectAll("stub says no"));
  auto txn = rejecting.beginWrite(identityFor(kHexA));
  ASSERT_TRUE(txn) << txn.error().message;
  const fs::path partial = txn->partialPath();
  writeFile(partial, "junk");
  auto hit = txn->commit();
  ASSERT_FALSE(hit);
  EXPECT_NE(hit.error().message.find("stub says no"), std::string::npos) << hit.error().message;
  EXPECT_FALSE(hit.error().retryable);
  EXPECT_FALSE(fs::exists(partial));  // partials never survive
  EXPECT_FALSE(fs::exists(rejecting.pathFor(identityFor(kHexA))));
  // The transaction is finished: a second commit is refused, not retried.
  EXPECT_FALSE(txn->commit());
  // A lookup miss carries the validator's reason.
  writeFile(root() / (kHexA + kSuffix), "not-an-artifact");
  std::string miss_reason;
  EXPECT_FALSE(rejecting.lookup(identityFor(kHexA), &miss_reason).has_value());
  EXPECT_NE(miss_reason.find("stub says no"), std::string::npos);
  EXPECT_TRUE(fs::exists(root() / (kHexA + kSuffix)));  // rejection never deletes
}

TEST_F(RequestCacheTest, AbandonedTransactionRemovesPartialAndReleasesLock) {
  fs::path partial;
  {
    auto txn = cache().beginWrite(identityFor(kHexA));
    ASSERT_TRUE(txn) << txn.error().message;
    partial = txn->partialPath();
    writeFile(partial, "half");
    // Lock held while the transaction lives.
    auto second = cache().beginWrite(identityFor(kHexA));
    ASSERT_FALSE(second);
    EXPECT_TRUE(second.error().retryable);
  }
  EXPECT_FALSE(fs::exists(partial));
  EXPECT_TRUE(cache().beginWrite(identityFor(kHexA)));
}

TEST_F(RequestCacheTest, NullValidatorFailsClosed) {
  RequestArtifactCache no_validator(specFor(root()), nullptr);
  // A pre-existing file at the right path must NOT classify as a hit.
  writeFile(root() / (kHexA + kSuffix), "foreign");
  std::string miss_reason;
  EXPECT_FALSE(no_validator.lookup(identityFor(kHexA), &miss_reason).has_value());
  EXPECT_NE(miss_reason.find("no artifact validator"), std::string::npos);
  auto txn = no_validator.beginWrite(identityFor(kHexB));
  ASSERT_TRUE(txn) << txn.error().message;
  writeFile(txn->partialPath(), "bytes");
  auto hit = txn->commit();
  ASSERT_FALSE(hit);
  EXPECT_NE(hit.error().message.find("no artifact validator"), std::string::npos) << hit.error().message;
}

TEST_F(RequestCacheTest, SecondWriteOnSameIdentityIsContendedWhileHeld) {
  auto first = cache().beginWrite(identityFor(kHexA));
  ASSERT_TRUE(first) << first.error().message;
  auto second = cache().beginWrite(identityFor(kHexA));
  ASSERT_FALSE(second);
  EXPECT_TRUE(second.error().retryable);                // held elsewhere = retry-able
  EXPECT_TRUE(cache().beginWrite(identityFor(kHexB)));  // other identity unaffected
  auto malformed = cache().beginWrite("junk");
  ASSERT_FALSE(malformed);
  EXPECT_FALSE(malformed.error().retryable);  // malformed: an error, never contention
  EXPECT_NE(malformed.error().message.find("invalid descriptor identity"), std::string::npos);
}

TEST_F(RequestCacheTest, LeaseBlocksWriteButAllowsLookup) {
  auto hit = materialize(kHexA, "bytes");
  ASSERT_TRUE(hit.has_value() && hit->lease.held());
  auto writer = cache().beginWrite(identityFor(kHexA));
  ASSERT_FALSE(writer);
  EXPECT_TRUE(writer.error().retryable);
  auto again = cache().lookup(identityFor(kHexA));
  ASSERT_TRUE(again.has_value());
  EXPECT_TRUE(again->lease.held());
  hit->lease.release();
  again->lease.release();
  EXPECT_TRUE(cache().beginWrite(identityFor(kHexA)));
}

TEST_F(RequestCacheTest, LookupUnderExclusiveHolderReturnsUnleasedHit) {
  auto hit = materialize(kHexA, "bytes");
  ASSERT_TRUE(hit.has_value());
  hit->lease.release();
  auto writer = cache().beginWrite(identityFor(kHexA));  // exclusive holder live
  ASSERT_TRUE(writer) << writer.error().message;
  auto found = cache().lookup(identityFor(kHexA));
  ASSERT_TRUE(found.has_value());  // still answers from the validated file
  EXPECT_FALSE(found->lease.held());
}

TEST_F(RequestCacheTest, ReadLeaseRejectsMalformedIdentity) {
  auto bad = cache().acquireReadLease("junk");
  ASSERT_FALSE(bad);
  EXPECT_NE(bad.error().message.find("invalid descriptor identity"), std::string::npos);
  auto lease = cache().acquireReadLease(identityFor(kHexA));
  ASSERT_TRUE(lease) << lease.error().message;
  EXPECT_TRUE(lease->held());
  ReadLease moved = std::move(*lease);
  EXPECT_TRUE(moved.held());
  EXPECT_FALSE(lease->held());
}

TEST_F(RequestCacheTest, CleanupRemovesStaleOrphanPartialsOnly) {
  const fs::path stale = root() / (kHexA + kSuffix + ".partial.99999");
  const fs::path fresh = root() / (kHexB + kSuffix + ".partial.99998");
  const fs::path foreign = root() / ("notes" + kSuffix + ".partial.1");
  writeFile(stale, "stale");
  writeFile(fresh, "fresh");
  writeFile(foreign, "foreign");
  fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours(48));
  fs::last_write_time(foreign, fs::file_time_type::clock::now() - std::chrono::hours(48));

  // A partial whose identity lock is HELD stays even when old.
  const fs::path held = root() / (kHexC + kSuffix + ".partial.99997");
  writeFile(held, "held");
  fs::last_write_time(held, fs::file_time_type::clock::now() - std::chrono::hours(48));
  auto lock = cache().beginWrite(identityFor(kHexC));
  ASSERT_TRUE(lock) << lock.error().message;

  const auto result = cache().cleanup(CleanupPolicy{});
  EXPECT_FALSE(fs::exists(stale));   // old + unlocked: collected
  EXPECT_TRUE(fs::exists(fresh));    // young: kept
  EXPECT_TRUE(fs::exists(held));     // locked: kept
  EXPECT_TRUE(fs::exists(foreign));  // not a managed name: never touched
  EXPECT_EQ(result.bytes_reclaimed, 5u);
  EXPECT_TRUE(result.target_met);
  EXPECT_FALSE(result.had_errors);
}

TEST_F(RequestCacheTest, CleanupEvictsOldestTouchedFirstAndStopsAtCap) {
  auto a = materialize(kHexA, std::string(1024, 'a'));
  auto b = materialize(kHexB, std::string(1024, 'b'));
  ASSERT_TRUE(a.has_value() && b.has_value());
  a->lease.release();
  b->lease.release();
  fs::last_write_time(fs::path(a->path.string() + ".touch"), fs::file_time_type::clock::now() - std::chrono::hours(10));
  // A foreign file in the root never counts and is never evicted.
  writeFile(root() / "unrelated.bin", std::string(4096, 'x'));

  CleanupPolicy policy;
  policy.max_total_bytes = 1536;  // both = 2048 > cap; one eviction suffices
  const auto result = cache().cleanup(policy);
  EXPECT_FALSE(fs::exists(a->path));  // oldest-touched evicted
  EXPECT_TRUE(fs::exists(b->path));
  EXPECT_FALSE(fs::exists(fs::path(a->path.string() + ".touch")));
  EXPECT_TRUE(fs::exists(root() / "unrelated.bin"));
  EXPECT_EQ(result.bytes_scanned, 2048u);
  EXPECT_EQ(result.bytes_reclaimed, 1024u);
  EXPECT_TRUE(result.target_met);
  EXPECT_EQ(result.bytes_held_over_target, 0u);
}

TEST_F(RequestCacheTest, CleanupSkipsLeasedVictimAndReportsTheShortfall) {
  auto a = materialize(kHexA, std::string(1024, 'a'));
  ASSERT_TRUE(a.has_value() && a->lease.held());

  CleanupPolicy policy;
  policy.max_total_bytes = 0;  // everything is over budget
  auto result = cache().cleanup(policy);
  EXPECT_TRUE(fs::exists(a->path));  // leased: never evicted under a holder
  EXPECT_FALSE(result.target_met);
  EXPECT_EQ(result.bytes_held_over_target, 1024u);
  EXPECT_FALSE(result.had_errors);  // contention is not an error

  a->lease.release();
  result = cache().cleanup(policy);
  EXPECT_FALSE(fs::exists(a->path));  // lease gone: evictable
  EXPECT_TRUE(result.target_met);
}

TEST_F(RequestCacheTest, CleanupOnMissingRootIsCleanAndOnFileRootIsAnError) {
  RequestArtifactCache missing(specFor(root() / "never-created"), acceptAll());
  const auto clean = missing.cleanup(CleanupPolicy{});
  EXPECT_TRUE(clean.target_met);
  EXPECT_FALSE(clean.had_errors);

  writeFile(root() / "file-root", "x");
  RequestArtifactCache bad(specFor(root() / "file-root"), acceptAll());
  CleanupPolicy policy;
  policy.max_total_bytes = 1;
  const auto result = bad.cleanup(policy);
  EXPECT_TRUE(result.had_errors);
  EXPECT_FALSE(result.target_met);
}

TEST_F(RequestCacheTest, RootlessCacheFailsEveryOperationCleanly) {
  RequestArtifactCache rootless(CacheSpec{{}, kSuffix, IdentityScheme{kPrefix, 32}}, acceptAll());
  auto txn = rootless.beginWrite(identityFor(kHexA));
  ASSERT_FALSE(txn);
  EXPECT_NE(txn.error().message.find("root"), std::string::npos);
  EXPECT_FALSE(rootless.acquireReadLease(identityFor(kHexA)));
  EXPECT_FALSE(rootless.lookup(identityFor(kHexA)).has_value());
  EXPECT_FALSE(rootless.cleanup(CleanupPolicy{}).had_errors);
}

TEST_F(RequestCacheTest, OutOfRangeDigestWidthStillRoundTrips) {
  // The spec's scheme normalizes the width exactly like identity minting, so
  // a provider that misconfigures digest_hex_chars still gets a coherent
  // cache instead of permanent silent misses.
  const IdentityScheme odd{"odd:", 33};  // normalizes to 32
  RequestArtifactCache cache_odd(CacheSpec{root(), kSuffix, odd}, acceptAll());
  const std::string identity = odd.identityFor("payload");
  auto txn = cache_odd.beginWrite(identity);
  ASSERT_TRUE(txn) << txn.error().message;
  writeFile(txn->partialPath(), "bytes");
  ASSERT_TRUE(txn->commit());
  EXPECT_TRUE(cache_odd.lookup(identity).has_value());
}

}  // namespace
