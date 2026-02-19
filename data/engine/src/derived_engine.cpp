#include "pj/engine/derived_engine.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <queue>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "pj/base/type_tree.hpp"
#include "pj/engine/engine.hpp"
#include "pj/engine/query.hpp"
#include "pj/engine/topic_storage.hpp"
#include "pj/engine/writer.hpp"

namespace pj::engine {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Walk a TypeTreeNode DFS to find the first primitive leaf's PrimitiveType.
static std::optional<pj::PrimitiveType> find_first_leaf(const pj::TypeTreeNode& node) {
  switch (node.kind) {
    case pj::TypeKind::kPrimitive:
      return node.primitive_type;
    case pj::TypeKind::kEnum:
      return node.primitive_type;  // set by make_enum via primitive_type field
    case pj::TypeKind::kStruct:
      for (const auto& child : node.children) {
        if (auto r = find_first_leaf(*child)) {
          return r;
        }
      }
      return std::nullopt;
    case pj::TypeKind::kArray:
      if (node.element_type) {
        return find_first_leaf(*node.element_type);
      }
      return std::nullopt;
  }
  return std::nullopt;
}

static pj::PrimitiveType storage_kind_to_primitive(StorageKind k) {
  switch (k) {
    case StorageKind::kFloat32:
      return pj::PrimitiveType::kFloat32;
    case StorageKind::kFloat64:
      return pj::PrimitiveType::kFloat64;
    case StorageKind::kInt32:
      return pj::PrimitiveType::kInt32;
    case StorageKind::kInt64:
      return pj::PrimitiveType::kInt64;
    case StorageKind::kUint64:
      return pj::PrimitiveType::kUint64;
    case StorageKind::kBool:
      return pj::PrimitiveType::kBool;
    case StorageKind::kString:
      return pj::PrimitiveType::kString;
  }
  return pj::PrimitiveType::kFloat64;
}

// Decode one row of a chunk column into a VarValue, based on the column's StorageKind.
static VarValue decode_as_varvalue(const TopicChunk& chunk, std::size_t col, std::size_t row, StorageKind kind) {
  switch (kind) {
    case StorageKind::kFloat32:
    case StorageKind::kFloat64:
      return chunk.read_numeric_as_double(col, row);
    case StorageKind::kInt32:
    case StorageKind::kInt64:
      return static_cast<int64_t>(chunk.read_numeric_as_double(col, row));
    case StorageKind::kUint64:
      return static_cast<int64_t>(chunk.read_numeric_as_double(col, row));
    case StorageKind::kBool:
      return static_cast<int64_t>(chunk.read_bool(col, row) ? 1 : 0);
    case StorageKind::kString:
      return std::string(chunk.read_string(col, row));
  }
  return 0.0;
}

// Write a VarValue to a DataWriter row at (topic, col), coercing to out_kind.
static void write_varvalue(
    DataWriter& writer, pj::TopicId tid, std::size_t col, const VarValue& val, StorageKind out_kind) {
  if (out_kind == StorageKind::kString) {
    if (const auto* s = std::get_if<std::string>(&val)) {
      writer.set_string(tid, col, *s);
    }
    return;
  }

  // Numeric: extract as double then coerce to the target type.
  double dval = std::visit(
      [](const auto& v) -> double {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return 0.0;
        } else {
          return static_cast<double>(v);
        }
      },
      val);

  switch (out_kind) {
    case StorageKind::kFloat32:
      writer.set_float32(tid, col, static_cast<float>(dval));
      break;
    case StorageKind::kFloat64:
      writer.set_float64(tid, col, dval);
      break;
    case StorageKind::kInt32:
      writer.set_int32(tid, col, static_cast<int32_t>(dval));
      break;
    case StorageKind::kInt64:
      writer.set_int64(tid, col, static_cast<int64_t>(dval));
      break;
    case StorageKind::kUint64:
      writer.set_uint64(tid, col, static_cast<uint64_t>(dval));
      break;
    case StorageKind::kBool:
      writer.set_bool(tid, col, dval != 0.0);
      break;
    case StorageKind::kString:
      break;  // handled above
  }
}

// ---------------------------------------------------------------------------
// Internal data structures (hidden in .cpp — not exposed in header)
// ---------------------------------------------------------------------------

struct DerivedNode {
  pj::NodeId id = pj::kInvalidNodeId;
  bool is_mimo = false;

