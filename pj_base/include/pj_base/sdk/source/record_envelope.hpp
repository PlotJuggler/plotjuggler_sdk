// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "pj_base/sdk/source/source_descriptor.hpp"

namespace PJ::sdk::source {

inline constexpr unsigned kSourceRecordEnvelopeVersion = 1;

/// Host acceptance envelope v1: only kind (non-empty string), v (unsigned JSON
/// integer, preserved without narrowing), request (object), label (optional
/// string). Uses SourceDescriptorPolicy's 64 KiB/4096-byte/4096-entry/depth-16
/// bounds and refuses credential-shaped keys at any depth. Provider-specific
/// version values and request semantics remain the provider's responsibility.
/// Does not reserialize bytes: cache identity is over the verbatim descriptor.
/// Pre-envelope descriptors (including flat mcap_cloud v1) need an explicit
/// versioned restore adapter, never silent re-wrapping of canonical bytes.
[[nodiscard]] Expected<nlohmann::json> parseSourceRecordEnvelope(std::string_view bytes);

}  // namespace PJ::sdk::source
