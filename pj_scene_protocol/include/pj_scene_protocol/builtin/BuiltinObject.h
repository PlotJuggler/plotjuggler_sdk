/**
 * @file BuiltinObject.h
 * @brief Type-erased holder for any builtin object a MessageParser may produce.
 *
 * BuiltinObject is `std::any`. A producer constructs it by passing a
 * concrete builtin value (`sdk::Image`, `sdk::PointCloud`, `sdk::DepthImage`,
 * `sdk::ImageAnnotations`, …); a consumer recovers the concrete type via
 * `std::any_cast<T>(&obj)` and obtains the kind tag via `kindOf(obj)`.
 *
 * The type erasure is deliberate: choosing `std::any` over `std::variant`
 * keeps the SDK forward-compatible. Plugins built against an older SDK can
 * keep producing the alternatives they know without any TU referencing the
 * (later-extended) full alternative list; hosts built against an older SDK
 * that receive an unknown kind simply see `BuiltinObjectKind::kNone` from
 * `kindOf` and reject the message. No protocol bump required when a new
 * builtin kind is appended to BuiltinObjectKind and its header.
 */
#pragma once

#include <any>

#include "pj_scene_protocol/builtin/BuiltinObjectKind.h"
#include "pj_scene_protocol/builtin/DepthImage.h"
#include "pj_scene_protocol/builtin/Image.h"
#include "pj_scene_protocol/builtin/ImageAnnotations.h"
#include "pj_scene_protocol/builtin/PointCloud.h"

namespace PJ {
namespace sdk {

using BuiltinObject = std::any;

/// Get the kind tag for a BuiltinObject without copying it.
/// Returns kNone for an empty BuiltinObject or one that wraps a type
/// unknown to this SDK build.
[[nodiscard]] inline BuiltinObjectKind kindOf(const BuiltinObject& obj) noexcept {
  if (!obj.has_value()) {
    return BuiltinObjectKind::kNone;
  }
  const auto& t = obj.type();
  if (t == typeid(Image)) {
    return BuiltinObjectKind::kImage;
  }
  if (t == typeid(PointCloud)) {
    return BuiltinObjectKind::kPointCloud;
  }
  if (t == typeid(DepthImage)) {
    return BuiltinObjectKind::kDepthImage;
  }
  if (t == typeid(ImageAnnotations)) {
    return BuiltinObjectKind::kImageAnnotations;
  }
  return BuiltinObjectKind::kNone;
}

}  // namespace sdk
}  // namespace PJ
