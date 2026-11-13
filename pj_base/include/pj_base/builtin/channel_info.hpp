/**
 * @file channel_info.hpp
 * @brief Semantics of one data channel, shared by every channelled builtin.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pj_base/builtin/image_annotations.hpp"  // for ColorRGBA

namespace PJ {
namespace sdk {

/// How a channel's values are meant to be read, so a viewer can pick a
/// colouring: a colormap for continuous values, a palette for categories.
enum class ChannelKind : uint8_t {
  kContinuous = 0,   ///< a measured or computed quantity (cost, height, probability)
  kCategorical = 1,  ///< integer class ids (semantic label, surface index, region)
};

/// One named value of a categorical channel.
struct ChannelCategory {
  int64_t value = 0;
  std::string label;
  std::optional<ColorRGBA> color;  ///< the producer's colour for it; viewer palette when absent
  bool operator==(const ChannelCategory&) const = default;
};

/// What a channel's numbers mean, beyond their storage type. Every field is
/// optional: a default ChannelInfo describes a continuous, unit-less channel
/// whose only "no data" marker is NaN.
///
/// Shared vocabulary: MeshMap channels embed it; other channelled builtins may
/// attach it to their fields by name.
struct ChannelInfo {
  std::string unit;  ///< e.g. "m", "s", "1/1000 log-odds"; "" = none
  /// The value that means "no data" in an integer channel (an occupancy grid's
  /// 255, a sensor's -1). NaN always means "no data" in a float channel; for one,
  /// this is an additional marker.
  std::optional<double> no_data;
  ChannelKind kind = ChannelKind::kContinuous;
  std::vector<ChannelCategory> categories;  ///< value -> label (+ colour), for kCategorical
  bool operator==(const ChannelInfo&) const = default;
};

}  // namespace sdk
}  // namespace PJ
