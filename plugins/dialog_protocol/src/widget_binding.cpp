#include <PJ/host_qt/widget_binding.hpp>

#include <PJ/host/widget_event_builder.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>

#include <set>

namespace PJ::host_qt {

// ---------------------------------------------------------------------------
// apply_widget_data — push WidgetDataView values into Qt widgets
// ---------------------------------------------------------------------------

static void apply_to_widget(QWidget* w, std::string_view name,
                            const PJ::host::WidgetDataView& view) {
  const QSignalBlocker blocker(w);

  // --- Generic properties (any widget) ---
  if (auto v = view.enabled(name)) w->setEnabled(*v);
  if (auto v = view.visible(name)) w->setVisible(*v);

  // --- QLineEdit ---
  if (auto* le = qobject_cast<QLineEdit*>(w)) {
    if (auto v = view.text(name)) le->setText(QString::fromStdString(*v));
    if (auto v = view.placeholder(name)) le->setPlaceholderText(QString::fromStdString(*v));
    if (auto v = view.read_only(name)) le->setReadOnly(*v);
    return;
  }

  // --- QComboBox ---
  if (auto* cb = qobject_cast<QComboBox*>(w)) {
    if (auto v = view.items(name)) {
      cb->clear();
      for (const auto& item : *v) cb->addItem(QString::fromStdString(item));
    }
    if (auto v = view.current_index(name)) cb->setCurrentIndex(*v);
    return;
  }

  // --- QCheckBox ---
  if (auto* ck = qobject_cast<QCheckBox*>(w)) {
    if (auto v = view.checked(name)) ck->setChecked(*v);
    if (auto v = view.text(name)) ck->setText(QString::fromStdString(*v));
    return;
  }

  // --- QRadioButton ---
  if (auto* rb = qobject_cast<QRadioButton*>(w)) {
    if (auto v = view.checked(name)) rb->setChecked(*v);
    return;
  }

  // --- QSpinBox ---
  if (auto* sb = qobject_cast<QSpinBox*>(w)) {
    if (auto v = view.range_min(name)) sb->setMinimum(*v);
    if (auto v = view.range_max(name)) sb->setMaximum(*v);
    if (auto v = view.value_int(name)) sb->setValue(*v);
    return;
  }

  // --- QDoubleSpinBox ---
  if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
    if (auto v = view.value_double(name)) dsb->setValue(*v);
    return;
  }

  // --- QListWidget ---
  if (auto* lw = qobject_cast<QListWidget*>(w)) {
    if (auto v = view.list_items(name)) {
      lw->clear();
      for (const auto& item : *v) lw->addItem(QString::fromStdString(item));
    }
    if (auto v = view.selected_items(name)) {
      std::set<std::string> selected(v->begin(), v->end());
      for (int i = 0; i < lw->count(); ++i) {
        auto* item = lw->item(i);
        item->setSelected(selected.count(item->text().toStdString()) > 0);
      }
    }
    return;
  }

  // --- QTableWidget ---
  if (auto* tw = qobject_cast<QTableWidget*>(w)) {
    if (auto v = view.table_headers(name)) {
      QStringList hdr;
      for (const auto& h : *v) hdr << QString::fromStdString(h);
      tw->setColumnCount(hdr.size());
      tw->setHorizontalHeaderLabels(hdr);
    }
    if (auto v = view.table_rows(name)) {
      tw->setRowCount(static_cast<int>(v->size()));
      for (int r = 0; r < static_cast<int>(v->size()); ++r) {
        const auto& row = (*v)[r];
        for (int c = 0; c < static_cast<int>(row.size()); ++c) {
          tw->setItem(r, c, new QTableWidgetItem(QString::fromStdString(row[c])));
        }
      }
    }
    return;
  }

  // --- QLabel ---
  if (auto* lbl = qobject_cast<QLabel*>(w)) {
    if (auto v = view.label(name)) lbl->setText(QString::fromStdString(*v));
    // Also allow "text" for labels
    if (auto v = view.text(name)) lbl->setText(QString::fromStdString(*v));
    return;
  }

  // --- QPushButton ---
  if (auto* btn = qobject_cast<QPushButton*>(w)) {
    if (auto v = view.button_text(name)) btn->setText(QString::fromStdString(*v));
    return;
  }

