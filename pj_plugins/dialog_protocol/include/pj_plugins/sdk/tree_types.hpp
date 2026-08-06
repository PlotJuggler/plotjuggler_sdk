#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/types.hpp"

namespace PJ {

/// Check state for column 0 of a QTreeWidget item.
///
/// The canonical wire strings are, respectively, "none", "unchecked",
/// "partially_checked", and "checked". No other spelling is valid.
/// @since 0.21.0
enum class TreeCheckState { None, Unchecked, PartiallyChecked, Checked };

/// Return the canonical JSON wire spelling for a tree check state.
/// @since 0.21.0
[[nodiscard]] inline std::string_view treeCheckStateWireValue(TreeCheckState state) noexcept {
  switch (state) {
    case TreeCheckState::None:
      return "none";
    case TreeCheckState::Unchecked:
      return "unchecked";
    case TreeCheckState::PartiallyChecked:
      return "partially_checked";
    case TreeCheckState::Checked:
      return "checked";
  }
  return "none";
}

/// Decode only the four canonical tree check-state wire strings.
/// @since 0.21.0
[[nodiscard]] inline std::optional<TreeCheckState> treeCheckStateFromWireValue(std::string_view value) noexcept {
  if (value == "none") {
    return TreeCheckState::None;
  }
  if (value == "unchecked") {
    return TreeCheckState::Unchecked;
  }
  if (value == "partially_checked") {
    return TreeCheckState::PartiallyChecked;
  }
  if (value == "checked") {
    return TreeCheckState::Checked;
  }
  return std::nullopt;
}

/// One display column of a TreeItem. `sort_value`, when present, is the
/// ordering truth; `text` remains the rendered representation. `icon` is a
/// host semantic icon name, never a path or serialized GUI object.
/// @since 0.21.0
struct TreeCell {
  std::string text;
  std::optional<NumericValue> sort_value;
  std::string tooltip;
  std::string icon;
};

/// One node in a flat, keyed tree snapshot. An empty parent_id denotes a
/// top-level item. IDs are stable plugin-owned identities, independent of
/// labels, row numbers, or paths. In v1 check_state applies to column 0 only.
/// @since 0.21.0
struct TreeItem {
  std::string id;
  std::string parent_id;
  std::vector<TreeCell> cells;
  bool enabled = true;
  bool selectable = true;
  TreeCheckState check_state = TreeCheckState::None;
  bool may_have_children = false;
};

}  // namespace PJ
