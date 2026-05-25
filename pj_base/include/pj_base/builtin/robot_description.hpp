/**
 * @file robot_description.hpp
 * @brief Robot kinematic + visual model carried as a raw markup document.
 *
 * RobotDescription is a small owned builtin for URDF-style robot descriptions
 * (or other XML-based formats: SDF, MJCF, COLLADA-scene, etc.). The SDK does
 * not parse the document — it carries the raw text and a format hint string,
 * and downstream consumers (the 3D viewer in particular) do the format-
 * specific parsing and asset resolution.
 *
 * Rationale for raw-text-only:
 *   - The format space is open (URDF, SDF, MJCF, MJCF variants, custom).
 *     A format-specific SDK type would multiply schemas without payoff.
 *   - Mesh-file resolution depends on consumer-side configuration (search
 *     paths, MCAP attachments, sidecar directories). Embedding parsed mesh
 *     references in the SDK type would force assumptions about resolution.
 *   - URDFs are usually 1-2 KB; the cost of carrying the string verbatim is
 *     negligible vs. structured re-encoding.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Robot description carried as the source-format text plus a hint about
/// which format the text uses. Producers (e.g. ParserROS) set `format` to
/// something like "urdf" / "sdf" / "mjcf" after validating the root element;
/// consumers route to the matching parser.
struct RobotDescription {
  /// Timestamp the description was observed (usually the message-arrival
  /// time, since /robot_description is rarely updated mid-recording).
  Timestamp timestamp_ns = 0;

  /// Source topic the description came from. Empty if not topic-sourced.
  std::string topic;

  /// Format hint set by the producer, e.g. "urdf", "sdf", "mjcf". Open-ended
  /// like `Image::encoding` so new formats land without an SDK change.
  std::string format;

  /// Raw text of the description (XML for URDF/SDF/COLLADA, JSON-ish for
  /// MJCF wrappers, etc.). Consumers must parse according to `format`.
  std::string text;

  bool operator==(const RobotDescription&) const = default;

  [[nodiscard]] bool empty() const noexcept {
    return text.empty();
  }
};

}  // namespace sdk
}  // namespace PJ
