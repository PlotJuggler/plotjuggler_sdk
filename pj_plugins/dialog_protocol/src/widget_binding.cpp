#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <pj_plugins/host/widget_event_builder.hpp>
#include <pj_plugins/host_qt/chart_preview_widget.hpp>
#include <pj_plugins/host_qt/widget_binding.hpp>
#include "lua_syntax_highlighter.hpp"
#include "python_syntax_highlighter.hpp"
#include <set>

namespace PJ {

// ---------------------------------------------------------------------------
// apply_widget_data — push WidgetDataView values into Qt widgets
// ---------------------------------------------------------------------------

static void apply_to_widget(QWidget* w, std::string_view name, const PJ::WidgetDataView& view) {
  const QSignalBlocker blocker(w);

  // --- Generic properties (any widget) ---
  if (auto v = view.enabled(name)) {
    w->setEnabled(*v);
  }
  if (auto v = view.visible(name)) {
    w->setVisible(*v);
  }

  // --- QLineEdit ---
  if (auto* le = qobject_cast<QLineEdit*>(w)) {
    if (auto v = view.text(name)) {
      le->setText(QString::fromStdString(*v));
    }
    if (auto v = view.placeholder(name)) {
      le->setPlaceholderText(QString::fromStdString(*v));
    }
    if (auto v = view.readOnly(name)) {
      le->setReadOnly(*v);
    }
    return;
  }

  // --- QPlainTextEdit ---
  if (auto* pte = qobject_cast<QPlainTextEdit*>(w)) {
    if (auto code = view.codeContent(name)) {
      // Code editor mode: only update if content actually differs (preserve cursor).
      QString new_text = QString::fromStdString(*code);
      if (pte->toPlainText() != new_text) {
        pte->setPlainText(new_text);
      }
      // Install or swap syntax highlighter when the language changes.
      if (auto lang = view.codeLanguage(name)) {
        QString current = pte->property("_pj_code_lang").toString();
        QString requested = QString::fromStdString(*lang);
        if (current != requested) {
          pte->setProperty("_pj_code_lang", requested);
          if (auto* old = pte->document()->findChild<QSyntaxHighlighter*>()) {
            delete old;
          }
          if (*lang == "lua") {
            new PJ::LuaSyntaxHighlighter(pte->document());
          } else if (*lang == "python") {
            new PJ::PythonSyntaxHighlighter(pte->document());
          }
        }
      }
    } else if (auto pt = view.plainText(name)) {
      pte->setPlainText(QString::fromStdString(*pt));
    }
    if (auto v = view.readOnly(name)) {
      pte->setReadOnly(*v);
    }
    return;
  }

  // --- QComboBox ---
  if (auto* cb = qobject_cast<QComboBox*>(w)) {
    if (auto v = view.items(name)) {
      cb->clear();
      for (const auto& item : *v) {
        cb->addItem(QString::fromStdString(item));
      }
    }
    if (auto v = view.currentIndex(name)) {
      cb->setCurrentIndex(*v);
    }
    return;
  }

  // --- QCheckBox ---
  if (auto* ck = qobject_cast<QCheckBox*>(w)) {
    if (auto v = view.checked(name)) {
      ck->setChecked(*v);
    }
    if (auto v = view.text(name)) {
      ck->setText(QString::fromStdString(*v));
    }
    return;
  }

  // --- QRadioButton ---
  if (auto* rb = qobject_cast<QRadioButton*>(w)) {
    if (auto v = view.checked(name)) {
      rb->setChecked(*v);
    }
    return;
  }

  // --- QSpinBox ---
  if (auto* sb = qobject_cast<QSpinBox*>(w)) {
    if (auto v = view.rangeMin(name)) {
      sb->setMinimum(*v);
    }
    if (auto v = view.rangeMax(name)) {
      sb->setMaximum(*v);
    }
    if (auto v = view.valueInt(name)) {
      sb->setValue(*v);
    }
    return;
  }

  // --- QDoubleSpinBox ---
  if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
    if (auto v = view.valueDouble(name)) {
      dsb->setValue(*v);
    }
    return;
  }

