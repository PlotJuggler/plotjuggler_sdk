#pragma once

#include <QDateTime>
#include <QString>

namespace PJ {

struct InstalledExtension {
  QString id;  ///< Matches Extension::id from the registry
  QString version;
  QDateTime install_date;
  QString path;  ///< Absolute path to ~/.plotjuggler/extensions/<id>/
  bool enabled = true;
};

}  // namespace PJ
