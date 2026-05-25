#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Vocabulary types for cross-module diagnostic propagation.
//
// Non-GUI code that wants to surface a problem (a plugin failed to load, a
// manifest is malformed, an external store is unreachable) accepts a
// DiagnosticSink in its constructor or as a parameter and emits Diagnostic
// values through it. A GUI host installs a sink that bridges the events into
// its own event loop (e.g. a Qt signal) and surfaces them to the user.
//
// The sink is type-erased via std::function so callers don't depend on Qt and
// implementations can be lambdas, free functions, or member-function bindings.
// A default-constructed sink is falsy and discards every event — code that
// emits should null-check (`if (sink_) sink_(...)`) or use the convenience
// emit helper. This keeps the no-listener path zero-cost.

#include <chrono>
#include <functional>
#include <string>
#include <utility>

namespace PJ {

enum class DiagnosticLevel { kInfo, kWarning, kError };

struct Diagnostic {
  DiagnosticLevel level = DiagnosticLevel::kInfo;
  std::string source;  ///< Component that produced the event, e.g. "PluginRegistry".
  std::string id;      ///< Optional plugin/extension id; empty if not applicable.
  std::string message;
  std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
};

using DiagnosticSink = std::function<void(const Diagnostic&)>;

/// Forwards events to both `a` and `b`. Either may be empty.
inline DiagnosticSink teeSink(DiagnosticSink a, DiagnosticSink b) {
  return [a = std::move(a), b = std::move(b)](const Diagnostic& d) {
    if (a) {
      a(d);
    }
    if (b) {
      b(d);
    }
  };
}

}  // namespace PJ
