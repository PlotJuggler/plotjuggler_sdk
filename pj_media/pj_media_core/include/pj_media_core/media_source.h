#pragma once

#include <cstdint>
#include <optional>

#include "pj_media_core/decoded_frame.h"

namespace PJ {

/// Uniform frame-delivery interface between decoder backends and
/// MediaViewerWidget.  The main thread calls setTimestamp() when the
/// global time changes and takeFrame() at render rate.
///
/// Three concrete implementations:
///   ImagePipelineSource  — synchronous CodecPipeline + ObjectStore
///   FileVideoSource      — wraps FfmpegBackend (file-based video)
///   StreamingVideoSource — wraps StreamingVideoDecoder + worker thread
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
  /// Returns the latest decoded frame, or nullopt if nothing new.
  virtual std::optional<DecodedFrame> takeFrame() = 0;
};

}  // namespace PJ
