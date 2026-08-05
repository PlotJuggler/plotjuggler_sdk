#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PJ {

/// Operation requested from a structured file picker. The canonical JSON wire
/// strings are "open_file", "open_files", "save_file", and
/// "select_directory", respectively. No other spelling is valid.
/// @since 0.21.0
enum class FilePickerMode { OpenFile, OpenFiles, SaveFile, SelectDirectory };

/// Return the canonical JSON wire spelling for a file-picker mode. An invalid
/// enum value produces an empty string, which strict readers reject.
/// @since 0.21.0
[[nodiscard]] inline std::string_view filePickerModeWireValue(FilePickerMode mode) noexcept {
  switch (mode) {
    case FilePickerMode::OpenFile:
      return "open_file";
    case FilePickerMode::OpenFiles:
      return "open_files";
    case FilePickerMode::SaveFile:
      return "save_file";
    case FilePickerMode::SelectDirectory:
      return "select_directory";
  }
  return "";
}

/// Decode only the four canonical file-picker mode wire strings.
/// @since 0.21.0
[[nodiscard]] inline std::optional<FilePickerMode> filePickerModeFromWireValue(std::string_view value) noexcept {
  if (value == "open_file") {
    return FilePickerMode::OpenFile;
  }
  if (value == "open_files") {
    return FilePickerMode::OpenFiles;
  }
  if (value == "save_file") {
    return FilePickerMode::SaveFile;
  }
  if (value == "select_directory") {
    return FilePickerMode::SelectDirectory;
  }
  return std::nullopt;
}

/// One host-rendered file-type filter. `id` is a stable plugin-owned identity;
/// unlike `label`, it is never localized or normalized by a native picker.
/// Every filter requires a non-empty unique ID and at least one non-empty
/// pattern.
/// @since 0.21.0
struct FilePickerFilter {
  std::string id;
  std::string label;
  std::vector<std::string> patterns;
};

/// Complete structured file-picker request.
/// @since 0.21.0
struct FilePickerOptions {
  FilePickerMode mode = FilePickerMode::OpenFile;
  std::string title;
  std::string accept_label;
  std::string initial_directory;
  std::string suggested_name;
  std::string default_suffix;
  std::vector<FilePickerFilter> filters;
  std::string initially_selected_filter_id;
  bool confirm_overwrite = true;
};

/// Outcome of a structured picker. The canonical JSON wire strings are
/// "selected", "cancelled", "unsupported", and "error", respectively. No
/// other spelling is valid.
/// @since 0.21.0
enum class FilePickerStatus { Selected, Cancelled, Unsupported, Error };

/// Return the canonical JSON wire spelling for a file-picker status. An
/// invalid enum value produces an empty string, which strict readers reject.
/// @since 0.21.0
[[nodiscard]] inline std::string_view filePickerStatusWireValue(FilePickerStatus status) noexcept {
  switch (status) {
    case FilePickerStatus::Selected:
      return "selected";
    case FilePickerStatus::Cancelled:
      return "cancelled";
    case FilePickerStatus::Unsupported:
      return "unsupported";
    case FilePickerStatus::Error:
      return "error";
  }
  return "";
}

/// Decode only the four canonical file-picker status wire strings.
/// @since 0.21.0
[[nodiscard]] inline std::optional<FilePickerStatus> filePickerStatusFromWireValue(std::string_view value) noexcept {
  if (value == "selected") {
    return FilePickerStatus::Selected;
  }
  if (value == "cancelled") {
    return FilePickerStatus::Cancelled;
  }
  if (value == "unsupported") {
    return FilePickerStatus::Unsupported;
  }
  if (value == "error") {
    return FilePickerStatus::Error;
  }
  return std::nullopt;
}

/// Complete structured file-picker outcome. `display_names` may preserve
/// browser-visible names when `paths` contains synthetic staged paths.
/// @since 0.21.0
struct FilePickerResult {
  FilePickerStatus status = FilePickerStatus::Cancelled;
  std::vector<std::string> paths;
  std::vector<std::string> display_names;
  std::string selected_filter_id;
  std::string error;
};

}  // namespace PJ
