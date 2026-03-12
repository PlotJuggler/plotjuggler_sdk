#include <QApplication>
#include <QUrl>
#include "core/DownloadManager.h"
#include "core/ExtensionManager.h"
#include "core/RegistryManager.h"
#include "ui/marketplace_window.hpp"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  const QUrl registry_url = QUrl::fromLocalFile(
      QStringLiteral(REGISTRY_JSON_PATH));
  auto* registry   = new PJ::RegistryManager;
  auto* downloader = new PJ::DownloadManager;
  auto* ext_mgr    = new PJ::ExtensionManager(downloader);
  PJ::MarketplaceWindow w(registry, ext_mgr, registry_url);
  w.resize(700, 500);
  w.show();
  return app.exec();
}
