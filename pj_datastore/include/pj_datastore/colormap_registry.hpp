#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace PJ {

/// Signature of a color evaluation callback. Receives a scalar value and a
/// user-provided context pointer; returns a CSS color name or "#rrggbb" hex
/// string. The returned pointer must remain valid until the next call to the
/// same callback.
using ColorMapEvalFn = const char* (*)(double value, void* user_ctx);

/// Registry of named colormap callbacks.
///
/// Plugins register one or more named maps during the lifetime of their
/// dialog, pick one as active, and consumers (chart renderers, exporters)
/// evaluate the active map per data point.
///
/// The `DatastoreToolboxHost` owns one instance and forwards
/// `register_colormap`/`unregister_colormap` calls received through the C ABI
/// vtable. Consumers read through `DatastoreToolboxHost::colorMaps()`.
class ColorMapRegistry {
 public:
  ColorMapRegistry() = default;
  ~ColorMapRegistry() = default;

  ColorMapRegistry(const ColorMapRegistry&) = delete;
  ColorMapRegistry& operator=(const ColorMapRegistry&) = delete;

  /// Register or replace a named colormap. The newly registered map becomes
  /// active; call `setActive()` afterwards to switch to a different one.
  void registerMap(std::string_view name, ColorMapEvalFn eval_fn, void* user_ctx);

  /// Unregister a colormap by name. If it was active, clears the active
  /// selection — subsequent `evaluate()` calls return an empty string.
  void unregisterMap(std::string_view name);

  /// Set the active colormap by name. No-op if `name` is not registered.
  void setActive(std::string_view name);

  /// Evaluate the active colormap for a scalar value. Returns empty when no
  /// colormap is active.
  [[nodiscard]] std::string evaluate(double value) const;

  /// True when a colormap is active and its callback is available.
  [[nodiscard]] bool hasActive() const;

  /// Name of the currently active colormap, or empty string when none.
  [[nodiscard]] const std::string& activeName() const { return active_; }

 private:
  struct Entry {
    ColorMapEvalFn eval_fn;
    void* user_ctx;
  };
  std::unordered_map<std::string, Entry> maps_;
  std::string active_;
};

}  // namespace PJ
