#include "series_tree_model.hpp"

#include <QColor>
#include <QDataStream>
#include <QIODevice>
#include <QPixmap>

#include "pj_datastore/reader.hpp"

namespace proto {

static constexpr quintptr kNoParent = 0xFFFFFFFF;

SeriesTreeModel::SeriesTreeModel(const PJ::DataEngine& engine, QObject* parent)
    : QAbstractItemModel(parent), engine_(engine) {}

void SeriesTreeModel::rebuild() {
  beginResetModel();
  datasets_.clear();

  auto reader = engine_.createReader();
  for (auto ds_id : reader.listDatasets()) {
    if (hidden_datasets_.count(ds_id) != 0) {
      continue;
    }
    DatasetNode ds_node;
    ds_node.dataset_id = ds_id;
    auto* ds_info = engine_.getDataset(ds_id);
    ds_node.name = ds_info ? ds_info->source_name : ("dataset_" + std::to_string(ds_id));

    for (auto topic_id : reader.listTopics(ds_id)) {
      TopicNode topic_node;
      topic_node.topic_id = topic_id;

      auto meta = reader.getMetadata(topic_id);
      topic_node.name = meta ? meta->name : ("topic_" + std::to_string(topic_id));

      auto* storage = engine_.getTopicStorage(topic_id);
      if (storage) {
        const auto& col_descs = storage->columnDescriptors();
        for (size_t i = 0; i < col_descs.size(); ++i) {
          FieldNode field;
          field.name = col_descs[i].field_path;
          field.topic_id = topic_id;
          field.col_index = i;
          topic_node.fields.push_back(std::move(field));
        }
      }

      ds_node.topics.push_back(std::move(topic_node));
    }

    datasets_.push_back(std::move(ds_node));
  }

  endResetModel();
}

void SeriesTreeModel::rebuildIfChanged() {
  // Build a candidate snapshot and compare to current state.
  // Only reset the model if the structure actually differs.
  std::vector<DatasetNode> candidate;

  auto reader = engine_.createReader();
  for (auto ds_id : reader.listDatasets()) {
    if (hidden_datasets_.count(ds_id) != 0) {
      continue;
    }
    DatasetNode ds_node;
    ds_node.dataset_id = ds_id;
    auto* ds_info = engine_.getDataset(ds_id);
    ds_node.name = ds_info ? ds_info->source_name : ("dataset_" + std::to_string(ds_id));

    for (auto topic_id : reader.listTopics(ds_id)) {
      TopicNode topic_node;
      topic_node.topic_id = topic_id;

      auto meta = reader.getMetadata(topic_id);
      topic_node.name = meta ? meta->name : ("topic_" + std::to_string(topic_id));

      auto* storage = engine_.getTopicStorage(topic_id);
      if (storage) {
        const auto& col_descs = storage->columnDescriptors();
        for (size_t i = 0; i < col_descs.size(); ++i) {
          FieldNode field;
          field.name = col_descs[i].field_path;
          field.topic_id = topic_id;
          field.col_index = i;
          topic_node.fields.push_back(std::move(field));
        }
      }

      ds_node.topics.push_back(std::move(topic_node));
    }

    candidate.push_back(std::move(ds_node));
  }

  // Compare: same structure?
  if (candidate.size() == datasets_.size()) {
    bool same = true;
    for (size_t d = 0; d < candidate.size() && same; ++d) {
      const auto& a = candidate[d];
      const auto& b = datasets_[d];
      if (a.dataset_id != b.dataset_id || a.name != b.name || a.topics.size() != b.topics.size()) {
        same = false;
        break;
      }
      for (size_t t = 0; t < a.topics.size() && same; ++t) {
        const auto& ta = a.topics[t];
        const auto& tb = b.topics[t];
        if (ta.topic_id != tb.topic_id || ta.name != tb.name || ta.fields.size() != tb.fields.size()) {
          same = false;
          break;
        }
        for (size_t f = 0; f < ta.fields.size() && same; ++f) {
          if (ta.fields[f].name != tb.fields[f].name || ta.fields[f].col_index != tb.fields[f].col_index) {
            same = false;
          }
        }
      }
    }
    if (same) {
      return;  // no change — skip reset
    }
  }

  beginResetModel();
  datasets_ = std::move(candidate);
  endResetModel();
}

// Internal ID encoding:
// Level 0 (dataset): internalId = kNoParent
// Level 1 (topic): internalId = dataset_index
// Level 2 (field): internalId = (dataset_index << 16) | topic_index | 0x80000000

QModelIndex SeriesTreeModel::index(int row, int column, const QModelIndex& parent) const {
  if (!hasIndex(row, column, parent)) {
    return {};
  }

  if (!parent.isValid()) {
    // Level 0: dataset
    return createIndex(row, column, kNoParent);
  }

  auto parent_id = parent.internalId();
  if (parent_id == kNoParent) {
    // Level 1: topic — parent is dataset at parent.row()
    return createIndex(row, column, static_cast<quintptr>(parent.row()));
  }

  if ((parent_id & 0x80000000u) == 0) {
    // Level 2: field — parent is topic
    auto ds_idx = static_cast<quintptr>(parent_id);
    auto topic_idx = static_cast<quintptr>(parent.row());
    return createIndex(row, column, static_cast<quintptr>(0x80000000u | (ds_idx << 16) | topic_idx));
  }

  return {};
}

QModelIndex SeriesTreeModel::parent(const QModelIndex& child) const {
  if (!child.isValid()) {
    return {};
  }

  auto id = child.internalId();
  if (id == kNoParent) {
    return {};  // dataset has no parent
  }

  if ((id & 0x80000000u) == 0) {
    // Topic: parent is dataset at index `id`
    return createIndex(static_cast<int>(id), 0, kNoParent);
  }

  // Field: decode dataset and topic index
  auto ds_idx = static_cast<int>((id >> 16) & 0x7FFF);
  auto topic_idx = static_cast<int>(id & 0xFFFF);
  return createIndex(topic_idx, 0, static_cast<quintptr>(ds_idx));
}

int SeriesTreeModel::rowCount(const QModelIndex& parent) const {
  if (!parent.isValid()) {
    return static_cast<int>(datasets_.size());
  }

  auto id = parent.internalId();
  if (id == kNoParent) {
    // Dataset → topic count
    auto ds_row = static_cast<size_t>(parent.row());
    return ds_row < datasets_.size() ? static_cast<int>(datasets_[ds_row].topics.size()) : 0;
  }

  if ((id & 0x80000000u) == 0) {
    // Topic → field count
    auto ds_idx = static_cast<size_t>(id);
    auto topic_row = static_cast<size_t>(parent.row());
    if (ds_idx < datasets_.size() && topic_row < datasets_[ds_idx].topics.size()) {
      return static_cast<int>(datasets_[ds_idx].topics[topic_row].fields.size());
    }
    return 0;
  }

  return 0;  // fields have no children
}

int SeriesTreeModel::columnCount(const QModelIndex&) const {
  return 1;
}

QVariant SeriesTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) {
    return {};
  }

  auto id = index.internalId();

  // Dataset-level decoration: colored state indicator
  if (id == kNoParent && role == Qt::DecorationRole) {
    auto row = static_cast<size_t>(index.row());
    if (row >= datasets_.size()) {
      return {};
    }
    auto it = dataset_states_.find(datasets_[row].dataset_id);
    if (it == dataset_states_.end()) {
      return {};
    }
    QColor color;
    switch (it->second) {
      case PJ_DATA_SOURCE_STATE_RUNNING:
        color = QColor(0x4C, 0xAF, 0x50);
        break;  // green
      case PJ_DATA_SOURCE_STATE_PAUSED:
        color = QColor(0xFF, 0x98, 0x00);
        break;  // orange
      case PJ_DATA_SOURCE_STATE_STOPPED:
        color = QColor(0x9E, 0x9E, 0x9E);
        break;  // gray
      case PJ_DATA_SOURCE_STATE_FAILED:
        color = QColor(0xF4, 0x43, 0x36);
        break;  // red
      default:
        return {};
    }
    QPixmap pm(10, 10);
    pm.fill(color);
    return pm;
  }

  // Dataset-level tooltip: state name
  if (id == kNoParent && role == Qt::ToolTipRole) {
    auto row = static_cast<size_t>(index.row());
    if (row >= datasets_.size()) {
      return {};
    }
    auto it = dataset_states_.find(datasets_[row].dataset_id);
    if (it == dataset_states_.end()) {
      return {};
    }
    switch (it->second) {
      case PJ_DATA_SOURCE_STATE_RUNNING:
        return QStringLiteral("Running");
      case PJ_DATA_SOURCE_STATE_PAUSED:
        return QStringLiteral("Paused");
      case PJ_DATA_SOURCE_STATE_STOPPED:
        return QStringLiteral("Stopped");
      case PJ_DATA_SOURCE_STATE_FAILED:
        return QStringLiteral("Failed");
      default:
        return {};
    }
  }

  if (role != Qt::DisplayRole) {
    return {};
  }

  if (id == kNoParent) {
    auto row = static_cast<size_t>(index.row());
    return row < datasets_.size() ? QString::fromStdString(datasets_[row].name) : QVariant();
  }

  if ((id & 0x80000000u) == 0) {
    auto ds_idx = static_cast<size_t>(id);
    auto row = static_cast<size_t>(index.row());
    if (ds_idx < datasets_.size() && row < datasets_[ds_idx].topics.size()) {
      return QString::fromStdString(datasets_[ds_idx].topics[row].name);
    }
    return {};
  }

  // Field
  auto ds_idx = static_cast<size_t>((id >> 16) & 0x7FFF);
  auto topic_idx = static_cast<size_t>(id & 0xFFFF);
  auto row = static_cast<size_t>(index.row());
  if (ds_idx < datasets_.size() && topic_idx < datasets_[ds_idx].topics.size() &&
      row < datasets_[ds_idx].topics[topic_idx].fields.size()) {
    return QString::fromStdString(datasets_[ds_idx].topics[topic_idx].fields[row].name);
  }
  return {};
}

