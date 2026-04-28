#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "pj_media_core/media_source.h"

namespace PJ {

/// Multi-layer MediaSource. Fans setTimestamp() out to all owned layers and
/// fuses their MediaFrames into one composite frame returned by takeFrame().
///
/// Fusion rules (this iteration):
///   - Pixel base: the FIRST layer that produces a `.base` wins. Additional
///     bases from later layers are silently dropped. The expected use case
///     is one image/video base layer + one or more overlay layers.
///   - Overlays: every layer's `.overlays` are concatenated in layer order
///     (so overlays from layer 0 render under overlays from layer 1).
///
/// Returns nullopt if no layer produced new data on this poll.
///
/// Ownership: the compositor takes ownership of every added layer. Layers
/// are polled in the order they were added.
class CompositeMediaSource : public MediaSource {
 public:
  CompositeMediaSource() = default;

  /// Append a layer. Layers are polled in addition order during takeFrame().
  void addLayer(std::unique_ptr<MediaSource> layer);

  /// Number of layers currently owned.
  [[nodiscard]] size_t layerCount() const noexcept {
    return layers_.size();
  }

  void setTimestamp(int64_t ts_ns) override;
  std::optional<MediaFrame> takeFrame() override;

 private:
  std::vector<std::unique_ptr<MediaSource>> layers_;
};

}  // namespace PJ