  // --- QListWidget ---
  if (auto* lw = qobject_cast<QListWidget*>(w)) {
    if (auto v = view.listItems(name)) {
      lw->clear();
      for (const auto& item : *v) {
        lw->addItem(QString::fromStdString(item));
      }
    }
    if (auto v = view.selectedItems(name)) {
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
    if (auto v = view.tableHeaders(name)) {
      QStringList hdr;
      for (const auto& h : *v) {
        hdr << QString::fromStdString(h);
      }
      tw->setColumnCount(static_cast<int>(hdr.size()));
      tw->setHorizontalHeaderLabels(hdr);
      // Stretch first column, resize-to-contents for the rest
      auto* header = tw->horizontalHeader();
      if (hdr.size() > 0) {
        header->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int i = 1; i < hdr.size(); ++i) {
          header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        }
      }
    }
    if (auto v = view.tableRows(name)) {
      tw->setRowCount(static_cast<int>(v->size()));
      for (std::size_t r = 0; r < v->size(); ++r) {
        const auto& row = (*v)[r];
        for (std::size_t c = 0; c < row.size(); ++c) {
          tw->setItem(static_cast<int>(r), static_cast<int>(c), new QTableWidgetItem(QString::fromStdString(row[c])));
        }
      }
    }
    if (auto v = view.disabledRows(name)) {
      std::set<int> disabled(v->begin(), v->end());
      for (int r = 0; r < tw->rowCount(); ++r) {
        bool is_disabled = disabled.count(r) > 0;
        for (int c = 0; c < tw->columnCount(); ++c) {
          if (auto* item = tw->item(r, c)) {
            auto flags = item->flags();
            if (is_disabled) {
              flags &= ~Qt::ItemIsEnabled;
              flags &= ~Qt::ItemIsSelectable;
            } else {
              flags |= Qt::ItemIsEnabled;
              flags |= Qt::ItemIsSelectable;
            }
            item->setFlags(flags);
          }
        }
      }
    }
    if (auto v = view.selectedRows(name)) {
      tw->clearSelection();
      for (int r : *v) {
        if (r >= 0 && r < tw->rowCount()) {
          tw->selectRow(r);
        }
      }
    }
    return;
  }

  // --- QLabel ---
  if (auto* lbl = qobject_cast<QLabel*>(w)) {
    if (auto v = view.label(name)) {
      lbl->setText(QString::fromStdString(*v));
    }
    // Also allow "text" for labels
    if (auto v = view.text(name)) {
      lbl->setText(QString::fromStdString(*v));
    }
    return;
  }

  // --- QPushButton ---
  if (auto* btn = qobject_cast<QPushButton*>(w)) {
    if (auto v = view.buttonText(name)) {
      btn->setText(QString::fromStdString(*v));
    }
    if (auto svg = view.buttonIconSvg(name)) {
      QByteArray svg_data = QByteArray::fromStdString(*svg);
      QSvgRenderer renderer(svg_data);
      if (renderer.isValid()) {
        int sz = btn->iconSize().height() > 0 ? btn->iconSize().height() : 16;
        QPixmap pix(sz, sz);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        renderer.render(&painter);
        btn->setIcon(QIcon(pix));
      }
    }
    return;
  }

  // --- QTabWidget ---
  if (auto* tw = qobject_cast<QTabWidget*>(w)) {
    if (auto v = view.tabIndex(name)) {
      tw->setCurrentIndex(*v);
    }
    return;
  }

  // --- QDialogButtonBox ---
  if (auto* dbb = qobject_cast<QDialogButtonBox*>(w)) {
    if (auto v = view.okEnabled(name)) {
      if (auto* ok = dbb->button(QDialogButtonBox::Ok)) {
        ok->setEnabled(*v);
      }
    }
    return;
  }

