#pragma once

#include <cstdint>
#include <optional>

#include "pj_media_core/media_frame.h"

namespace PJ {

/// Uniform frame-delivery interface between decoder backends and
/// MediaViewerWidget.  The main thread calls setTimestamp() when the
/// global time changes and takeFrame() at render rate.
///
/// Concrete implementations:
///   ImagePipelineSource  — synchronous CodecPipeline + ObjectStore
///   FileVideoSource      — wraps FfmpegBackend (file-based video)
///   StreamingVideoSource — wraps StreamingVideoDecoder + worker thread
///   ScenePipelineSource  — vector overlays decoded from ObjectStore
///   CompositeMediaSource — fans out across N layers, returns one MediaFrame
class MediaSource {
 public:
  virtual ~MediaSource() = default;

  MediaSource() = default;
  MediaSource(const MediaSource&) = delete;
  MediaSource& operator=(const MediaSource&) = delete;
  MediaSource(MediaSource&&) = delete;
  MediaSource& operator=(MediaSource&&) = delete;

  /// Called by main thread when the global time changes.
  /// May decode synchronously (images) or post to an internal worker (video).
  virtual void setTimestamp(int64_t ts_ns) = 0;

  /// Called by main thread at render rate.
  /// Returns the latest MediaFrame (base pixels and/or overlays), or nullopt
  /// if nothing new since the last call.
  virtual std::optional<MediaFrame> takeFrame() = 0;
};

}  // namespace PJ
