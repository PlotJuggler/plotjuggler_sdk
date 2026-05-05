#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_scene_protocol/image_annotation.h"

namespace PJ {

/// Wire-format identifier for image annotations on ObjectStore. Loaders stamp
/// this in the topic's `metadata_json` under the key "encoding"; pj_media
/// looks for it before decoding. The literal value is the published Foxglove
/// schema name; we conform to that wire format spec, which gives us free
/// interop with Foxglove Studio and other tools.
inline constexpr std::string_view kSchemaImageAnnotations = "foxglove.ImageAnnotations";

/// Serializes an ImageAnnotation to canonical foxglove.ImageAnnotations
/// Protobuf bytes.
///
/// `timestamp` and `image_topic` on the input are NOT serialized — the
/// timestamp travels with ObjectStore's push, topic identity with the topic
/// registration. Round-trip equality holds when the caller leaves both at
/// default values.
[[nodiscard]] std::vector<uint8_t> serializeImageAnnotation(const ImageAnnotation& ia);

}  // namespace PJ
