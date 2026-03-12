#pragma once

#include <QList>
#include "core/RegistryManager.h"
#include "models/Extension.h"

namespace PJ {

/// Provides a hard-coded extension catalog without any network access.
///
/// fetchRegistry() emits fetchStarted() and fetchFinished(true) synchronously
/// so that MarketplaceWindow is populated immediately on construction, exactly
/// as the inline setup_mock_catalog() did before this refactoring.
class MockRegistryManager : public RegistryManager {
  Q_OBJECT

 public:
  explicit MockRegistryManager(QObject* parent = nullptr);
  ~MockRegistryManager() override = default;

  void fetchRegistry(const QUrl& url) override;
  QList<Extension> extensions() const override;
  Extension findById(const QString& id) const override;

 private:
  QList<Extension> mock_extensions_;
};

}  // namespace PJ
