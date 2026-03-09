#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

#include "models/Extension.h"

namespace PJ {

// Downloads and parses the marketplace registry JSON from a remote URL.
//
// Usage:
//   auto* mgr = new RegistryManager(this);
//   connect(mgr, &RegistryManager::fetchFinished, this, [mgr](bool ok) {
//     if (ok) use(mgr->extensions());
//   });
//   mgr->fetchRegistry(QUrl("https://raw.githubusercontent.com/.../registry.json"));
//
// A new fetchRegistry() call cancels any in-flight request.
class RegistryManager : public QObject {
  Q_OBJECT

 public:
  explicit RegistryManager(QObject* parent = nullptr);

  // Starts an async download of the registry JSON at `url`.
  // Emits fetchStarted() immediately, then fetchFinished() or fetchError() when done.
  void fetchRegistry(const QUrl& url);

  // Returns the parsed extensions after a successful fetch; empty list otherwise.
  QList<Extension> extensions() const;

  // Returns the first extension whose id matches, or a default-constructed Extension
  // (id is empty) when not found.
  Extension findById(const QString& id) const;

 signals:
  void fetchStarted();
  void fetchFinished(bool success);
  void fetchError(const QString& error_message);

 private:
  // Parses raw JSON bytes into m_extensions.
  // Emits fetchError() and returns false on any parse failure.
  bool parseJson(const QByteArray& data);

  QNetworkAccessManager* m_network;
  QNetworkReply* m_pending_reply = nullptr;  // Non-owning; owned by m_network
  QList<Extension> m_extensions;
};

}  // namespace PJ
