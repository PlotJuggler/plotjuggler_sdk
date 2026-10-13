// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/depth_image_codec.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "pj_base/builtin/depth_image_utils.hpp"
#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::DepthImage;
namespace pb = ::PJ::test_pb;

TEST(DepthImageCodecTest, SchemaName) {
  EXPECT_EQ(kSchemaDepthImage, "PJ.DepthImage");
}

TEST(DepthImageCodecTest, EmptyBufferProducesError) {
  EXPECT_FALSE(deserializeDepthImage(nullptr, 0).has_value());
}

TEST(DepthImageCodecTest, RoundTripRectified16UC1) {
  DepthImage in;
  in.timestamp_ns = 1'234'000'000LL;
  in.width = 2;
  in.height = 2;
  in.encoding = "16UC1";
  in.K = {525.0, 0.0, 319.5, 0.0, 525.0, 239.5, 0.0, 0.0, 1.0};
  // empty distortion_model => rectified
  const std::vector<uint8_t> payload = {0x10, 0x00, 0x20, 0x00, 0x30, 0x00, 0x40, 0x00};
  in.data = Span<const uint8_t>(payload.data(), payload.size());

  const auto bytes = serializeDepthImage(in);
  auto out = deserializeDepthImage(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->width, in.width);
  EXPECT_EQ(out->height, in.height);
  EXPECT_EQ(out->encoding, in.encoding);
  EXPECT_EQ(out->K, in.K);
  EXPECT_TRUE(out->distortion_model.empty());
  EXPECT_TRUE(out->D.empty());
  ASSERT_EQ(out->data.size(), payload.size());
  EXPECT_EQ(std::memcmp(out->data.data(), payload.data(), payload.size()), 0);
}

TEST(DepthImageCodecTest, RoundTripPlumbBobDistortion) {
  DepthImage in;
  in.width = 640;
  in.height = 480;
  in.encoding = "32FC1";
  in.K = {525.0, 0.0, 319.5, 0.0, 525.0, 239.5, 0.0, 0.0, 1.0};
  in.distortion_model = "plumb_bob";
  in.D = {-0.1, 0.05, 0.001, -0.002, 0.0};

  const auto bytes = serializeDepthImage(in);
  auto out = deserializeDepthImage(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->distortion_model, "plumb_bob");
  ASSERT_EQ(out->D.size(), 5u);
  for (size_t i = 0; i < in.D.size(); ++i) {
    EXPECT_DOUBLE_EQ(out->D[i], in.D[i]) << "D[" << i << "]";
  }
}

// K * p / p.z, the forward pinhole projection: the oracle for unprojection.
std::array<double, 2> project(const std::array<double, 9>& k, const std::array<double, 3>& p) {
  const double x = k[0] * p[0] + k[1] * p[1] + k[2] * p[2];
  const double y = k[3] * p[0] + k[4] * p[1] + k[5] * p[2];
  const double z = k[6] * p[0] + k[7] * p[1] + k[8] * p[2];
  return {x / z, y / z};
}

// Unprojects (u, v, depth) through `k` and checks the point projects back to (u, v).
void expectReprojects(const std::array<double, 9>& k, double u, double v, double depth) {
  const auto point = sdk::unprojectPixel(k, u, v, depth);
  ASSERT_TRUE(point);
  const auto reprojected = project(k, *point);
  EXPECT_NEAR(reprojected[0], u, 1e-9);
  EXPECT_NEAR(reprojected[1], v, 1e-9);
}

TEST(DepthImageUtilsTest, PurePinholeMatchesTheShortFormula) {
  const std::array<double, 9> k{525.3, 0, 319.5, 0, 524.7, 239.5, 0, 0, 1};
  const double u = 123.4;
  const double v = 456.7;
  const double depth = 3.21;
  const auto p = sdk::unprojectPixel(k, u, v, depth);
  ASSERT_TRUE(p);
  const std::array<double, 3> expected{(u - k[2]) / k[0] * depth, (v - k[5]) / k[4] * depth, depth};
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR((*p)[i], expected[i], 1e-12 * std::abs(expected[i])) << "component " << i;
  }
}

