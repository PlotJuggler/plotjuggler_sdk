#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace PJ {

struct Platform {
  QString url;
  QString checksum;  ///< Format: "sha256:<hex>"
};

struct ExtensionPlugin {
  QString name;     ///< Plugin class name
  QString type;     ///< "data_loader" | "data_streamer" | "parser" | "toolbox"
  QString library;  ///< Library filename without extension
};

struct Extension {
  QString id;
  QString name;
  QString description;
  QString author;
  QString publisher;
  QString website;
  QString repository;
  QString license;   ///< SPDX identifier
  QString icon_url;  ///< Optional

  /// "data_loader" | "data_streamer" | "parser" | "toolbox" | "bundle"
  QString category;
  QStringList tags;

  QString version;
  QString min_plotjuggler_version;

  QList<ExtensionPlugin> plugins;
  QMap<QString, Platform> platforms;  ///< Keyed by "linux-x86_64", "windows-x86_64", etc.
  QMap<QString, QString> changelog;   ///< version -> description
};

}  // namespace PJ
