// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ {
namespace {

// Fake host for pj.playback.v1: records the last call and serves a settable
// state snapshot. to_display_time applies a fixed offset so the args-to-result
// path is observable.
struct FakePlaybackHost {
  bool play_called = false;
  bool pause_called = false;
  double last_seek_s = 0.0;
  double last_rate = 0.0;
  bool should_fail = false;

  PJ_playback_state_t state{};

  std::string last_topic;
  int64_t last_absolute_ns = 0;
  int64_t display_offset_ns = 0;
};

bool pbFail(FakePlaybackHost* self, PJ_error_t* out_error) noexcept {
  if (self->should_fail) {
    sdk::fillError(out_error, 1, "playback", "playback boom");
    return true;
  }
  return false;
}

bool pbPlay(void* ctx, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlaybackHost*>(ctx);
  if (pbFail(self, out_error)) {
    return false;
  }
  self->play_called = true;
  return true;
}

bool pbPause(void* ctx, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlaybackHost*>(ctx);
  if (pbFail(self, out_error)) {
    return false;
  }
  self->pause_called = true;
  return true;
}

bool pbSeek(void* ctx, double time_s, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlaybackHost*>(ctx);
  if (pbFail(self, out_error)) {
    return false;
  }
  self->last_seek_s = time_s;
  return true;
}

bool pbSetRate(void* ctx, double rate, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlaybackHost*>(ctx);
  if (pbFail(self, out_error)) {
    return false;
  }
  self->last_rate = rate;
  return true;
}

bool pbGetState(void* ctx, PJ_playback_state_t* out_state, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlaybackHost*>(ctx);
  if (pbFail(self, out_error)) {
    return false;
  }
  *out_state = self->state;
  return true;
}

bool pbToDisplayTime(
    void* ctx, PJ_string_view_t topic, int64_t absolute_ns, double* out_display_s, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakePlaybackHost*>(ctx);
  if (pbFail(self, out_error)) {
    return false;
  }
  self->last_topic = std::string(sdk::toStringView(topic));
  self->last_absolute_ns = absolute_ns;
  *out_display_s = static_cast<double>(absolute_ns - self->display_offset_ns) * 1e-9;
  return true;
}

PJ_playback_host_vtable_t makePlaybackVtable() {
  return PJ_playback_host_vtable_t{
      .protocol_version = 1,
      .struct_size = sizeof(PJ_playback_host_vtable_t),
      .play = pbPlay,
      .pause = pbPause,
      .seek = pbSeek,
      .set_playback_rate = pbSetRate,
      .get_state = pbGetState,
      .to_display_time = pbToDisplayTime,
  };
}

// Fake host for pj.viewport.v1: records the last zoom call.
struct FakeViewportHost {
  double last_t0_s = 0.0;
  double last_t1_s = 0.0;
  bool reset_called = false;
  bool should_fail = false;
};

bool vpZoom(void* ctx, double t0_s, double t1_s, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeViewportHost*>(ctx);
  if (self->should_fail) {
    sdk::fillError(out_error, 1, "viewport", "zoom boom");
    return false;
  }
  self->last_t0_s = t0_s;
  self->last_t1_s = t1_s;
  return true;
}

bool vpReset(void* ctx, PJ_error_t* out_error) noexcept {
  auto* self = static_cast<FakeViewportHost*>(ctx);
  if (self->should_fail) {
    sdk::fillError(out_error, 1, "viewport", "reset boom");
    return false;
  }
  self->reset_called = true;
  return true;
}

PJ_viewport_host_vtable_t makeViewportVtable() {
  return PJ_viewport_host_vtable_t{
      .protocol_version = 1,
      .struct_size = sizeof(PJ_viewport_host_vtable_t),
      .zoom_to_time_range = vpZoom,
      .zoom_reset = vpReset,
  };
}

// --- PlaybackHostView --------------------------------------------------------

TEST(PlaybackApiTest, PlayPauseForwardToHost) {
  FakePlaybackHost host;
  const auto vtable = makePlaybackVtable();
  sdk::PlaybackHostView view(PJ_playback_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.play());
  EXPECT_TRUE(host.play_called);
  ASSERT_TRUE(view.pause());
  EXPECT_TRUE(host.pause_called);
}

TEST(PlaybackApiTest, SeekAndRateForwardValues) {
  FakePlaybackHost host;
  const auto vtable = makePlaybackVtable();
  sdk::PlaybackHostView view(PJ_playback_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.seek(42.25));
  EXPECT_DOUBLE_EQ(host.last_seek_s, 42.25);
  ASSERT_TRUE(view.setPlaybackRate(0.5));
  EXPECT_DOUBLE_EQ(host.last_rate, 0.5);
}

TEST(PlaybackApiTest, StateMarshalsAllFields) {
  FakePlaybackHost host;
  host.state = PJ_playback_state_t{
      .is_playing = true, .current_time_s = 12.5, .range_min_s = 1.0, .range_max_s = 99.0, .playback_rate = 2.0};
  const auto vtable = makePlaybackVtable();
  sdk::PlaybackHostView view(PJ_playback_host_t{.ctx = &host, .vtable = &vtable});

  auto state = view.state();
  ASSERT_TRUE(state) << state.error();
  EXPECT_TRUE(state->is_playing);
  EXPECT_DOUBLE_EQ(state->current_time_s, 12.5);
  EXPECT_DOUBLE_EQ(state->range_min_s, 1.0);
  EXPECT_DOUBLE_EQ(state->range_max_s, 99.0);
  EXPECT_DOUBLE_EQ(state->playback_rate, 2.0);
}

TEST(PlaybackApiTest, ToDisplayTimeForwardsTopicAndConverts) {
  FakePlaybackHost host;
  host.display_offset_ns = 1'000'000'000;  // 1 s
  const auto vtable = makePlaybackVtable();
  sdk::PlaybackHostView view(PJ_playback_host_t{.ctx = &host, .vtable = &vtable});

  auto display_s = view.toDisplayTime("imu/accel", 3'500'000'000);
  ASSERT_TRUE(display_s) << display_s.error();
  EXPECT_EQ(host.last_topic, "imu/accel");
  EXPECT_EQ(host.last_absolute_ns, 3'500'000'000);
  EXPECT_DOUBLE_EQ(*display_s, 2.5);
}

TEST(PlaybackApiTest, HostFailureSurfacesError) {
  FakePlaybackHost host;
  host.should_fail = true;
  const auto vtable = makePlaybackVtable();
  sdk::PlaybackHostView view(PJ_playback_host_t{.ctx = &host, .vtable = &vtable});

  auto status = view.play();
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("playback boom"), std::string::npos);
}

TEST(PlaybackApiTest, UnboundViewReportsNotBound) {
  sdk::PlaybackHostView view;  // default-constructed = not bound
  EXPECT_FALSE(view.valid());

  EXPECT_FALSE(view.play());
  EXPECT_FALSE(view.pause());
  EXPECT_FALSE(view.seek(0.0));
  EXPECT_FALSE(view.setPlaybackRate(1.0));
  EXPECT_FALSE(view.state());
  auto converted = view.toDisplayTime("t", 0);
  EXPECT_FALSE(converted);
  EXPECT_NE(converted.error().find("not bound"), std::string::npos);
}

// --- ViewportHostView --------------------------------------------------------

TEST(ViewportApiTest, ZoomForwardsRange) {
  FakeViewportHost host;
  const auto vtable = makeViewportVtable();
  sdk::ViewportHostView view(PJ_viewport_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.zoomToTimeRange(2.0, 8.5));
  EXPECT_DOUBLE_EQ(host.last_t0_s, 2.0);
  EXPECT_DOUBLE_EQ(host.last_t1_s, 8.5);
}

TEST(ViewportApiTest, ResetForwards) {
  FakeViewportHost host;
  const auto vtable = makeViewportVtable();
  sdk::ViewportHostView view(PJ_viewport_host_t{.ctx = &host, .vtable = &vtable});

  ASSERT_TRUE(view.zoomReset());
  EXPECT_TRUE(host.reset_called);
}

TEST(ViewportApiTest, HostFailureSurfacesError) {
  FakeViewportHost host;
  host.should_fail = true;
  const auto vtable = makeViewportVtable();
  sdk::ViewportHostView view(PJ_viewport_host_t{.ctx = &host, .vtable = &vtable});

  auto status = view.zoomToTimeRange(0.0, 1.0);
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("zoom boom"), std::string::npos);
}

TEST(ViewportApiTest, UnboundViewReportsNotBound) {
  sdk::ViewportHostView view;
  EXPECT_FALSE(view.valid());

  auto status = view.zoomReset();
  EXPECT_FALSE(status);
  EXPECT_NE(status.error().find("not bound"), std::string::npos);
}

}  // namespace
}  // namespace PJ
