#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file parser_module_manifest.hpp
 * @brief WebAssembly parser-module manifest custom-section codec.
 */

#include <cstdint>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/parser_module_abi.h"
#include "pj_base/span.hpp"

namespace PJ::parser_module {

/** Append the parser-module manifest custom section to a wasm binary.
 *
 * The input must have a valid wasm preamble and fully bounded sections. It
 * must not already contain PJ_PARSER_MODULE_MANIFEST_SECTION_NAME. The result
 * contains exactly one such section, appended after every input section, and
 * its payload after the custom-section name is byte-identical to `manifest`.
 */
[[nodiscard]] Expected<std::vector<uint8_t>> appendManifestSection(
    Span<const uint8_t> wasm, Span<const uint8_t> manifest);

/** Return the borrowed manifest bytes from a wasm custom section.
 *
 * The complete module is bounds-checked. A malformed preamble or section, no
 * parser-module manifest section, or more than one such section is an error.
 * The returned view remains valid only while `wasm` remains valid.
 */
[[nodiscard]] Expected<Span<const uint8_t>> readManifestSection(Span<const uint8_t> wasm);

}  // namespace PJ::parser_module
