#pragma once
#include <memory>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/column_buffer.hpp"

namespace PJ {

class DataEngine;

/// Implementation struct — defined in derived_engine.cpp, hidden from callers.
struct DerivedEngineImpl;

// ---------------------------------------------------------------------------
// VarValue — universal column value type for transform I/O
// ---------------------------------------------------------------------------
// Engine storage kinds map as follows:
//   kFloat32, kFloat64        → double   (float32 widens losslessly)
//   kInt8 … kInt64, kBool     → int64_t  (sign-extend; bool → 0/1)
//   kUint64                   → uint64_t (lossless)
//   kString                   → std::string
using VarValue = std::variant<int64_t, uint64_t, double, std::string>;

// ---------------------------------------------------------------------------
// ISISOTransform — single-input / single-output transform
// ---------------------------------------------------------------------------
// SEQUENTIAL CONTRACT (fundamental):
//   The engine calls calculate() once per sample, strictly in ascending
//   timestamp order. Implementations may therefore accumulate state freely
//   in member variables between calls (e.g. previous value for derivative,
//   ring buffer for moving average, running sum for integral).
//   State persists across chunk boundaries — the engine never resets it
//   during incremental scheduling.
//   reset() is the only path that clears state; the engine calls it
//   exclusively before a full batch recompute.
class ISISOTransform {
 public:
  virtual ~ISISOTransform() = default;

  /// Clear all accumulated state. Called by DerivedEngine before batch recompute.
  /// After reset(), the next calculate() call must behave as if no data has
  /// been seen (same as a freshly constructed instance).
  virtual void reset() {}

  /// Declare the StorageKind of the output column. Called once at registration.
  /// Default: kFloat64 (suitable for most numeric filters).
  /// Override to preserve integer types or produce strings.
  virtual StorageKind outputKind(StorageKind input_kind) const {
    (void)input_kind;
    return StorageKind::kFloat64;
  }

  /// Process one sample. Called in strictly ascending timestamp order.
  ///   time:      sample timestamp (nanoseconds since epoch)
  ///   input:     sample value decoded as VarValue
  ///   out_time:  output timestamp (written by callee; read by engine only when true)
  ///   out_value: output value   (written by callee; read by engine only when true)
  ///
  /// Returns true to emit a row, false to suppress (e.g. first row of derivative).
  ///
  /// out_time MAY differ from `time` — time-offset transforms and interpolation
  /// may produce output on a different time grid than their input.
  /// When true is returned, out_time must be >= all previously returned out_times.
  virtual bool calculate(PJ::Timestamp time, const VarValue& input, PJ::Timestamp& out_time, VarValue& out_value) = 0;
};

// ---------------------------------------------------------------------------
// IMIMOTransform — multi-input / multi-output transform
// ---------------------------------------------------------------------------
// SEQUENTIAL CONTRACT (fundamental, same as ISISOTransform):
//   The engine calls calculate() once per joined sample, strictly in ascending
//   timestamp order. State may be accumulated in member variables between calls.
//   reset() clears all state; called exclusively before batch recompute.
class IMIMOTransform {
 public:
  virtual ~IMIMOTransform() = default;

  /// Clear all accumulated state. Called by DerivedEngine before batch recompute.
  virtual void reset() {}

  /// Declare output StorageKind for each output topic.
  /// Called once at registration with the input kinds (one per input topic).
  /// Return one StorageKind per output topic name passed to add_mimo_transform.
  virtual std::vector<StorageKind> outputKinds(PJ::Span<const StorageKind> input_kinds) const = 0;

