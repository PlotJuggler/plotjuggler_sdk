#pragma once

#include <vector>

#include "pj_base/types.hpp"
#include "pj_scene_protocol/builtin/ImageAnnotations.h"

namespace PJ {

/// Top-level output of a SceneDecoder. Wraps a list of ImageAnnotations for
/// this iteration; future iterations will extend with 3D ScenePrimitive,
/// Grid, etc.
struct SceneFrame {
  Timestamp timestamp = 0;
  std::vector<sdk::ImageAnnotations> annotations;
  bool operator==(const SceneFrame&) const = default;

  [[nodiscard]] bool empty() const noexcept {
    return annotations.empty();
  }
};

}  // namespace PJ