  // SISO fields
  pj::TopicId siso_input_topic_id = 0;
  StorageKind siso_input_kind = StorageKind::kFloat64;
  StorageKind siso_output_kind = StorageKind::kFloat64;
  std::unique_ptr<ISISOTransform> siso_op;

  // MIMO fields (flat list; no primary/secondary distinction)
  std::vector<pj::TopicId> mimo_input_topic_ids;
  std::vector<StorageKind> mimo_input_kinds;
  std::vector<StorageKind> mimo_output_kinds;
  std::unique_ptr<IMIMOTransform> mimo_op;

  // Common
  std::vector<pj::TopicId> all_input_topic_ids;  // unified input list for all types
  std::vector<pj::TopicId> output_topic_ids;     // 1 for SISO, M for MIMO
  bool dirty = true;
  pj::ChunkId last_processed_chunk_id = 0;                              // SISO: chunk watermark
  pj::Timestamp mimo_last_ts = std::numeric_limits<pj::Timestamp>::min();  // MIMO: timestamp watermark

  // Reusable decode buffers (avoid per-row allocation)
  VarValue in_val_buf = 0.0;           // SISO input
  VarValue out_val_buf = 0.0;          // SISO output
  std::vector<VarValue> mimo_in_buf;   // MIMO inputs
  std::vector<VarValue> mimo_out_buf;  // MIMO outputs
};

struct DerivedEngineImpl {
  absl::flat_hash_map<pj::NodeId, DerivedNode> nodes;

  // downstream_of[N] = list of nodes whose inputs include an output of N
  absl::flat_hash_map<pj::NodeId, std::vector<pj::NodeId>> downstream_of;

  // topic_to_nodes[T] = list of nodes that use T as an input
  absl::flat_hash_map<pj::TopicId, std::vector<pj::NodeId>> topic_to_nodes;

  // output_topic_to_node[T] = node that produces T (for cycle detection)
  absl::flat_hash_map<pj::TopicId, pj::NodeId> output_topic_to_node;

  // Name uniqueness within dataset: (dataset_id, topic_name) → topic_id
  absl::flat_hash_map<std::pair<pj::DatasetId, std::string>, pj::TopicId> registered_output_names;
};

// ---------------------------------------------------------------------------
// DerivedEngine — constructor / destructor
// ---------------------------------------------------------------------------

DerivedEngine::DerivedEngine(DataEngine& engine) : engine_(engine), impl_(std::make_unique<DerivedEngineImpl>()) {}

DerivedEngine::~DerivedEngine() = default;

// ---------------------------------------------------------------------------
// Cycle detection (DFS)
// ---------------------------------------------------------------------------
// Returns an error string if adding a node with `input_topics → output_topics`
// would create a cycle. Otherwise returns empty string.
static std::string check_cycle(
    const DerivedEngineImpl& impl, const std::vector<pj::TopicId>& input_topics,
    const std::vector<pj::TopicId>& output_topics) {
  absl::flat_hash_set<pj::TopicId> outputs(output_topics.begin(), output_topics.end());

  // DFS from each input: follow upstream edges (output → producing node → its inputs).
  // If we ever reach a topic in `outputs`, we have a cycle.
  std::vector<pj::TopicId> stack(input_topics.begin(), input_topics.end());
  absl::flat_hash_set<pj::TopicId> visited;

  while (!stack.empty()) {
    pj::TopicId t = stack.back();
    stack.pop_back();
    if (!visited.insert(t).second) {
      continue;
    }

    if (outputs.contains(t)) {
      return absl::StrCat("cycle detected: topic ", t, " is both an input and an output");
    }

    auto it = impl.output_topic_to_node.find(t);
    if (it == impl.output_topic_to_node.end()) {
      continue;  // source topic, no upstream
    }

    pj::NodeId producer = it->second;
    auto nit = impl.nodes.find(producer);
    if (nit == impl.nodes.end()) {
      continue;
    }

    for (pj::TopicId in : nit->second.all_input_topic_ids) {
      if (!visited.contains(in)) {
        stack.push_back(in);
      }
    }
  }
  return "";  // no cycle
}

// ---------------------------------------------------------------------------
// add_siso_transform
// ---------------------------------------------------------------------------

