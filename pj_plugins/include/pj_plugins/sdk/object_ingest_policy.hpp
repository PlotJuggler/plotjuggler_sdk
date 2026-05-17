/**
 * @file object_ingest_policy.hpp
 * @brief Configurable policy that the host applies when a DataSource hands
 *        it a deferred FetchMessageData callable via
 *        DataSourceRuntimeHostView::pushMessage.
 *
 * The DataSource is policy-agnostic: it only fabricates a callable that
 * produces the raw payload bytes when invoked. The host decides — based on
 * the policy resolved for (source_id, topic, type) — whether to invoke the
 * callable immediately (parse and store now), invoke it once for scalars
 * and again on each pull, or never invoke it during ingest and only on
 * consumer pulls.
 */
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "pj_base/builtin/BuiltinObject.h"

namespace PJ {
namespace sdk {

enum class ObjectIngestPolicy : uint8_t {
  /// Host never invokes the FetchMessageData callable during ingest. The
  /// (timestamp, callable) pair is registered in the ObjectStore and the
  /// callable fires only when a consumer pulls. No scalar timeseries are
  /// produced for this topic — its scalar fields (header.stamp, width,
  /// height, …) do not appear in the Datastore. The topic shows up as an
  /// ObjectTopic without children in the unified curve tree. Best for very
  /// large blobs (point clouds, 4K video) when scalar timeseries are not
  /// interesting.
  kPureLazy,

  /// Host invokes the FetchMessageData callable once during ingest to
  /// obtain bytes; parser's parseScalars runs and writes scalar fields to
  /// the Datastore; bytes are then dropped from RAM. The ObjectStore
  /// retains only the callable for re-invocation on pull (which means the
  /// file/source is read again). Best for the common case: scalar
  /// timeseries appear in the tree, the blob does not stay in RAM, and
  /// pulls re-read on demand.
  kLazyObjectsEagerScalars,

  /// Host invokes the FetchMessageData callable once during ingest, parser's parseScalars and
  /// parseObject both run, the canonical object is serialized into the
  /// ObjectStore via pushOwned. Pull is trivial — bytes are already there.
  /// Highest memory cost; the only viable mode for streaming sources that
  /// have no persistent reader to re-read from. Streaming-only fallback.
  kEager,
};

/// Resolver with hierarchical overrides:
///
///   topic > data_source > type > default
///
/// The application sets the levels it cares about during setup; the host
/// queries resolve(source_id, topic, type) for each message. The resolver
/// is intentionally an opaque carrier — its policy decisions are the
/// host's concern, not the DataSource plugin's.
///
/// Typical setup:
///
///   resolver.setDefault(kLazyObjectsEagerScalars);
///   resolver.setForType(BuiltinObjectType::kPointCloud, kPureLazy);
///   // kImage stays at kLazyObjectsEagerScalars: width/height/encoding columns are useful
///
class ObjectIngestPolicyResolver {
 public:
  /// Default policy applied when no more specific override matches.
  void setDefault(ObjectIngestPolicy policy) {
    default_ = policy;
  }

  /// Override the default for a specific canonical object type. Useful when
  /// (e.g.) all PointCloud2 topics should be lazy regardless of source.
  void setForType(BuiltinObjectType type, ObjectIngestPolicy policy) {
    by_type_[type] = policy;
  }

  /// Override the default for all topics of a specific DataSource, keyed by
  /// the plugin manifest "id".
  void setForDataSource(std::string_view source_id, ObjectIngestPolicy policy) {
    by_source_[std::string(source_id)] = policy;
  }

  /// Override the default for a specific topic name. Highest precedence.
  void setForTopic(std::string_view topic_name, ObjectIngestPolicy policy) {
    by_topic_[std::string(topic_name)] = policy;
  }

  /// Resolve the policy for a given (source_id, topic_name, object_type).
  /// Precedence: topic > source > type > default. The first match wins —
  /// no merging or composition between levels.
  [[nodiscard]] ObjectIngestPolicy resolve(
      std::string_view source_id, std::string_view topic_name, BuiltinObjectType object_type) const {
    if (auto it = by_topic_.find(std::string(topic_name)); it != by_topic_.end()) {
      return it->second;
    }
    if (auto it = by_source_.find(std::string(source_id)); it != by_source_.end()) {
      return it->second;
    }
    if (auto it = by_type_.find(object_type); it != by_type_.end()) {
      return it->second;
    }
    return default_;
  }

 private:
  ObjectIngestPolicy default_ = ObjectIngestPolicy::kLazyObjectsEagerScalars;
  std::unordered_map<BuiltinObjectType, ObjectIngestPolicy> by_type_;
  std::unordered_map<std::string, ObjectIngestPolicy> by_source_;
  std::unordered_map<std::string, ObjectIngestPolicy> by_topic_;
};

}  // namespace sdk
}  // namespace PJ
