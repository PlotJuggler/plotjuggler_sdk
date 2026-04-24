#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/object_store.hpp"
#include "pj_media_core/decoded_frame.h"

namespace PJ {

class FfmpegDecoder;

/// Decodes H.264 VideoFrame entries stored in ObjectStore.
///
/// VideoFrame entries are annex-B encoded NAL units: each ObjectStore entry
/// is exactly one frame. Keyframes contain SPS + PPS + IDR slice; P-frames
/// contain only the slice NAL.
///
/// Two modes of use (mutually exclusive per REQUIREMENTS.md §4.3):
/// - **Live mode**: call decodeAt(timeRange().second) on each tick.
///   The decoder advances sequentially — no seeking needed.
/// - **Scrub mode**: buffer is frozen (no pushes/eviction). Call decodeAt()
///   with any timestamp in the retained window. The decoder seeks to the
///   nearest preceding keyframe and decodes forward to the target.
///
/// The decoder builds a keyframe index incrementally by NAL-inspecting each
/// new entry. This is the "streaming sources" path from REQUIREMENTS.md §4.4.
class StreamingVideoDecoder {
 public:
  StreamingVideoDecoder();
  ~StreamingVideoDecoder();

  StreamingVideoDecoder(const StreamingVideoDecoder&) = delete;
  StreamingVideoDecoder& operator=(const StreamingVideoDecoder&) = delete;
  StreamingVideoDecoder(StreamingVideoDecoder&&) = delete;
  StreamingVideoDecoder& operator=(StreamingVideoDecoder&&) = delete;

  /// Attach to an ObjectStore topic. Does not open the FFmpeg decoder yet —
  /// waits for the first keyframe to auto-detect codec parameters.
  void attach(ObjectStore* store, ObjectTopicId topic);

  /// Decode the frame at-or-before the given timestamp.
  /// Returns the decoded frame, or an error if:
  /// - Not attached
  /// - No keyframe exists yet (waiting to join stream)
  /// - The target's keyframe was evicted (undecodable)
  Expected<DecodedFrame> decodeAt(Timestamp ts);

  /// Reset decoder state. Call when switching modes or after ObjectStore::clear().
  void reset();

  /// Has the decoder seen at least one keyframe and successfully opened?
  [[nodiscard]] bool isInitialized() const;

 private:
  void updateKeyframeIndex();
  [[nodiscard]] Timestamp findKeyframeBefore(Timestamp ts) const;
  bool initDecoder(const uint8_t* data, size_t size);
  Expected<DecodedFrame> decodeRange(size_t start_idx, size_t target_idx);

  ObjectStore* store_ = nullptr;
  ObjectTopicId topic_{};

  std::unique_ptr<FfmpegDecoder> decoder_;
  bool initialized_ = false;

  // Keyframe timestamps (sorted, ascending). Updated incrementally.
  std::vector<Timestamp> keyframe_timestamps_;
  std::optional<Timestamp> last_scanned_ts_;

  // Last packet timestamp sent to the decoder (tracks ObjectStore position,
  // not the PTS of the decoded output — those differ with B-frames).
  std::optional<Timestamp> last_sent_ts_;
  DecodedFrame last_frame_;  // Cached for same-timestamp re-requests
};

}  // namespace PJ