TEST(DepthImageUtilsTest, AcceptsFloat32RoundedK) {
  // k[8] = 1.0000001f and a float32 skew: neither is exactly 1/0 in double.
  std::array<double, 9> k{525, static_cast<double>(1e-4f), 319.5, 0, 525, 239.5, 0, 0, static_cast<double>(1.0000001f)};
  ASSERT_NE(k[8], 1.0);
  expectReprojects(k, 100, 50, 2);
}

TEST(DepthImageUtilsTest, SkewedKReprojectsToThePixel) {
  const std::array<double, 9> skewed{500, 2.5, 320, 0, 510, 240, 0, 0, 1};
  const std::array<double, 9> projective{500, 2.5, 320, 0.001, 510, 240, 1e-4, 2e-4, 1};
  expectReprojects(skewed, 37.5, 411.25, 1.75);
  expectReprojects(projective, 37.5, 411.25, 1.75);
  // A skewed K is not the pure-pinhole one: the short formula would be wrong here.
  const auto p = sdk::unprojectPixel(skewed, 37.5, 411.25, 1.75);
  ASSERT_TRUE(p);
  EXPECT_GT(std::abs((*p)[0] - (37.5 - 320) / 500 * 1.75), 1e-3);
}

TEST(DepthImageUtilsTest, RejectsSingularK) {
  EXPECT_FALSE(sdk::PinholeIntrinsics::fromK({}));
  EXPECT_FALSE(sdk::PinholeIntrinsics::fromK({0, 0, 1, 0, 4, 1, 0, 0, 1}));  // fx == 0
  EXPECT_FALSE(sdk::PinholeIntrinsics::fromK({1, 2, 3, 2, 4, 6, 0, 0, 1}));  // proportional rows
}

TEST(DepthImageUtilsTest, RejectsNonFiniteK) {
  const double bad_values[] = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};
  for (double bad : bad_values) {
    for (size_t i = 0; i < 9; ++i) {
      std::array<double, 9> k{2, 0, 1, 0, 4, 1, 0, 0, 1};
      k[i] = bad;
      EXPECT_FALSE(sdk::PinholeIntrinsics::fromK(k)) << "entry " << i;
    }
  }
  // Finite entries whose 2x2 minor overflows (1e300 * 1e300): the inverse is not finite.
  EXPECT_FALSE(sdk::PinholeIntrinsics::fromK({1e300, 0, 0, 0, 1e300, 0, 0, 0, 1e-300}));
  // Finite entries whose determinant overflows (1e110^3): the inverse would collapse to zeros.
  EXPECT_FALSE(sdk::PinholeIntrinsics::fromK({1e110, 0, 0, 0, 1e110, 0, 0, 0, 1e110}));
}

TEST(DepthImageUtilsTest, RejectsRaysBehindTheCamera) {
  // K^-1 [u, v, 1] has z == -1 for every pixel.
  const std::array<double, 9> k{1, 0, 0, 0, 1, 0, 0, 0, -1};
  const auto intrinsics = sdk::PinholeIntrinsics::fromK(k);
  ASSERT_TRUE(intrinsics);
  EXPECT_FALSE(intrinsics->unproject(3, 5, 2));
}

TEST(DepthImageUtilsTest, RejectsInvalidPixelOrDepth) {
  const std::array<double, 9> k{2, 0, 1, 0, 4, 1, 0, 0, 1};
  ASSERT_TRUE(sdk::unprojectPixel(k, 3, 5, 2));
  EXPECT_FALSE(sdk::unprojectPixel(k, 3, 5, 0));
  EXPECT_FALSE(sdk::unprojectPixel(k, 3, 5, -1));
  EXPECT_FALSE(sdk::unprojectPixel(k, std::numeric_limits<double>::quiet_NaN(), 5, 2));
  EXPECT_FALSE(sdk::unprojectPixel(k, 3, std::numeric_limits<double>::infinity(), 2));
  EXPECT_FALSE(sdk::unprojectPixel(k, 3, 5, std::numeric_limits<double>::quiet_NaN()));
}

}  // namespace
}  // namespace PJ