  // --- QFrame with chart_series or chart_zoom_enabled → ChartPreviewWidget ---
  if (auto* frame = qobject_cast<QFrame*>(w)) {
    auto series_data = view.chartSeries(name);
    auto zoom_enabled = view.chartZoomEnabled(name);
    if (series_data || zoom_enabled) {
      // Find or create the ChartPreviewWidget inside this frame.
      auto* chart = frame->findChild<PJ::ChartPreviewWidget*>();
      if (!chart) {
        auto* layout = frame->layout();
        if (!layout) {
          layout = new QVBoxLayout(frame);
          layout->setContentsMargins(0, 0, 0, 0);
        }
        chart = new PJ::ChartPreviewWidget(frame);
        layout->addWidget(chart);
      }
      if (series_data) {
        // Convert WidgetDataView series to ChartPreviewWidget series.
        std::vector<PJ::ChartPreviewWidget::Series> chart_series;
        chart_series.reserve(series_data->size());
        for (const auto& s : *series_data) {
          chart_series.push_back({s.label, s.points, s.color});
        }
        chart->setSeries(chart_series);
      }
      if (zoom_enabled) {
        chart->setZoomEnabled(*zoom_enabled);
      }
    }
    return;
  }

  // Containers (QGroupBox, QWidget) — only generic properties applied above.
  // Warn about widget types that have data in the view but aren't handled.
  // Skip known container types that only use generic enabled/visible properties.
  if (!qobject_cast<QGroupBox*>(w) && !qobject_cast<QSplitter*>(w)) {
    qWarning(
        "WidgetBinding: unsupported widget type '%s' for '%s'; "
        "see dialog-plugin-guide.md for supported types",
        w->metaObject()->className(), std::string(name).c_str());
  }
}

void applyWidgetData(QWidget* root, const PJ::WidgetDataView& view) {
  for (const auto& name : view.widgetNames()) {
    auto* w = root->findChild<QWidget*>(QString::fromStdString(name));
    if (!w) {
      continue;
    }
    apply_to_widget(w, name, view);
  }
}

// ---------------------------------------------------------------------------
// connect_widget_signals — wire Qt signals to WidgetEventBuilder output
// ---------------------------------------------------------------------------

static bool is_internal_widget_name(const QString& name) {
  return name.startsWith("qt_");
}

