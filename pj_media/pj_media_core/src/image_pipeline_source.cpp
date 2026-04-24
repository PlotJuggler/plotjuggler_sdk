#include "pj_media_core/image_pipeline_source.h"

namespace PJ {

ImagePipelineSource::ImagePipelineSource(
    ObjectStore* store, ObjectTopicId topic, std::unique_ptr<CodecPipeline> pipeline)
    : store_(store), topic_(topic), pipeline_(std::move(pipeline)) {}

void ImagePipelineSource::setTimestamp(int64_t ts_ns) {
  if (ts_ns == last_ts_) {
    return;  // same timestamp — reuse pending frame, skip redundant decode
  }
  last_ts_ = ts_ns;

  auto entry = store_->latestAt(topic_, ts_ns);
  if (!entry.has_value() || entry->data == nullptr || entry->data->empty()) {
    pending_frame_.reset();
    return;
  }

  auto result = pipeline_->decode(entry->data->data(), entry->data->size());
  if (result.has_value()) {
    pending_frame_ = std::move(*result);
  } else {
    pending_frame_.reset();
  }
}

std::optional<DecodedFrame> ImagePipelineSource::takeFrame() {
  if (!pending_frame_.has_value()) {
    return std::nullopt;
  }
  auto frame = std::move(*pending_frame_);
  pending_frame_.reset();
  return frame;
}

}  // namespace PJ
