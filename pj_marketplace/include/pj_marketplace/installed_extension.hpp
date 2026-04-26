#pragma once

#include <QDateTime>
#include <QString>

namespace PJ {

// Installed extension discovered from an embedded plugin manifest on disk.
struct InstalledExtension {
  QString id;  ///< Matches Extension::id from the registry
  QString version;
  QDateTime install_date;
  QString path;  ///< Absolute path to ~/.plotjuggler/extensions/<id>/
  bool enabled = true;
};

}  // namespace PJ
