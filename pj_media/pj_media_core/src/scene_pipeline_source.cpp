#include "pj_media_core/scene_pipeline_source.h"

#include <utility>

namespace PJ {

ScenePipelineSource::ScenePipelineSource(
    ObjectStore* store, ObjectTopicId topic, std::unique_ptr<ISceneDecoder> decoder)
    : store_(store), topic_(topic), decoder_(std::move(decoder)) {}

void ScenePipelineSource::setTimestamp(int64_t ts_ns) {
  if (ts_ns == last_ts_) {
    return;  // same timestamp — reuse pending result, skip redundant decode
  }
  last_ts_ = ts_ns;

  // Copy bytes out of the store before decoding — keep no series-mutex held
  // through the decode (lock-discipline pattern documented in
  // ObjectStore::entryTimestamps).
  auto entry = store_->latestAt(topic_, ts_ns);
  if (!entry.has_value() || entry->data == nullptr || entry->data->empty()) {
    pending_scene_.reset();
    return;
  }

  auto result = decoder_->decode(entry->data->data(), entry->data->size());
  if (result.has_value()) {
    pending_scene_ = std::move(*result);
  } else {
    pending_scene_.reset();
  }
}

std::optional<MediaFrame> ScenePipelineSource::takeFrame() {
  if (!pending_scene_.has_value()) {
    return std::nullopt;
  }
  MediaFrame mf;
  mf.overlays.push_back(std::move(*pending_scene_));
  pending_scene_.reset();
  return mf;
}

}  // namespace PJ
