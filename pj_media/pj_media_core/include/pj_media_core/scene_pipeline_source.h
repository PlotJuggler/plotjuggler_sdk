#pragma once

#include <climits>
#include <memory>
#include <optional>

#include "pj_datastore/object_store.hpp"
#include "pj_media_core/media_source.h"
#include "pj_media_core/scene_decoder.h"
#include "pj_media_core/scene_frame.h"

namespace PJ {

/// MediaSource for vector-overlay topics (annotations, markers, etc.).
///
/// Symmetric to ImagePipelineSource but produces SceneFrames in MediaFrame.overlays
/// rather than DecodedFrames in MediaFrame.base. On setTimestamp() it queries the
/// store via latestAt() and decodes the bytes synchronously — overlay messages
/// are small (KBs) so on-the-fly decode is cheap.
///
/// Ownership: `store` is NOT owned (must outlive this object). `decoder`
/// is owned (moved in).
class ScenePipelineSource : public MediaSource {
 public:
  /// @param store    ObjectStore to query (not owned)
  /// @param topic    Topic ID with serialized scene/annotation messages
  /// @param decoder  Format-specific decoder for this topic's schema (owned)
  ScenePipelineSource(ObjectStore* store, ObjectTopicId topic, std::unique_ptr<ISceneDecoder> decoder);

  void setTimestamp(int64_t ts_ns) override;
  std::optional<MediaFrame> takeFrame() override;

 private:
  ObjectStore* store_;
  ObjectTopicId topic_;
  std::unique_ptr<ISceneDecoder> decoder_;
  std::optional<SceneFrame> pending_scene_;
  int64_t last_ts_ = INT64_MIN;
};

}  // namespace PJ