pj::Expected<pj::NodeId> DerivedEngine::add_siso_transform(
    pj::TopicId input_topic_id, std::string output_topic_name, pj::DatasetId output_dataset_id,
    std::unique_ptr<ISISOTransform> op) {
  // 1. Check input topic exists
  const TopicStorage* in_storage = engine_.get_topic_storage(input_topic_id);
  if (!in_storage) {
    return pj::unexpected(absl::StrCat("add_siso_transform: input topic ", input_topic_id, " not found"));
  }

  // 2. Determine the single leaf column's StorageKind.
  // Prefer TypeRegistry (via schema_id). Fall back to the first sealed chunk's
  // column_descriptors when schema_id == 0 (e.g. topics created via
  // register_scalar_series, which stores schema only in the writer's internal state).
  pj::SchemaId schema_id = in_storage->descriptor().schema_id;

  std::size_t num_cols = 0;
  std::optional<pj::PrimitiveType> leaf_primitive;

  if (schema_id != 0) {
    const pj::TypeTreeNode* root = engine_.type_registry().lookup(schema_id);
    if (root) {
      num_cols = pj::count_leaf_fields(*root);
      leaf_primitive = find_first_leaf(*root);
    }
  }

  if (num_cols == 0) {
    // Fall back 1: inline column layout stored in TopicStorage at registration time.
    // This covers schema_id==0 topics (register_scalar_series) with no committed chunks yet.
    const auto& stored = in_storage->column_descriptors();
    if (!stored.empty()) {
      num_cols = stored.size();
      leaf_primitive = stored[0].logical_type;
    }
  }

  if (num_cols == 0) {
    // Fall back 2: first committed chunk's column_descriptors (legacy path).
    const auto& chunks = in_storage->sealed_chunks();
    if (!chunks.empty() && !chunks[0].column_descriptors.empty()) {
      num_cols = chunks[0].column_descriptors.size();
      leaf_primitive = chunks[0].column_descriptors[0].logical_type;
    }
  }

  if (num_cols == 0) {
    return pj::unexpected(absl::StrCat(
        "add_siso_transform: cannot determine column layout for topic ", input_topic_id,
        " (no schema_id, no stored column layout, and no committed chunks)"));
  }
  if (num_cols != 1) {
    return pj::unexpected(
        absl::StrCat("add_siso_transform: SISO requires single-column input, got ", num_cols, " columns"));
  }
  if (!leaf_primitive) {
    return pj::unexpected(std::string("add_siso_transform: could not determine leaf primitive type"));
  }
  StorageKind in_kind = storage_kind_of(*leaf_primitive);

  // 3. Determine output kind
  StorageKind out_kind = op->output_kind(in_kind);

  // 4. Check output name uniqueness within dataset
  auto name_key = std::make_pair(output_dataset_id, output_topic_name);
  if (impl_->registered_output_names.contains(name_key)) {
    return pj::unexpected(absl::StrCat(
        "add_siso_transform: output topic '", output_topic_name, "' already registered in dataset ",
        output_dataset_id));
  }

  // 5. Cycle detection (structurally impossible for SISO fresh output, but guard correctly)
  std::string cycle_err = check_cycle(*impl_, {input_topic_id}, {});  // output topic doesn't exist yet
  if (!cycle_err.empty()) {
    return pj::unexpected(cycle_err);
  }

  // 6. Create output schema (single column, output_kind, name = "value")
  pj::PrimitiveType out_primitive = storage_kind_to_primitive(out_kind);
  std::string schema_name = absl::StrCat("derived_siso_", output_topic_name, "_", next_node_id_);
  auto out_type_tree = pj::make_primitive("value", out_primitive);
  auto out_schema_or = engine_.type_registry().register_or_get(schema_name, out_type_tree);
  if (!out_schema_or.has_value()) {
    return pj::unexpected(out_schema_or.error());
  }

  // 7. Create output topic
  auto out_topic_or = engine_.create_topic(
      output_dataset_id,
      TopicDescriptor{.name = output_topic_name, .schema_id = *out_schema_or, .dataset_id = output_dataset_id});
  if (!out_topic_or.has_value()) {
    return pj::unexpected(out_topic_or.error());
  }
  pj::TopicId out_topic_id = *out_topic_or;

  // 8. Register node
  pj::NodeId node_id = next_node_id_++;
  DerivedNode node;
  node.id = node_id;
  node.is_mimo = false;
  node.siso_input_topic_id = input_topic_id;
  node.siso_input_kind = in_kind;
  node.siso_output_kind = out_kind;
  node.siso_op = std::move(op);
  node.all_input_topic_ids = {input_topic_id};
  node.output_topic_ids = {out_topic_id};
  node.dirty = true;

  impl_->registered_output_names[name_key] = out_topic_id;
  impl_->topic_to_nodes[input_topic_id].push_back(node_id);
  impl_->output_topic_to_node[out_topic_id] = node_id;

  // Update downstream_of: if input_topic_id is produced by another node, register dependency
  auto prod_it = impl_->output_topic_to_node.find(input_topic_id);
  if (prod_it != impl_->output_topic_to_node.end()) {
    impl_->downstream_of[prod_it->second].push_back(node_id);
  }

  impl_->nodes[node_id] = std::move(node);
  return node_id;
}

