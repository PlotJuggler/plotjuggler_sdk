#pragma once
#include <QDialog>
#include "models/Extension.h"

namespace Ui { class ExtensionDetailDialog; }

namespace PJ {

class ExtensionDetailDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ExtensionDetailDialog(const Extension& ext, const QString& installed_version,
                                 QWidget* parent = nullptr);
  ~ExtensionDetailDialog() override;

 signals:
  void install_requested();
  void uninstall_requested();

 private:
  Ui::ExtensionDetailDialog* ui_ = nullptr;
};

}  // namespace PJ
