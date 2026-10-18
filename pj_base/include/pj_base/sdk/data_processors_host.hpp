#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Host-side decoders for the typed requests of "pj.data_processors.v1"
// (create_data_processor_v2, submit_evaluation; see `DataProcessorsHostView` in
// plugin_data_api.hpp for the plugin-facing side). Every host implementation decodes
// through these, so the read-prefix `struct_size` rule and the structural rejections
// live in one place. Kind, language and per-kind time-flag rules stay with the
// backend that serves the kind. Header-only.
//
// A failure is a refusal: report it as PJ_ERROR_CODE_REJECTED with the returned text.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "pj_base/expected.hpp"
#include "pj_base/plugin_data_api.h"

namespace PJ::sdk {

/// The slot a request arrived on; a submit accepts fewer flags than a create.
/// @since 0.37.0
enum class DataProcessorSlot { kCreateV2, kSubmitEvaluation };

namespace detail {

/// The prefix of a caller-sized struct that this header knows, zero-filled beyond a
/// shorter one, with `struct_size` set to sizeof(T) so the copy is safe to forward.
template <typename T>
[[nodiscard]] Expected<T> copyPrefix(const T* caller, std::size_t min_size) {
  if (caller == nullptr) {
    return unexpected("null struct");
  }
  if (caller->struct_size < min_size) {
    return unexpected("struct_size is below the v1 minimum");
  }
  T out{};
  std::memcpy(&out, caller, std::min<std::size_t>(caller->struct_size, sizeof(T)));
  out.struct_size = sizeof(T);
  return out;
}

/// A view may be NULL only when empty; a valid one needs no terminator.
[[nodiscard]] constexpr bool validView(PJ_string_view_t view) noexcept {
  return view.data != nullptr || view.size == 0;
}

}  // namespace detail

/// Decode the request a plugin passed to `slot`: the known prefix (see copyPrefix).
/// Its string and array pointers still borrow the plugin's request: use them only
/// during the call. Rejects a null request, a struct_size below the v1 minimum,
/// unknown flags or time_flags bits, WINDOW with INSTANT, a NULL array with a nonzero
/// count, a NULL string with a nonzero size, and a nonzero output `reserved`. On
/// kSubmitEvaluation it also rejects HISTORY_EXEMPT, a request with neither INSTANT nor
/// WINDOW, and, for an installed node (empty script), any recipe field: language,
/// params_json, inputs, outputs or dataset_id. EPHEMERAL is never required.
/// @since 0.37.0
[[nodiscard]] inline Expected<PJ_data_processor_request_t> readDataProcessorRequest(
    const PJ_data_processor_request_t* request, DataProcessorSlot slot) {
  auto decoded = detail::copyPrefix(request, PJ_DATA_PROCESSOR_REQUEST_V1_MIN_SIZE);
  if (!decoded) {
    return unexpected("data processor request: " + decoded.error());
  }
  const PJ_data_processor_request_t& out = *decoded;

  constexpr uint32_t kKnownFlags = PJ_DATA_PROCESSOR_FLAG_EPHEMERAL | PJ_DATA_PROCESSOR_FLAG_HISTORY_EXEMPT;
  constexpr uint32_t kTimeFlags = PJ_DATA_PROCESSOR_TIME_FLAG_WINDOW | PJ_DATA_PROCESSOR_TIME_FLAG_INSTANT;
  if ((out.flags & ~kKnownFlags) != 0) {
    return unexpected("unknown data processor flags bits");
  }
  if ((out.time_flags & ~kTimeFlags) != 0) {
    return unexpected("unknown time_flags bits");
  }
  if (out.time_flags == kTimeFlags) {
    return unexpected("time_flags WINDOW and INSTANT are mutually exclusive");
  }
  for (const PJ_string_view_t view : {out.id, out.kind, out.language, out.script, out.params_json}) {
    if (!detail::validView(view)) {
      return unexpected("a request string is NULL with a nonzero size");
    }
  }
  if (out.input_count > 0 && out.inputs == nullptr) {
    return unexpected("inputs is NULL with input_count > 0");
  }
  if (out.output_count > 0 && out.outputs == nullptr) {
    return unexpected("outputs is NULL with output_count > 0");
  }
  for (uint64_t i = 0; i < out.input_count; ++i) {
    if (!detail::validView(out.inputs[i])) {
      return unexpected("an input name is NULL with a nonzero size");
    }
  }
  for (uint64_t i = 0; i < out.output_count; ++i) {
    const PJ_data_processor_output_t& output = out.outputs[i];
    if (!detail::validView(output.name) || !detail::validView(output.type)) {
      return unexpected("an output name or type is NULL with a nonzero size");
    }
    if (output.reserved[0] != 0 || output.reserved[1] != 0) {
      return unexpected("output reserved fields must be 0");
    }
  }
  if (slot == DataProcessorSlot::kSubmitEvaluation) {
    if ((out.flags & PJ_DATA_PROCESSOR_FLAG_HISTORY_EXEMPT) != 0) {
      return unexpected("submit_evaluation installs no node: HISTORY_EXEMPT is rejected");
    }
    if (out.time_flags == 0) {
      return unexpected("submit_evaluation needs time_flags INSTANT or WINDOW");
    }
    if (out.script.size == 0 && (out.language.size != 0 || out.params_json.size != 0 || out.input_count != 0 ||
                                 out.output_count != 0 || out.dataset_id != 0)) {
      return unexpected(
          "submit_evaluation of an installed node uses its stored recipe: language, params_json, inputs, outputs "
          "and dataset_id must be empty");
    }
  }
  return decoded;
}

/// Decode the budget a plugin passed to submit_evaluation: the known prefix (see
/// copyPrefix). A NULL budget yields every field 0 (= the host default). Rejects a
/// struct_size below the v1 minimum and a nonzero `reserved`.
/// @since 0.37.0
[[nodiscard]] inline Expected<PJ_evaluation_budget_t> readEvaluationBudget(const PJ_evaluation_budget_t* budget) {
  if (budget == nullptr) {
    PJ_evaluation_budget_t defaults{};
    defaults.struct_size = sizeof(defaults);
    return defaults;
  }
  auto decoded = detail::copyPrefix(budget, PJ_EVALUATION_BUDGET_V1_MIN_SIZE);
  if (!decoded) {
    return unexpected("evaluation budget: " + decoded.error());
  }
  if (decoded->reserved != 0) {
    return unexpected("evaluation budget reserved must be 0");
  }
  return decoded;
}

}  // namespace PJ::sdk