// ---------------------------------------------------------------------------
// add_mimo_transform
// ---------------------------------------------------------------------------

pj::Expected<pj::NodeId> DerivedEngine::add_mimo_transform(
    std::vector<pj::TopicId> input_topic_ids, std::vector<std::string> output_topic_names,
    pj::DatasetId output_dataset_id, std::unique_ptr<IMIMOTransform> op) {
  if (input_topic_ids.empty()) {
    return pj::unexpected(std::string("add_mimo_transform: requires at least one input topic"));
  }
  if (output_topic_names.empty()) {
    return pj::unexpected(std::string("add_mimo_transform: requires at least one output topic name"));
  }
  if (!op) {
    return pj::unexpected(std::string("add_mimo_transform: null transform op"));
  }

  // 1. Validate all inputs and determine their StorageKinds.
  //    Same 3-tier fallback as add_siso_transform: type_registry → stored
  //    column_descriptors → first sealed chunk.
  std::vector<StorageKind> input_kinds;
  input_kinds.reserve(input_topic_ids.size());

  for (pj::TopicId tid : input_topic_ids) {
    const TopicStorage* storage = engine_.get_topic_storage(tid);
    if (!storage) {
      return pj::unexpected(absl::StrCat("add_mimo_transform: input topic ", tid, " not found"));
    }

    pj::SchemaId schema_id = storage->descriptor().schema_id;
    std::size_t num_cols = 0;
    std::optional<pj::PrimitiveType> leaf_primitive;

    if (schema_id != 0) {
      const pj::TypeTreeNode* root = engine_.type_registry().lookup(schema_id);
      if (root) {
        num_cols = pj::count_leaf_fields(*root);
        leaf_primitive = find_first_leaf(*root);
      }
    }
    if (num_cols == 0) {
      const auto& stored = storage->column_descriptors();
      if (!stored.empty()) {
        num_cols = stored.size();
        leaf_primitive = stored[0].logical_type;
      }
    }
    if (num_cols == 0) {
      const auto& chunks = storage->sealed_chunks();
      if (!chunks.empty() && !chunks[0].column_descriptors.empty()) {
        num_cols = chunks[0].column_descriptors.size();
        leaf_primitive = chunks[0].column_descriptors[0].logical_type;
      }
    }

    if (num_cols == 0) {
      return pj::unexpected(absl::StrCat(
          "add_mimo_transform: cannot determine column layout for input topic ", tid));
    }
    if (num_cols != 1) {
      return pj::unexpected(absl::StrCat(
          "add_mimo_transform: MIMO requires single-column inputs; topic ", tid, " has ", num_cols, " columns"));
    }
    if (!leaf_primitive) {
      return pj::unexpected(
          absl::StrCat("add_mimo_transform: cannot determine primitive type for input topic ", tid));
    }
    input_kinds.push_back(storage_kind_of(*leaf_primitive));
  }

  // 2. Check output name uniqueness within dataset.
  for (const auto& name : output_topic_names) {
    auto key = std::make_pair(output_dataset_id, name);
    if (impl_->registered_output_names.contains(key)) {
      return pj::unexpected(absl::StrCat(
          "add_mimo_transform: output topic '", name, "' already registered in dataset ", output_dataset_id));
    }
  }

  // 3. Cycle detection.
  {
    std::string cycle_err = check_cycle(*impl_, input_topic_ids, {});
    if (!cycle_err.empty()) {
      return pj::unexpected(cycle_err);
    }
  }

  // 4. Query output StorageKinds from the transform.
  std::vector<StorageKind> output_kinds = op->output_kinds(pj::Span<const StorageKind>(input_kinds));
  if (output_kinds.size() != output_topic_names.size()) {
    return pj::unexpected(absl::StrCat(
        "add_mimo_transform: op->output_kinds() returned ", output_kinds.size(), " kinds but ",
        output_topic_names.size(), " output names provided"));
  }

  // 5. Create output schema (single "value" column) and topic for each output.
  pj::NodeId node_id = next_node_id_++;
  std::vector<pj::TopicId> out_topic_ids;
  out_topic_ids.reserve(output_topic_names.size());

  for (std::size_t k = 0; k < output_topic_names.size(); ++k) {
    pj::PrimitiveType out_primitive = storage_kind_to_primitive(output_kinds[k]);
    std::string schema_name = absl::StrCat("derived_mimo_", node_id, "_", k);
    auto out_type_tree = pj::make_primitive("value", out_primitive);
    auto out_schema_or = engine_.type_registry().register_or_get(schema_name, out_type_tree);
    if (!out_schema_or.has_value()) {
      return pj::unexpected(out_schema_or.error());
    }
    auto out_topic_or = engine_.create_topic(
        output_dataset_id,
        TopicDescriptor{
            .name = output_topic_names[k], .schema_id = *out_schema_or, .dataset_id = output_dataset_id});
    if (!out_topic_or.has_value()) {
      return pj::unexpected(out_topic_or.error());
    }
    out_topic_ids.push_back(*out_topic_or);
  }

  // 6. Build and register the node.
  DerivedNode node;
  node.id = node_id;
  node.is_mimo = true;
  node.mimo_input_topic_ids = input_topic_ids;
  node.mimo_input_kinds = std::move(input_kinds);
  node.mimo_output_kinds = std::move(output_kinds);
  node.mimo_op = std::move(op);
  node.mimo_last_ts = std::numeric_limits<pj::Timestamp>::min();
  node.all_input_topic_ids = std::move(input_topic_ids);
  node.output_topic_ids = std::move(out_topic_ids);
  node.dirty = true;

  // Register output names for uniqueness enforcement.
  for (std::size_t k = 0; k < output_topic_names.size(); ++k) {
    impl_->registered_output_names[std::make_pair(output_dataset_id, output_topic_names[k])] =
        node.output_topic_ids[k];
  }

  // Map input topics to this node (for dirty propagation via on_source_committed).
  for (pj::TopicId in_tid : node.all_input_topic_ids) {
    impl_->topic_to_nodes[in_tid].push_back(node_id);
  }

  // Map output topics to this node (for cycle detection of downstream nodes).
  for (pj::TopicId out_tid : node.output_topic_ids) {
    impl_->output_topic_to_node[out_tid] = node_id;
  }

  // Update downstream_of: if any input is produced by another derived node,
  // record that node_id depends on the producer (deduplicated for multi-input).
  for (pj::TopicId in_tid : node.all_input_topic_ids) {
    auto prod_it = impl_->output_topic_to_node.find(in_tid);
    if (prod_it != impl_->output_topic_to_node.end()) {
      auto& list = impl_->downstream_of[prod_it->second];
      if (std::find(list.begin(), list.end(), node_id) == list.end()) {
        list.push_back(node_id);
      }
    }
  }

  impl_->nodes[node_id] = std::move(node);
  return node_id;
}

