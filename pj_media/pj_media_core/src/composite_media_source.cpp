#include "pj_media_core/composite_media_source.h"

#include <utility>

namespace PJ {

void CompositeMediaSource::addLayer(std::unique_ptr<MediaSource> layer) {
  if (layer != nullptr) {
    layers_.push_back(std::move(layer));
  }
}

void CompositeMediaSource::setTimestamp(int64_t ts_ns) {
  for (auto& layer : layers_) {
    layer->setTimestamp(ts_ns);
  }
}

std::optional<MediaFrame> CompositeMediaSource::takeFrame() {
  MediaFrame composite;
  bool any = false;
  for (auto& layer : layers_) {
    auto f = layer->takeFrame();
    if (!f.has_value()) {
      continue;
    }
    any = true;
    if (f->base.has_value() && !composite.base.has_value()) {
      composite.base = std::move(*f->base);
    }
    for (auto& ov : f->overlays) {
      composite.overlays.push_back(std::move(ov));
    }
  }
  if (!any) {
    return std::nullopt;
  }
  return composite;
}

}  // namespace PJ
