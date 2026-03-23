#pragma once
#include <QDialog>
#include "pj_marketplace/extension.hpp"

namespace Ui { class ExtensionDetailDialog; }

namespace PJ {

class ExtensionDetailDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ExtensionDetailDialog(const Extension& ext, const QString& installed_version,
                                 QWidget* parent = nullptr);
  ~ExtensionDetailDialog() override;

 signals:
  void installRequested();
  void uninstallRequested();

 private:
  Ui::ExtensionDetailDialog* ui_ = nullptr;
};

}  // namespace PJ
