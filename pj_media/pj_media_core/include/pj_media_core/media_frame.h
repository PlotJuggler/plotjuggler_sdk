#pragma once

#include <optional>
#include <vector>

#include "pj_media_core/decoded_frame.h"
#include "pj_media_core/scene_frame.h"

namespace PJ {

/// Multi-layer frame produced by MediaSource at a given timestamp.
///
/// `base` is the pixel-buffer layer (image, video, depth colormap, segmentation).
/// `overlays` are vector primitives drawn on top (annotations, markers).
///
/// An empty MediaFrame (no base and no overlays) is a valid return — it signals
/// "nothing new since the last takeFrame()".
struct MediaFrame {
  std::optional<DecodedFrame> base;
  std::vector<SceneFrame> overlays;

  [[nodiscard]] bool empty() const noexcept {
    return !base.has_value() && overlays.empty();
  }
};

}  // namespace PJ
