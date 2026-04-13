#pragma once

#include <QDialog>
#include <QString>

namespace proto {

class PreferencesDialog : public QDialog {
  Q_OBJECT

 public:
  explicit PreferencesDialog(const QString& builtin_plugin_dir, QWidget* parent = nullptr);
};

}  // namespace proto