// ---------------------------------------------------------------------------
// Node management
// ---------------------------------------------------------------------------

pj::Status DerivedEngine::remove_node(pj::NodeId id) {
  auto it = impl_->nodes.find(id);
  if (it == impl_->nodes.end()) {
    return pj::unexpected(absl::StrCat("remove_node: node ", id, " not found"));
  }

  const DerivedNode& node = it->second;

  // Remove from topic_to_nodes
  for (pj::TopicId in_tid : node.all_input_topic_ids) {
    auto& v = impl_->topic_to_nodes[in_tid];
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
  }

  // Remove from output_topic_to_node and registered_output_names
  for (pj::TopicId out_tid : node.output_topic_ids) {
    impl_->output_topic_to_node.erase(out_tid);
    // Remove from registered_output_names (scan for the value)
    for (auto sit = impl_->registered_output_names.begin(); sit != impl_->registered_output_names.end(); ++sit) {
      if (sit->second == out_tid) {
        impl_->registered_output_names.erase(sit);
        break;
      }
    }
  }

  // Remove from downstream_of
  impl_->downstream_of.erase(id);
  for (auto& [upstream, list] : impl_->downstream_of) {
    list.erase(std::remove(list.begin(), list.end(), id), list.end());
  }

  impl_->nodes.erase(it);
  return pj::ok_status();
}

bool DerivedEngine::has_node(pj::NodeId id) const noexcept {
  return impl_->nodes.contains(id);
}

std::vector<pj::TopicId> DerivedEngine::output_topics(pj::NodeId id) const {
  auto it = impl_->nodes.find(id);
  if (it == impl_->nodes.end()) {
    return {};
  }
  return it->second.output_topic_ids;
}

