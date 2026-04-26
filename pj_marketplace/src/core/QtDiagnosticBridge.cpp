#include "pj_marketplace/qt_diagnostic_bridge.hpp"

#include <QMetaObject>
#include <QPointer>
#include <Qt>

namespace PJ {

QtDiagnosticBridge::QtDiagnosticBridge(QObject* parent) : QObject(parent) {}

DiagnosticSink QtDiagnosticBridge::sink() {
  QPointer<QtDiagnosticBridge> guard(this);
  return [guard](const Diagnostic& diagnostic) {
    if (!guard) {
      return;
    }
    QMetaObject::invokeMethod(
        guard.data(),
        [guard, diagnostic]() {
          if (!guard) {
            return;
          }
          emit guard->diagnosticReported(
              static_cast<int>(diagnostic.level), QString::fromStdString(diagnostic.source),
              QString::fromStdString(diagnostic.id), QString::fromStdString(diagnostic.message));
        },
        Qt::QueuedConnection);
  };
}

}  // namespace PJ
