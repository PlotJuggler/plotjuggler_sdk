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

  bool installations_changed() const { return installations_changed_; }

 protected:
  bool eventFilter(QObject* obj, QEvent* event) override;

 private slots:
  void on_search_changed(const QString& text);
  void on_category_changed(int index);
  void on_refresh_clicked();
  void on_update_all_clicked();
  void on_settings_clicked();
  void on_action_button_clicked(const QString& ext_id);
  void on_uninstall_button_clicked(const QString& ext_id);

 private:
  void setup_ui();
  void setup_signals();
  void populate_cards();
  void apply_filters();
  void set_status(const QString& msg, bool is_error = false);
  void open_detail(const QString& ext_id);
  void process_install_queue();

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
