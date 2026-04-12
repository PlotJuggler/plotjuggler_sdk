#include "pj_media_qt/mpv_backend.h"

#include <clocale>
#include <cstring>

namespace PJ {

MpvBackend::MpvBackend() {
  std::setlocale(LC_NUMERIC, "C");

  mpv_ = mpv_create();
  if (mpv_ == nullptr) {
    return;
  }

  mpv_set_option_string(mpv_, "hwdec", "auto");
  mpv_set_option_string(mpv_, "hr-seek", "yes");
  mpv_set_option_string(mpv_, "keep-open", "yes");
  mpv_set_option_string(mpv_, "idle", "yes");
  mpv_set_option_string(mpv_, "vo", "libmpv");
  mpv_set_option_string(mpv_, "terminal", "no");

  if (mpv_initialize(mpv_) < 0) {
    mpv_terminate_destroy(mpv_);
    mpv_ = nullptr;
    return;
  }

  mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
  mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
}

MpvBackend::~MpvBackend() {
  if (mpv_gl_ != nullptr) {
    mpv_render_context_free(mpv_gl_);
  }
  if (mpv_ != nullptr) {
    mpv_terminate_destroy(mpv_);
  }
}

bool MpvBackend::open(const std::string& path) {
  if (mpv_ == nullptr) {
    return false;
  }
  const char* cmd[] = {"loadfile", path.c_str(), nullptr};
  return mpv_command(mpv_, cmd) == 0;
}

void MpvBackend::close() {
  if (mpv_ == nullptr) {
    return;
  }
  const char* cmd[] = {"stop", nullptr};
  mpv_command(mpv_, cmd);
}

void MpvBackend::seek(double seconds) {
  if (mpv_ == nullptr) {
    return;
  }
  auto ts = std::to_string(seconds);
  const char* cmd[] = {"seek", ts.c_str(), "absolute+exact", nullptr};
  mpv_command_async(mpv_, 0, cmd);
}

void MpvBackend::setPaused(bool paused) {
  if (mpv_ == nullptr) {
    return;
  }
  int val = paused ? 1 : 0;
  mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &val);
}

bool MpvBackend::isPaused() const {
  if (mpv_ == nullptr) {
    return true;
  }
  int val = 1;
  mpv_get_property(mpv_, "pause", MPV_FORMAT_FLAG, &val);
  return val != 0;
}

double MpvBackend::duration() const {
  if (mpv_ == nullptr) {
    return 0.0;
  }
  double val = 0.0;
  mpv_get_property(mpv_, "duration", MPV_FORMAT_DOUBLE, &val);
  return val;
}

double MpvBackend::position() const {
  if (mpv_ == nullptr) {
    return 0.0;
  }
  double val = 0.0;
  mpv_get_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &val);
  return val;
}

void MpvBackend::stepForward() {
  if (mpv_ == nullptr) {
    return;
  }
  const char* cmd[] = {"frame-step", nullptr};
  mpv_command_async(mpv_, 0, cmd);
}

void MpvBackend::stepBackward() {
  if (mpv_ == nullptr) {
    return;
  }
  const char* cmd[] = {"frame-back-step", nullptr};
  mpv_command_async(mpv_, 0, cmd);
}

void MpvBackend::setPositionCallback(PositionCallback cb) {
  on_position_ = std::move(cb);
}

void MpvBackend::setDurationCallback(DurationCallback cb) {
  on_duration_ = std::move(cb);
}

void MpvBackend::setFileLoadedCallback(FileLoadedCallback cb) {
  on_file_loaded_ = std::move(cb);
}

void MpvBackend::renderFrame(int fbo_id, int width, int height) {
  if (mpv_gl_ == nullptr) {
    return;
  }
  mpv_opengl_fbo fbo{fbo_id, width, height, 0};
  int flip_y = 1;
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
      {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  mpv_render_context_render(mpv_gl_, params);
}

void MpvBackend::processEvents() {
  if (mpv_ == nullptr) {
    return;
  }
  while (true) {
    mpv_event* event = mpv_wait_event(mpv_, 0);
    if (event->event_id == MPV_EVENT_NONE) {
      break;
    }
    if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
      auto* prop = static_cast<mpv_event_property*>(event->data);
      if (std::strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
        if (on_position_) {
          on_position_(*static_cast<double*>(prop->data));
        }
      } else if (std::strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
        if (on_duration_) {
          on_duration_(*static_cast<double*>(prop->data));
        }
      }
    } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
      if (on_file_loaded_) {
        on_file_loaded_();
      }
    }
  }
}

bool MpvBackend::initRenderContext(void* (*get_proc_address)(void*, const char*), void* ctx) {
  if (mpv_ == nullptr) {
    return false;
  }
  mpv_opengl_init_params gl_init_params{get_proc_address, ctx, nullptr};
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
      {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  return mpv_render_context_create(&mpv_gl_, mpv_, params) == 0;
}

}  // namespace PJ