  /// Process one joined sample. Called in strictly ascending timestamp order,
  /// only when ALL input topics have a sample at exactly `time`.
  ///   inputs[i]  = value from input topic i (in add_mimo_transform order).
  ///   out_time   = output timestamp (written by callee; read only when true).
  ///   output     = pre-allocated buffer (size == num output topics); fill in-place.
  ///                output[k] corresponds to output_topic_names[k] from add_mimo_transform.
  ///
  /// Returns true to emit a row; false to suppress.
  /// out_time MAY differ from `time`. When true is returned, out_time must be
  /// >= all previously returned out_times. All M output topics share this timestamp.
  virtual bool calculate(
      PJ::Timestamp time, PJ::Span<const VarValue> inputs, PJ::Timestamp& out_time, std::vector<VarValue>& output) = 0;
};

// ---------------------------------------------------------------------------
// DerivedEngine
// ---------------------------------------------------------------------------
class DerivedEngine {
 public:
  explicit DerivedEngine(DataEngine& engine);
  ~DerivedEngine();
  DerivedEngine(const DerivedEngine&) = delete;
  DerivedEngine& operator=(const DerivedEngine&) = delete;

  // ---- SISO ----------------------------------------------------------------
  // Creates one scalar output topic (StorageKind from op->outputKind()).
  // Returns error if:
  //   - input_topic_id does not exist
  //   - input topic has more than one column
  //   - output_topic_name already registered within output_dataset_id
  //
  // Topics created via DataWriter::registerScalarSeries (schema_id == 0)
  // are supported even before any data has been committed: the column layout
  // is stored in TopicStorage at registration time. Topics created via
  // DataWriter::register_topic with schema_id != 0 are always supported.
  // Returns error if the column layout cannot be determined (e.g. a topic
  // created with schema_id==0 via the low-level register_topic API with no
  // committed chunks and no stored column descriptors).
  [[nodiscard]] PJ::Expected<PJ::NodeId> addSisoTransform(
      PJ::TopicId input_topic_id, std::string output_topic_name, PJ::DatasetId output_dataset_id,
      std::unique_ptr<ISISOTransform> op);

  // ---- MIMO -----------------------------------------------------------------
  // All input topics must be single-column (scalar).
  // A row is emitted only when ALL input topics share the exact same timestamp.
  // Creates output_topic_names.size() new topics (kinds from op->outputKinds()).
  [[nodiscard]] PJ::Expected<PJ::NodeId> addMimoTransform(
      std::vector<PJ::TopicId> input_topic_ids, std::vector<std::string> output_topic_names,
      PJ::DatasetId output_dataset_id, std::unique_ptr<IMIMOTransform> op);

  // ---- Node management -----------------------------------------------------
  PJ::Status removeNode(PJ::NodeId id);
  [[nodiscard]] bool hasNode(PJ::NodeId id) const noexcept;

  // Returns output topic IDs: 1 for SISO, M for MIMO.
  [[nodiscard]] std::vector<PJ::TopicId> outputTopics(PJ::NodeId id) const;

  // Kahn's topological order (upstream → downstream).
  [[nodiscard]] std::vector<PJ::NodeId> topologicalOrder() const;

  // ---- Commit-cycle hook ---------------------------------------------------
  // Call after DataEngine::commitChunks() with the set of changed topic IDs.
  // Marks directly dependent nodes dirty.
  void onSourceCommitted(PJ::Span<const PJ::TopicId> changed_topics);

  // ---- Scheduling ----------------------------------------------------------
  // Run all dirty nodes in topological order (incremental path).
  // Use for file/batch playback and tests. Equivalent to passing every
  // registered node to scheduleActive().
  PJ::Status scheduleAll();

  // Run only the specified nodes (and their transitive upstream dependencies)
  // that are dirty. Pass the set of nodes whose output topics are currently
  // visible in the UI to implement display-lazy scheduling.
  PJ::Status scheduleActive(const std::unordered_set<PJ::NodeId>& active_nodes);

  // Full history recompute: clear output, reset transform, replay all input.
  PJ::Status recompute_batch(PJ::NodeId node_id);

 private:
  DataEngine& engine_;
  PJ::NodeId next_node_id_ = 1;
  std::unique_ptr<DerivedEngineImpl> impl_;
};

}  // namespace PJ
