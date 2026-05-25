/**
 * @file asset_video.hpp
 * @brief File-backed video reference + typed playback metadata.
 *
 * AssetVideo is the entry-point handle for video assets ingested by data
 * loaders that point at an external media file (LeRobot datasets, MP4
 * loaders, etc.). Producers push exactly one AssetVideo per topic; the
 * ObjectStore timestamp of that entry equals `time_origin_ns` so timeline
 * UIs naturally see the asset's start instant.
 *
 * Unlike VideoFrame (a single frame of a streamed payload), AssetVideo
 * carries no pixel data — it references the file by path and surfaces
 * decode-routing metadata (media type, dimensions, frame rate) without
 * forcing the consumer to open the file just to size a playback window.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// File-backed video reference with typed metadata.
///
/// `time_origin_ns` carries the wall-clock instant of the first frame.
/// Consumers subtract this from a tracker time to derive a file-relative
/// seek position. An absent value means the asset is not aligned to wall
/// clock and should not advance with the tracker.
///
/// `duration_ns` is the total playable duration when known; absent means
/// consumers may probe the file.
///
/// `media_type` is a MIME-type hint ("video/mp4", "video/x-matroska",
/// "video/av1"). Empty string means "probe the file".
///
/// `width` / `height` are pixel dimensions; zero in either field means
/// "unknown — probe the file".
///
/// `frame_rate` is nominal frames per second; zero or NaN means "unknown —
/// probe the file". For variable-frame-rate video this is an advisory
/// average; actual per-frame timestamps come from the decoder.
struct AssetVideo {
  std::optional<Timestamp> time_origin_ns;  ///< Wall-clock instant of the first frame.
  std::optional<int64_t> duration_ns;       ///< Total playable duration in nanoseconds.
  std::string file_path;                    ///< Absolute path or path relative to a consumer-known root.
  std::string media_type;                   ///< MIME type. Empty string means "probe the file".
  uint32_t width = 0;                       ///< Pixel width. 0 means unknown.
  uint32_t height = 0;                      ///< Pixel height. 0 means unknown.
  double frame_rate = 0.0;                  ///< Nominal frames per second. 0 or NaN means unknown.
};

}  // namespace sdk
}  // namespace PJ