// ---------------------------------------------------------------------------
// topological_order — Kahn's algorithm
// ---------------------------------------------------------------------------

std::vector<pj::NodeId> DerivedEngine::topological_order() const {
  absl::flat_hash_map<pj::NodeId, int> in_degree;
  for (const auto& [id, _] : impl_->nodes) {
    in_degree[id] = 0;
  }

  for (const auto& [upstream, downstream_list] : impl_->downstream_of) {
    for (pj::NodeId downstream : downstream_list) {
      if (impl_->nodes.contains(downstream)) {
        in_degree[downstream]++;
      }
    }
  }

  // Seed queue with in-degree 0 nodes (sorted for determinism)
  std::vector<pj::NodeId> ready;
  ready.reserve(in_degree.size());
  for (const auto& [id, deg] : in_degree) {
    if (deg == 0) {
      ready.push_back(id);
    }
  }
  std::sort(ready.begin(), ready.end());

  std::vector<pj::NodeId> order;
  order.reserve(impl_->nodes.size());
  std::size_t head = 0;

  while (head < ready.size()) {
    pj::NodeId n = ready[head++];
    order.push_back(n);

    auto it = impl_->downstream_of.find(n);
    if (it == impl_->downstream_of.end()) {
      continue;
    }

    std::vector<pj::NodeId> newly_ready;
    for (pj::NodeId m : it->second) {
      if (!impl_->nodes.contains(m)) {
        continue;
      }
      if (--in_degree[m] == 0) {
        newly_ready.push_back(m);
      }
    }
    // Keep deterministic order within the newly ready set
    std::sort(newly_ready.begin(), newly_ready.end());
    for (pj::NodeId m : newly_ready) {
      ready.push_back(m);
    }
  }

  return order;
}

// ---------------------------------------------------------------------------
// on_source_committed
// ---------------------------------------------------------------------------