Qt::ItemFlags SeriesTreeModel::flags(const QModelIndex& index) const {
  auto base_flags = QAbstractItemModel::flags(index);
  if (!index.isValid()) {
    return base_flags;
  }

  auto id = index.internalId();
  if ((id & 0x80000000u) != 0) {
    // Leaf fields are draggable
    return base_flags | Qt::ItemIsDragEnabled;
  }
  return base_flags;
}

QStringList SeriesTreeModel::mimeTypes() const {
  return {"application/x-pj-field"};
}

QMimeData* SeriesTreeModel::mimeData(const QModelIndexList& indexes) const {
  if (indexes.isEmpty()) {
    return nullptr;
  }

  QByteArray encoded;
  QDataStream stream(&encoded, QIODevice::WriteOnly);

  quint32 count = 0;
  // Reserve space for the count; we'll overwrite it after collecting valid fields
  stream << count;

  for (const auto& idx : indexes) {
    auto id = idx.internalId();
    if ((id & 0x80000000u) == 0) {
      continue;  // skip dataset/topic nodes
    }

    auto ds_idx = static_cast<size_t>((id >> 16) & 0x7FFF);
    auto topic_idx = static_cast<size_t>(id & 0xFFFF);
    auto row = static_cast<size_t>(idx.row());

    if (ds_idx >= datasets_.size() || topic_idx >= datasets_[ds_idx].topics.size() ||
        row >= datasets_[ds_idx].topics[topic_idx].fields.size()) {
      continue;
    }

    const auto& topic = datasets_[ds_idx].topics[topic_idx];
    const auto& field = topic.fields[row];
    std::string qualified_name = topic.name + "/" + field.name;
    stream << static_cast<quint32>(field.topic_id) << static_cast<quint32>(field.col_index)
           << QString::fromStdString(qualified_name);
    ++count;
  }

  if (count == 0) {
    return nullptr;
  }

  // Overwrite the count at the start of the buffer
  QDataStream fix(&encoded, QIODevice::WriteOnly);
  fix << count;

  auto* mime = new QMimeData();
  mime->setData("application/x-pj-field", encoded);
  return mime;
}

