// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/camera_info_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace PJ {
namespace {

using sdk::CameraInfo;

TEST(CameraInfoCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaCameraInfo, "PJ.CameraInfo");
}

TEST(CameraInfoCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeCameraInfo(nullptr, 0).has_value());
}

TEST(CameraInfoCodecTest, RoundTripFullCalibration) {
  CameraInfo in;
  in.timestamp_ns = 1'500'000'000LL;
  in.frame_id = "camera_optical";
  in.width = 640;
  in.height = 480;
  in.distortion_model = "plumb_bob";
  in.D = {0.1, -0.2, 0.001, 0.002, 0.0};
  in.K = {525.0, 0.0, 319.5, 0.0, 525.0, 239.5, 0.0, 0.0, 1.0};
  in.R = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  in.P = {525.0, 0.0, 319.5, 0.0, 0.0, 525.0, 239.5, 0.0, 0.0, 0.0, 1.0, 0.0};

  const auto bytes = serializeCameraInfo(in);
  auto out = deserializeCameraInfo(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, in);
}

TEST(CameraInfoCodecTest, RectifiedNoDistortionRoundTrips) {
  CameraInfo in;
  in.frame_id = "cam";
  in.width = 1;
  in.height = 1;
  in.K = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  // D empty, R/P default-zero — exercise the packed-empty / default paths.

  const auto bytes = serializeCameraInfo(in);
  auto out = deserializeCameraInfo(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, in);
  EXPECT_TRUE(out->D.empty());
}

}  // namespace
}  // namespace PJ
