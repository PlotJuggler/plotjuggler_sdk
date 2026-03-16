#include "core/PlatformUtils.h"

#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

namespace PJ {

QString PlatformUtils::currentPlatform() {
  // QSysInfo::kernelType() returns "linux", "winnt", "darwin", etc.
  // Normalise to the friendlier names used in registry artifact keys.
  const QString kernel = QSysInfo::kernelType();
  const QString arch = QSysInfo::currentCpuArchitecture();

  QString os;
  if (kernel == "linux") {
    os = "linux";
  } else if (kernel == "winnt") {
    os = "windows";
  } else if (kernel == "darwin") {
    os = "macos";
  } else {
    os = kernel;
  }

  return os + "-" + arch;
}

bool PlatformUtils::isWindows() {
#ifdef Q_OS_WIN
  return true;
#else
  return false;
#endif
}

QString PlatformUtils::configDir() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/plotjuggler";
}

QString PlatformUtils::extensionsDir() {
  return configDir() + "/extensions";
}

QString PlatformUtils::pendingDir() {
  return configDir() + "/.pending";
}

QString PlatformUtils::backupDir() {
  return configDir() + "/.backup";
}

}  // namespace PJ
