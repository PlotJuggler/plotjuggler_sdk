#pragma once

#include <memory>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/codec_pipeline.h"
#include "pj_media_core/media_source.h"

namespace PJ {

/// MediaSource for image topics (JPEG, PNG, depth, segmentation).
///
/// Decodes synchronously in setTimestamp() via a CodecPipeline — JPEG at
/// 1080p takes <10ms, adequate for 30fps interactive scrub. Skips redundant
/// decodes when the same timestamp is requested twice (60Hz render polling).
///
/// Ownership: `store` is NOT owned (must outlive this object). `pipeline`
/// is owned (moved in). See ARCHITECTURE.md §5.1.
class ImagePipelineSource : public MediaSource {
 public:
  /// @param store  ObjectStore to query (not owned, must outlive this object)
  /// @param topic  Topic ID to query via latestAt()
  /// @param pipeline  Codec pipeline for decode (owned, moved in)
  ImagePipelineSource(ObjectStore* store, ObjectTopicId topic, std::unique_ptr<CodecPipeline> pipeline);

  void setTimestamp(int64_t ts_ns) override;
  std::optional<DecodedFrame> takeFrame() override;

 private:
  ObjectStore* store_;
  ObjectTopicId topic_;
  std::unique_ptr<CodecPipeline> pipeline_;

  std::optional<DecodedFrame> pending_frame_;
  int64_t last_ts_ = INT64_MIN;
};

}  // namespace PJ