void DerivedEngine::on_source_committed(pj::Span<const pj::TopicId> changed_topics) {
  for (pj::TopicId tid : changed_topics) {
    auto it = impl_->topic_to_nodes.find(tid);
    if (it == impl_->topic_to_nodes.end()) {
      continue;
    }
    for (pj::NodeId nid : it->second) {
      auto nit = impl_->nodes.find(nid);
      if (nit != impl_->nodes.end()) {
        nit->second.dirty = true;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// run_node_incremental (private helper)
// ---------------------------------------------------------------------------

static pj::Status run_siso_incremental(DerivedEngineImpl& /*impl*/, DataEngine& engine, DerivedNode& node) {
  const TopicStorage* in_storage = engine.get_topic_storage(node.siso_input_topic_id);
  if (!in_storage) {
    return pj::unexpected(absl::StrCat("run_siso_incremental: input topic ", node.siso_input_topic_id, " not found"));
  }

  const std::deque<TopicChunk>& all_chunks = in_storage->sealed_chunks();

  DataWriter writer = engine.create_writer();
  pj::TopicId out_tid = node.output_topic_ids[0];
  pj::ChunkId max_seen = node.last_processed_chunk_id;
  bool wrote_any = false;
  pj::Timestamp out_ts = 0;

  for (const TopicChunk& chunk : all_chunks) {
    if (chunk.id <= node.last_processed_chunk_id) {
      continue;
    }
    max_seen = std::max(max_seen, chunk.id);

    for (uint32_t i = 0; i < chunk.stats.row_count; ++i) {
      pj::Timestamp ts = chunk.timestamps[i];
      node.in_val_buf = decode_as_varvalue(chunk, 0, i, node.siso_input_kind);

      if (node.siso_op->calculate(ts, node.in_val_buf, out_ts, node.out_val_buf)) {
        auto s = writer.begin_row(out_tid, out_ts);
        if (!s.has_value()) {
          return s;
        }
        write_varvalue(writer, out_tid, 0, node.out_val_buf, node.siso_output_kind);
        s = writer.finish_row(out_tid);
        if (!s.has_value()) {
          return s;
        }
        wrote_any = true;
      }
    }
  }

  if (wrote_any) {
    auto chunks = writer.flush_all();
    engine.commit_chunks(std::move(chunks));
  }

  node.last_processed_chunk_id = max_seen;
  return pj::ok_status();
}

// ---------------------------------------------------------------------------
// run_mimo_incremental
// ---------------------------------------------------------------------------

static pj::Status run_mimo_incremental(DerivedEngineImpl& /*impl*/, DataEngine& engine, DerivedNode& node) {
  const std::size_t num_inputs = node.mimo_input_topic_ids.size();
  if (num_inputs == 0) {
    return pj::ok_status();
  }

  // 1. Collect (timestamp, chunk*, row_index) for each input topic,
  //    only for rows strictly newer than the watermark.
  struct SampleLoc {
    pj::Timestamp ts;
    const TopicChunk* chunk;
    uint32_t row;
  };
  std::vector<std::vector<SampleLoc>> per_topic(num_inputs);

  for (std::size_t i = 0; i < num_inputs; ++i) {
    const TopicStorage* storage = engine.get_topic_storage(node.mimo_input_topic_ids[i]);
    if (!storage) {
      return pj::unexpected(
          absl::StrCat("run_mimo_incremental: input topic ", node.mimo_input_topic_ids[i], " not found"));
    }
    for (const TopicChunk& chunk : storage->sealed_chunks()) {
      if (chunk.stats.t_max <= node.mimo_last_ts) {
        continue;  // entire chunk already processed
      }
      for (uint32_t r = 0; r < chunk.stats.row_count; ++r) {
        pj::Timestamp ts = chunk.timestamps[r];
        if (ts <= node.mimo_last_ts) {
          continue;
        }
        per_topic[i].push_back({ts, &chunk, r});
      }
    }
    // Early exit: if any topic has no new data, no join is possible.
    if (per_topic[i].empty()) {
      return pj::ok_status();
    }
  }

  // 2. N-way timestamp intersection: find timestamps present in ALL input topics.
  //    Start from topic 0's sorted timestamps, remove any not in subsequent topics.
  std::vector<pj::Timestamp> joined_ts;
  joined_ts.reserve(per_topic[0].size());
  for (const auto& s : per_topic[0]) {
    joined_ts.push_back(s.ts);
  }

  for (std::size_t i = 1; i < num_inputs; ++i) {
    absl::flat_hash_set<pj::Timestamp> topic_set;
    topic_set.reserve(per_topic[i].size());
    for (const auto& s : per_topic[i]) {
      topic_set.insert(s.ts);
    }
    auto new_end = std::remove_if(joined_ts.begin(), joined_ts.end(),
                                   [&](pj::Timestamp t) { return !topic_set.contains(t); });
    joined_ts.erase(new_end, joined_ts.end());
    if (joined_ts.empty()) {
      return pj::ok_status();
    }
  }

  // 2b. Deduplicate joined_ts: if topic[0] has two rows at the same timestamp,
  //     that timestamp appears twice in joined_ts. We must process it exactly once
  //     (joined_ts is already sorted because per_topic[0] preserves chunk order).
  {
    auto new_end = std::unique(joined_ts.begin(), joined_ts.end());
    joined_ts.erase(new_end, joined_ts.end());
  }
  if (joined_ts.empty()) {
    return pj::ok_status();
  }

  // 3. Build per-topic lookup: timestamp → (chunk*, row_index).
  //    insert_or_assign gives last-write-wins semantics for duplicate timestamps
  //    within a topic, producing a well-defined and consistent result.
  std::vector<absl::flat_hash_map<pj::Timestamp, std::pair<const TopicChunk*, uint32_t>>> lookups(num_inputs);
  for (std::size_t i = 0; i < num_inputs; ++i) {
    lookups[i].reserve(per_topic[i].size());
    for (const auto& s : per_topic[i]) {
      lookups[i].insert_or_assign(s.ts, std::make_pair(s.chunk, s.row));
    }
  }

  // 4. Process each joined timestamp: decode, call transform, emit output.
  const std::size_t num_outputs = node.output_topic_ids.size();
  node.mimo_in_buf.resize(num_inputs);
  node.mimo_out_buf.resize(num_outputs);

  DataWriter writer = engine.create_writer();
  bool wrote_any = false;

  for (pj::Timestamp ts : joined_ts) {
    for (std::size_t i = 0; i < num_inputs; ++i) {
      const auto& [chp, row] = lookups[i].at(ts);
      node.mimo_in_buf[i] = decode_as_varvalue(*chp, 0, row, node.mimo_input_kinds[i]);
    }

    pj::Timestamp out_ts = ts;
    if (node.mimo_op->calculate(ts, node.mimo_in_buf, out_ts, node.mimo_out_buf)) {
      for (std::size_t k = 0; k < num_outputs; ++k) {
        auto s = writer.begin_row(node.output_topic_ids[k], out_ts);
        if (!s.has_value()) {
          return s;
        }
        write_varvalue(writer, node.output_topic_ids[k], 0, node.mimo_out_buf[k], node.mimo_output_kinds[k]);
        s = writer.finish_row(node.output_topic_ids[k]);
        if (!s.has_value()) {
          return s;
        }
      }
      wrote_any = true;
    }
  }

  if (wrote_any) {
    engine.commit_chunks(writer.flush_all());
  }

  // Advance watermark to the last joined input timestamp.
  // Data is monotonically increasing, so timestamps ≤ joined_ts.back() won't
  // produce new joins in the future even if not all of them generated output.
  node.mimo_last_ts = joined_ts.back();

  return pj::ok_status();
}

// ---------------------------------------------------------------------------
// schedule
// ---------------------------------------------------------------------------

pj::Status DerivedEngine::schedule(const std::unordered_set<pj::NodeId>& active_nodes) {
  auto order = topological_order();

  // Compute the set of nodes to consider (active_nodes ∪ their transitive upstream deps).
  absl::flat_hash_set<pj::NodeId> filter;
  if (!active_nodes.empty()) {
    std::queue<pj::NodeId> bfs;
    for (pj::NodeId n : active_nodes) {
      if (impl_->nodes.contains(n)) {
        filter.insert(n);
        bfs.push(n);
      }
    }
    while (!bfs.empty()) {
      pj::NodeId curr = bfs.front();
      bfs.pop();
      auto nit = impl_->nodes.find(curr);
      if (nit == impl_->nodes.end()) {
        continue;
      }
      for (pj::TopicId in_tid : nit->second.all_input_topic_ids) {
        auto prod_it = impl_->output_topic_to_node.find(in_tid);
        if (prod_it == impl_->output_topic_to_node.end()) {
          continue;
        }
        pj::NodeId prod = prod_it->second;
        if (filter.insert(prod).second) {
          bfs.push(prod);
        }
      }
    }
  }

  for (pj::NodeId node_id : order) {
    if (!active_nodes.empty() && !filter.contains(node_id)) {
      continue;
    }

    auto& node = impl_->nodes.at(node_id);
    if (!node.dirty) {
      continue;
    }

    pj::Status s = pj::ok_status();
    if (!node.is_mimo) {
      s = run_siso_incremental(*impl_, engine_, node);
    } else {
      s = run_mimo_incremental(*impl_, engine_, node);
    }

    if (!s.has_value()) {
      return s;
    }

    node.dirty = false;

    // Propagate dirty to downstream nodes
    auto dit = impl_->downstream_of.find(node_id);
    if (dit != impl_->downstream_of.end()) {
      for (pj::NodeId downstream : dit->second) {
        auto dnit = impl_->nodes.find(downstream);
        if (dnit != impl_->nodes.end()) {
          dnit->second.dirty = true;
        }
      }
    }
  }

  return pj::ok_status();
}

// ---------------------------------------------------------------------------
// recompute_batch
// ---------------------------------------------------------------------------

pj::Status DerivedEngine::recompute_batch(pj::NodeId node_id) {
  auto it = impl_->nodes.find(node_id);
  if (it == impl_->nodes.end()) {
    return pj::unexpected(absl::StrCat("recompute_batch: node ", node_id, " not found"));
  }
  DerivedNode& node = it->second;

  // 1. Clear all output chunks unconditionally.
  for (pj::TopicId out_tid : node.output_topic_ids) {
    TopicStorage* storage = engine_.get_topic_storage(out_tid);
    if (storage) {
      storage->clear_chunks();
    }
  }

  // 2. Reset transform state
  if (!node.is_mimo) {
    if (node.siso_op) {
      node.siso_op->reset();
    }
  } else {
    if (node.mimo_op) {
      node.mimo_op->reset();
    }
  }

  // 3. Reset processed chunk watermark
  node.last_processed_chunk_id = 0;
  if (node.is_mimo) {
    node.mimo_last_ts = std::numeric_limits<pj::Timestamp>::min();
  }

  // 4. Full replay
  pj::Status s = pj::ok_status();
  if (!node.is_mimo) {
    s = run_siso_incremental(*impl_, engine_, node);
  } else {
    s = run_mimo_incremental(*impl_, engine_, node);
  }

  if (!s.has_value()) {
    return s;
  }
  node.dirty = false;
  return pj::ok_status();
}

}  // namespace pj::engine
