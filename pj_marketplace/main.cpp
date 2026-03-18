#include <QApplication>
#include <QUrl>
#include "pj_marketplace/marketplace_window.hpp"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  const QUrl registry_url = QUrl("https://raw.githubusercontent.com/PlotJuggler/pj-plugin-registry/refs/heads/development/registry.json");
  PJ::MarketplaceWindow w(registry_url);
  w.resize(700, 500);
  w.show();
  return app.exec();
}
