#pragma once

// QtDiagnosticBridge adapts the Qt-free PJ::DiagnosticSink vocabulary to a
// queued Qt signal. Hosts can pass sink() to non-GUI components and connect the
// signal to their UI without duplicating thread-marshalling code.

#include <QObject>
#include <QString>

#include "pj_base/diagnostic_sink.hpp"

namespace PJ {

class QtDiagnosticBridge : public QObject {
  Q_OBJECT

 public:
  // Creates a bridge that emits diagnostics on its QObject thread.
  explicit QtDiagnosticBridge(QObject* parent = nullptr);

  // Safe to call from any thread. The returned sink may outlive this object:
  // queued delivery is guarded by QPointer.
  DiagnosticSink sink();

 signals:
  // Emitted on the bridge object's thread for each received diagnostic.
  void diagnosticReported(int level, QString source, QString id, QString message);
};

}  // namespace PJ
