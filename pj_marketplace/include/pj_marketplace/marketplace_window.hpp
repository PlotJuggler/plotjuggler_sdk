#pragma once

#include <QMap>
#include <QDialog>
#include <QUrl>

#include "pj_marketplace/extension.hpp"
#include "pj_marketplace/installed_extension.hpp"

namespace Ui {
class MarketplaceWindow;
}

namespace PJ {

class DownloadManager;
class ExtensionManager;
class RegistryManager;

// Marketplace dialog that renders registry extensions and local install state.
class MarketplaceWindow : public QDialog {
  Q_OBJECT

 public:
  explicit MarketplaceWindow(const QUrl& registry_url, QWidget* parent = nullptr);

  // Uses an externally owned ExtensionManager, mainly for tests and embedding.
  explicit MarketplaceWindow(ExtensionManager* ext_mgr, const QUrl& registry_url, QWidget* parent = nullptr);

  // Uses an externally owned ExtensionManager and a caller-provided installed snapshot.
  explicit MarketplaceWindow(
      ExtensionManager* ext_mgr, const QUrl& registry_url, const QMap<QString, InstalledExtension>& installed,
      QWidget* parent = nullptr);

  ~MarketplaceWindow() override;

  // Returns true when install state changed while the dialog was open.
  bool installationsChanged() const {
    return installations_changed_;
  }

 protected:
  // Handles card hover styling and delegated button events.
  bool eventFilter(QObject* obj, QEvent* event) override;

  // Refreshes installed state before cards are painted.
  void showEvent(QShowEvent* event) override;

 private slots:
  // Updates the search filter.
  void onSearchChanged(const QString& text);

  // Updates the category filter.
  void onCategoryChanged(int index);

  // Refetches registry data and refreshes installed state.
  void onRefreshClicked();

  // Queues updates for every installed extension with a newer registry version.
  void onUpdateAllClicked();

  // Opens the registry URL settings dialog.
  void onSettingsClicked();

  // Opens a read-only view of recent marketplace diagnostics.
  void onDiagnosticsClicked();

  // Runs the primary install/update action for one extension card.
  void onActionButtonClicked(const QString& ext_id);

  // Confirms and uninstalls one installed extension.
  void onUninstallButtonClicked(const QString& ext_id);

 private:
  // Creates widgets from the .ui file and configures fixed UI affordances.
  void setupUi();

  // Connects registry, extension-manager, and widget signals.
  void setupSignals();

  // Rebuilds extension cards from the current filtered list.
  void populateCards();

  // Applies search and category filters to the registry list.
  void applyFilters();

  // Updates the status label; error statuses remain sticky until a user action clears them.
  void setStatus(const QString& msg, bool is_error = false);

  // Allows the next non-error status update to replace an error.
  void clearStickyStatus();

  // Shows the newest diagnostic in the status bar, if one exists.
  void showLatestDiagnostic();

  // Shows or hides the diagnostics button based on diagnostic history.
  void updateDiagnosticsButton();

  // Opens the detail dialog for one registry extension.
  void openDetail(const QString& ext_id);

  // Processes one pending bulk-update item at a time.
  void processInstallQueue();

  Ui::MarketplaceWindow* ui_ = nullptr;
  DownloadManager* download_mgr_ = nullptr;
  RegistryManager* registry_mgr_ = nullptr;
  ExtensionManager* ext_mgr_ = nullptr;
  QUrl registry_url_;

  QList<Extension> extensions_;  // populated from RegistryManager::fetchFinished
  QList<Extension> filtered_;
  QList<Extension> update_queue_;
  bool installations_changed_ = false;
  bool status_error_sticky_ = false;
  bool initial_snapshot_provided_ = false;
};

}  // namespace PJ
