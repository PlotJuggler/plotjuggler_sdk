#pragma once

#include <QString>

namespace PJ {

// Static helpers for platform detection and standard directory resolution.
//
// All path helpers return absolute paths without a trailing separator.
// Directories are NOT created here — callers are responsible for mkpath.
class PlatformUtils {
 public:
  // Returns the platform identifier used as key in registry artifact maps.
  // Format: "<os>-<arch>", e.g. "linux-x86_64", "windows-x86_64", "macos-arm64".
  static QString currentPlatform();

  static bool isWindows();

  // ~/.plotjuggler/ — root of all PlotJuggler user data.
  static QString configDir();

  // ~/.plotjuggler/extensions/ — active, loaded extensions.
  static QString extensionsDir();

  // ~/.plotjuggler/.pending/ — staging area for extensions awaiting a restart (Windows only).
  static QString pendingDir();

  // ~/.plotjuggler/.backup/ — pre-update backups (F-12, deferred to April+).
  static QString backupDir();
};

}  // namespace PJ