void connectWidgetSignals(QWidget* root, WidgetEventCallback callback) {
  using PJ::WidgetEventBuilder;

  // ChartPreviewWidget instances are unnamed children of their parent QFrame.
  // Wire their viewChanged signals using the parent frame's objectName as the event widget name.
  // Must run after applyWidgetData() so charts that were created on first apply are found here.
  for (auto* chart : root->findChildren<PJ::ChartPreviewWidget*>()) {
    auto* parent_frame = qobject_cast<QFrame*>(chart->parent());
    if (!parent_frame || parent_frame->objectName().isEmpty()) {
      continue;
    }
    std::string chart_name = parent_frame->objectName().toStdString();
    QObject::connect(chart, &PJ::ChartPreviewWidget::viewChanged, chart,
                     [callback, chart_name](double x_min, double x_max, double y_min, double y_max) {
                       callback(chart_name, WidgetEventBuilder::chartViewChanged(x_min, x_max, y_min, y_max));
                     });
  }

  for (auto* w : root->findChildren<QWidget*>()) {
    QString qname = w->objectName();
    if (qname.isEmpty() || is_internal_widget_name(qname)) {
      continue;
    }
    std::string name = qname.toStdString();

    if (auto* le = qobject_cast<QLineEdit*>(w)) {
      QObject::connect(le, &QLineEdit::textChanged, le, [callback, name](const QString& text) {
        callback(name, WidgetEventBuilder::textChanged(text.toStdString()));
      });
      continue;
    }
    if (auto* pte = qobject_cast<QPlainTextEdit*>(w)) {
      // Only wire code editors (marked by _pj_code_lang property), not read-only plain text.
      if (pte->property("_pj_code_lang").isValid()) {
        QObject::connect(pte, &QPlainTextEdit::textChanged, pte, [callback, name, pte]() {
          callback(name, WidgetEventBuilder::codeChanged(pte->toPlainText().toStdString()));
        });
      }
      continue;
    }
    if (auto* cb = qobject_cast<QComboBox*>(w)) {
      QObject::connect(cb, &QComboBox::currentIndexChanged, cb, [callback, name, cb](int index) {
        callback(name, WidgetEventBuilder::indexChanged(index, cb->currentText().toStdString()));
      });
      continue;
    }
    if (auto* ck = qobject_cast<QCheckBox*>(w)) {
      QObject::connect(ck, &QCheckBox::toggled, ck, [callback, name](bool checked) {
        callback(name, WidgetEventBuilder::toggled(checked));
      });
      continue;
    }
    if (auto* rb = qobject_cast<QRadioButton*>(w)) {
      QObject::connect(rb, &QRadioButton::toggled, rb, [callback, name](bool checked) {
        callback(name, WidgetEventBuilder::toggled(checked));
      });
      continue;
    }
    if (auto* sb = qobject_cast<QSpinBox*>(w)) {
      QObject::connect(sb, &QSpinBox::valueChanged, sb, [callback, name](int value) {
        callback(name, WidgetEventBuilder::valueChanged(value));
      });
      continue;
    }
    if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
      QObject::connect(dsb, &QDoubleSpinBox::valueChanged, dsb, [callback, name](double value) {
        callback(name, WidgetEventBuilder::valueChanged(value));
      });
      continue;
    }
    if (auto* lw = qobject_cast<QListWidget*>(w)) {
      QObject::connect(lw, &QListWidget::itemSelectionChanged, lw, [callback, name, lw]() {
        std::vector<std::string> sel;
        for (auto* item : lw->selectedItems()) {
          sel.push_back(item->text().toStdString());
        }
        callback(name, WidgetEventBuilder::selectionChanged(sel));
      });
      QObject::connect(lw, &QListWidget::itemDoubleClicked, lw, [callback, name, lw](QListWidgetItem* item) {
        callback(name, WidgetEventBuilder::itemDoubleClicked(lw->row(item)));
      });
      continue;
    }
    if (auto* tw = qobject_cast<QTableWidget*>(w)) {
      QObject::connect(tw, &QTableWidget::itemSelectionChanged, tw, [callback, name, tw]() {
        std::vector<std::string> sel;
        for (auto* item : tw->selectedItems()) {
          if (item->column() == 0) {
            sel.push_back(item->text().toStdString());
          }
        }
        callback(name, WidgetEventBuilder::selectionChanged(sel));
      });
      continue;
    }
    if (auto* btn = qobject_cast<QPushButton*>(w)) {
      // Skip buttons that are part of QDialogButtonBox
      if (qobject_cast<QDialogButtonBox*>(btn->parent())) {
        continue;
      }
      QObject::connect(
          btn, &QPushButton::clicked, btn, [callback, name]() { callback(name, WidgetEventBuilder::clicked()); });
      continue;
    }
    if (auto* tw = qobject_cast<QTabWidget*>(w)) {
      QObject::connect(tw, &QTabWidget::currentChanged, tw, [callback, name](int index) {
        callback(name, WidgetEventBuilder::tabChanged(index));
      });
      continue;
    }
  }
}

// ---------------------------------------------------------------------------
// installButtonShortcuts — create QShortcuts for buttons declaring a shortcut
// ---------------------------------------------------------------------------

void installButtonShortcuts(QWidget* root, const PJ::WidgetDataView& view) {
  for (const auto& name : view.widgetNames()) {
    auto sc = view.shortcut(name);
    if (!sc) {
      continue;
    }
    auto* btn = root->findChild<QPushButton*>(QString::fromStdString(name));
    if (!btn) {
      continue;
    }
    auto* shortcut = new QShortcut(QKeySequence(QString::fromStdString(*sc)), root);
    QObject::connect(shortcut, &QShortcut::activated, btn, &QPushButton::click);
  }
}

}  // namespace PJ
