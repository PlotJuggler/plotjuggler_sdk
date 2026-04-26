#pragma once

#include <QString>
#include <string>

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

  // Returns true on Windows builds.
  static bool isWindows();

  // Returns the shared library extension for the current platform:
  //   Linux:   ".so"
  //   Windows: ".dll"
  //   macOS:   ".dylib"
  static std::string pluginExtension();

  // Root of all PlotJuggler user data, using the OS-standard writable location:
  //   Linux:   ~/.local/share/plotjuggler/
  //   Windows: AppData/Local/plotjuggler/
  //   macOS:   ~/Library/Application Support/plotjuggler/
  static QString configDir();

  // ~/.plotjuggler/extensions/ — active, loaded extensions.
  static QString extensionsDir();

  // <config-root>/.extension_staging/ — restart staging for Windows updates.
  static QString pendingDir();

  // ~/.plotjuggler/.backup/ — pre-update backups (F-12, deferred to April+).
  static QString backupDir();
};

}  // namespace PJ
