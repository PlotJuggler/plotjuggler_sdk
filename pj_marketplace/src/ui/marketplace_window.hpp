#pragma once

#include <QDialog>
#include <QSet>
#include <QUrl>
#include "models/Extension.h"

namespace Ui { class MarketplaceWindow; }

namespace PJ {

class ExtensionManager;
class RegistryManager;

class MarketplaceWindow : public QDialog {
  Q_OBJECT

 public:
  explicit MarketplaceWindow(RegistryManager* registry_mgr, ExtensionManager* ext_mgr,
                             const QUrl& registry_url, QWidget* parent = nullptr);
  ~MarketplaceWindow() override;

  bool installationsChanged() const { return installations_changed_; }

 protected:
  bool eventFilter(QObject* obj, QEvent* event) override;

 private slots:
  void onSearchChanged(const QString& text);
  void onCategoryChanged(int index);
  void onRefreshClicked();
  void onUpdateAllClicked();
  void onSettingsClicked();
  void onActionButtonClicked(const QString& ext_id);
  void onUninstallButtonClicked(const QString& ext_id);

 private:
  void setupUi();
  void setupSignals();
  void populateCards();
  void applyFilters();
  void setStatus(const QString& msg, bool is_error = false);
  void openDetail(const QString& ext_id);
  void processInstallQueue();

  Ui::MarketplaceWindow* ui_           = nullptr;
  RegistryManager*       registry_mgr_ = nullptr;
  ExtensionManager*      ext_mgr_      = nullptr;
  QUrl                   registry_url_;

  QList<Extension> extensions_;  // populated from RegistryManager::fetchFinished
  QList<Extension> filtered_;
  QList<Extension> update_queue_;
  QSet<QString>    pending_restart_ids_;
  bool             installations_changed_ = false;
};

}  // namespace PJ