  // --- QTabWidget ---
  if (auto* tw = qobject_cast<QTabWidget*>(w)) {
    if (auto v = view.tab_index(name)) tw->setCurrentIndex(*v);
    return;
  }

  // --- QDialogButtonBox ---
  if (auto* dbb = qobject_cast<QDialogButtonBox*>(w)) {
    if (auto v = view.ok_enabled(name)) {
      if (auto* ok = dbb->button(QDialogButtonBox::Ok)) ok->setEnabled(*v);
    }
    return;
  }

  // Containers (QFrame, QGroupBox, QWidget) — only generic properties applied above.
}

void apply_widget_data(QWidget* root, const PJ::host::WidgetDataView& view) {
  for (const auto& name : view.widget_names()) {
    auto* w = root->findChild<QWidget*>(QString::fromStdString(name));
    if (!w) continue;
    apply_to_widget(w, name, view);
  }
}

// ---------------------------------------------------------------------------
// connect_widget_signals — wire Qt signals to WidgetEventBuilder output
// ---------------------------------------------------------------------------

static bool is_internal_widget_name(const QString& name) {
  return name.startsWith("qt_");
}

void connect_widget_signals(QWidget* root, WidgetEventCallback callback) {
  using PJ::host::WidgetEventBuilder;

  for (auto* w : root->findChildren<QWidget*>()) {
    QString qname = w->objectName();
    if (qname.isEmpty() || is_internal_widget_name(qname)) continue;
    std::string name = qname.toStdString();

    if (auto* le = qobject_cast<QLineEdit*>(w)) {
      QObject::connect(le, &QLineEdit::textChanged, le,
                       [callback, name](const QString& text) {
                         callback(name, WidgetEventBuilder::text_changed(text.toStdString()));
                       });
      continue;
    }
    if (auto* cb = qobject_cast<QComboBox*>(w)) {
      QObject::connect(cb, &QComboBox::currentIndexChanged, cb,
                       [callback, name, cb](int index) {
                         callback(name, WidgetEventBuilder::index_changed(
                                            index, cb->currentText().toStdString()));
                       });
      continue;
    }
    if (auto* ck = qobject_cast<QCheckBox*>(w)) {
      QObject::connect(ck, &QCheckBox::toggled, ck,
                       [callback, name](bool checked) {
                         callback(name, WidgetEventBuilder::toggled(checked));
                       });
      continue;
    }
    if (auto* rb = qobject_cast<QRadioButton*>(w)) {
      QObject::connect(rb, &QRadioButton::toggled, rb,
                       [callback, name](bool checked) {
                         callback(name, WidgetEventBuilder::toggled(checked));
                       });
      continue;
    }
    if (auto* sb = qobject_cast<QSpinBox*>(w)) {
      QObject::connect(sb, &QSpinBox::valueChanged, sb,
                       [callback, name](int value) {
                         callback(name, WidgetEventBuilder::value_changed(value));
                       });
      continue;
    }
    if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
      QObject::connect(dsb, &QDoubleSpinBox::valueChanged, dsb,
                       [callback, name](double value) {
                         callback(name, WidgetEventBuilder::value_changed(value));
                       });
      continue;
    }
    if (auto* lw = qobject_cast<QListWidget*>(w)) {
      QObject::connect(lw, &QListWidget::itemSelectionChanged, lw,
                       [callback, name, lw]() {
                         std::vector<std::string> sel;
                         for (auto* item : lw->selectedItems()) {
                           sel.push_back(item->text().toStdString());
                         }
                         callback(name, WidgetEventBuilder::selection_changed(sel));
                       });
      continue;
    }
    if (auto* btn = qobject_cast<QPushButton*>(w)) {
      // Skip buttons that are part of QDialogButtonBox
      if (qobject_cast<QDialogButtonBox*>(btn->parent())) continue;
      QObject::connect(btn, &QPushButton::clicked, btn,
                       [callback, name]() {
                         callback(name, WidgetEventBuilder::clicked());
                       });
      continue;
    }
    if (auto* tw = qobject_cast<QTabWidget*>(w)) {
      QObject::connect(tw, &QTabWidget::currentChanged, tw,
                       [callback, name](int index) {
                         callback(name, WidgetEventBuilder::tab_changed(index));
                       });
      continue;
    }
  }
}

}  // namespace PJ::host_qt
