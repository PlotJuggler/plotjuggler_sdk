#include "pj_media_core/file_video_source.h"

#include "pj_media_core/ffmpeg_backend.h"

namespace PJ {

FileVideoSource::FileVideoSource() : backend_(std::make_unique<FfmpegBackend>()) {}

FileVideoSource::~FileVideoSource() = default;

Expected<std::unique_ptr<FileVideoSource>> FileVideoSource::open(const std::string& path) {
  auto source = std::unique_ptr<FileVideoSource>(new FileVideoSource());

  // Wire internal frame callback to capture latest decoded frame
  source->backend_->setFrameCallback([source_ptr = source.get()](const DecodedFrame& frame) {
    std::lock_guard lock(source_ptr->frame_mutex_);
    source_ptr->pending_frame_ = frame;
  });

  if (!source->backend_->open(path)) {
    return unexpected("failed to open video: " + path);
  }

  return source;
}

void FileVideoSource::setTimestamp(int64_t ts_ns) {
  double seconds = static_cast<double>(ts_ns) / 1'000'000'000.0;
  backend_->seek(seconds);
}

std::optional<DecodedFrame> FileVideoSource::takeFrame() {
  // processEvents fires the frame callback on the caller's thread,
  // which populates pending_frame_ under frame_mutex_.
  backend_->processEvents();

  std::lock_guard lock(frame_mutex_);
  if (!pending_frame_.has_value()) {
    return std::nullopt;
  }
  auto frame = std::move(*pending_frame_);
  pending_frame_.reset();
  return frame;
}

double FileVideoSource::duration() const {
  return backend_->duration();
}

double FileVideoSource::position() const {
  return backend_->position();
}

void FileVideoSource::setPaused(bool paused) {
  backend_->setPaused(paused);
}

bool FileVideoSource::isPaused() const {
  return backend_->isPaused();
}

void FileVideoSource::stepForward() {
  backend_->stepForward();
}

void FileVideoSource::stepBackward() {
  backend_->stepBackward();
}

void FileVideoSource::setPositionCallback(VideoBackend::PositionCallback cb) {
  backend_->setPositionCallback(std::move(cb));
}

void FileVideoSource::setDurationCallback(VideoBackend::DurationCallback cb) {
  backend_->setDurationCallback(std::move(cb));
}

void FileVideoSource::setFileLoadedCallback(VideoBackend::FileLoadedCallback cb) {
  backend_->setFileLoadedCallback(std::move(cb));
}

}  // namespace PJ