void SeriesTreeModel::setDatasetState(PJ::DatasetId id, PJ_data_source_state_t state) {
  dataset_states_[id] = state;
  // Find the row for this dataset and emit dataChanged for the decoration
  for (size_t i = 0; i < datasets_.size(); ++i) {
    if (datasets_[i].dataset_id == id) {
      auto idx = createIndex(static_cast<int>(i), 0, kNoParent);
      emit dataChanged(idx, idx, {Qt::DecorationRole, Qt::ToolTipRole});
      break;
    }
  }
}

void SeriesTreeModel::hideDataset(PJ::DatasetId id) {
  hidden_datasets_.insert(id);
  dataset_states_.erase(id);
}

void SeriesTreeModel::clearHidden() {
  hidden_datasets_.clear();
  dataset_states_.clear();
}

PJ::DatasetId SeriesTreeModel::datasetIdAt(const QModelIndex& index) const {
  if (!index.isValid() || index.internalId() != kNoParent) {
    return 0;
  }
  auto row = static_cast<size_t>(index.row());
  return row < datasets_.size() ? datasets_[row].dataset_id : 0;
}

bool SeriesTreeModel::isDatasetNode(const QModelIndex& index) const {
  return index.isValid() && index.internalId() == kNoParent;
}

}  // namespace proto
