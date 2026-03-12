#include "ui/marketplace_window.hpp"
#include "ui/extension_detail_dialog.hpp"
#include "ui_marketplace_window.h"
#include "core/ExtensionManager.h"
#include "core/RegistryManager.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSettings>
#include <QEvent>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace PJ {

static constexpr const char* kDefaultRegistryUrl =
    "https://raw.githubusercontent.com/PlotJuggler/pj-plugin-registry"
    "/refs/heads/development/registry.json";

MarketplaceWindow::MarketplaceWindow(RegistryManager* registry_mgr, ExtensionManager* ext_mgr,
                                     const QUrl& registry_url, QWidget* parent)
    : QDialog(parent), ui_(new Ui::MarketplaceWindow),
      registry_mgr_(registry_mgr), ext_mgr_(ext_mgr) {
  QSettings settings("PlotJuggler", "Marketplace");
  const QString saved = settings.value("registry_url").toString();
  registry_url_ = saved.isEmpty() ? registry_url : QUrl(saved);

  ui_->setupUi(this);
  setup_ui();
  setup_signals();
  ext_mgr_->applyPendingInstalls();
  registry_mgr_->fetchRegistry(registry_url_);
  // extensions_ is now populated via the fetchFinished signal above.
}

MarketplaceWindow::~MarketplaceWindow() {
  delete ui_;
}

// ─── UI Setup ────────────────────────────────────────────────────────────────

void MarketplaceWindow::setup_ui() {
  ui_->refresh_btn_->setFixedWidth(80);

  ui_->category_combo_->addItem("All categories", "");
  ui_->category_combo_->addItem("Data Loader",    "data_loader");
  ui_->category_combo_->addItem("Data Streamer",  "data_streamer");
  ui_->category_combo_->addItem("Parser",         "parser");
  ui_->category_combo_->addItem("Toolbox",        "toolbox");
  ui_->category_combo_->addItem("Bundle",         "bundle");

  connect(ui_->search_edit_, &QLineEdit::textChanged,
          this, &MarketplaceWindow::on_search_changed);
  connect(ui_->category_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MarketplaceWindow::on_category_changed);
  connect(ui_->refresh_btn_, &QPushButton::clicked,
          this, &MarketplaceWindow::on_refresh_clicked);
  connect(ui_->settings_btn_, &QPushButton::clicked,
          this, &MarketplaceWindow::on_settings_clicked);
}

// ─── Signal wiring ───────────────────────────────────────────────────────────

void MarketplaceWindow::setup_signals() {
  // RegistryManager
  connect(registry_mgr_, &RegistryManager::fetchStarted, this,
          [this]() { set_status("Loading registry..."); });

  connect(registry_mgr_, &RegistryManager::fetchFinished, this, [this](bool success) {
    if (!success) {
      set_status("Failed to load registry", true);
      return;
    }

   connect(ext_mgr_, &ExtensionManager::installPendingRestart, this,
        [this](const QString& id) {
          pending_restart_ids_.insert(id);
          ui_->progress_bar_->setVisible(false);
          populate_cards();
          set_status("Extension staged — will be active after restart");
        });
    extensions_ = registry_mgr_->extensions();
    apply_filters();
    set_status("Ready — " + QString::number(extensions_.size()) + " extensions loaded");
  });

  connect(registry_mgr_, &RegistryManager::fetchError, this,
          [this](const QString& error) { set_status("Registry error: " + error, true); });

  // ExtensionManager
  connect(ext_mgr_, &ExtensionManager::installStarted, this, [this](const QString& id) {
    ui_->progress_bar_->setValue(0);
    ui_->progress_bar_->setRange(0, 100);
    ui_->progress_bar_->setVisible(true);
    for (const auto& ext : extensions_)
      if (ext.id == id) { set_status("Installing " + ext.name + "..."); break; }
  });

  connect(ext_mgr_, &ExtensionManager::installProgress, this,
          [this](const QString& /*id*/, int percent) {
            ui_->progress_bar_->setValue(percent);
          });

  connect(ext_mgr_, &ExtensionManager::installFinished, this,
          [this](const QString& id, bool success) {
            ui_->progress_bar_->setVisible(false);
            if (success) installations_changed_ = true;
            populate_cards();
            if (success) {
              for (const auto& ext : extensions_)
                if (ext.id == id) {
                  set_status("Installed " + ext.name + " v" + ext.version);
                  break;
                }
            }
            // On failure the status was already set by installError — do not overwrite it.
          });

  connect(ext_mgr_, &ExtensionManager::installError, this,
          [this](const QString& /*id*/, const QString& error) {
            ui_->progress_bar_->setVisible(false);
            set_status("Installation failed: " + error, true);
          });

  connect(ext_mgr_, &ExtensionManager::uninstallFinished, this,
          [this](const QString& id, bool success) {
            if (success) {
              installations_changed_ = true;
              populate_cards();
              for (const auto& ext : extensions_)
                if (ext.id == id) { set_status("Uninstalled " + ext.name); break; }
            }
            // On failure the status was already set by uninstallError — do not overwrite it.
          });

  connect(ext_mgr_, &ExtensionManager::uninstallError, this,
          [this](const QString& /*id*/, const QString& error) {
            set_status("Uninstall failed: " + error, true);
          });
}

// ─── Cards Population ─────────────────────────────────────────────────────────

void MarketplaceWindow::populate_cards() {
  while (ui_->cards_layout_->count() > 1)
    delete ui_->cards_layout_->takeAt(0)->widget();

  for (const Extension& ext : filtered_) {
    const QString ext_id = ext.id;

    auto* card = new QFrame(ui_->cards_container);
    card->setFrameShape(QFrame::NoFrame);
    card->setProperty("ext_id", ext_id);
    card->setToolTip(ext.description);
    card->setCursor(Qt::PointingHandCursor);
    card->setObjectName("extCard");
    card->installEventFilter(this);
    card->setStyleSheet(
        "QFrame#extCard { background-color: palette(base);"
        "                 border: 1px solid palette(mid);"
        "                 border-radius: 6px; }"
        "QFrame#extCard:hover { background-color: palette(alternate-base);"
        "                       border-color: palette(shadow); }");

    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(10, 8, 10, 8);
    card_layout->setSpacing(4);

    auto* top_row = new QHBoxLayout();

    auto* name_lbl = new QLabel(ext.name, card);
    QFont f = name_lbl->font();
    f.setBold(true);
    name_lbl->setFont(f);

    QString version_text = ext.version;
    if (ext_mgr_->hasUpdate(ext)) {
      const auto installed = ext_mgr_->installedExtensions();
      if (installed.contains(ext.id))
        version_text = installed[ext.id].version + " \u2192 " + ext.version;
    }
    auto* version_lbl = new QLabel(version_text, card);
    version_lbl->setStyleSheet("color: palette(mid);");

    auto* btn_box = new QHBoxLayout();
    btn_box->setSpacing(6);

    if (pending_restart_ids_.contains(ext.id)) {
      auto* badge = new QPushButton("Needs Restart", card);
      badge->setFixedWidth(90);
      badge->setEnabled(false);
      badge->setStyleSheet(
          "QPushButton:disabled { background:#e6a817; color:white; border:none;"
          "  border-radius:4px; padding:4px 0px; font-weight:bold; }");
      btn_box->addWidget(badge);
    } else if (ext_mgr_->hasUpdate(ext)) {
      auto* btn = new QPushButton("Update \u2B06", card);
      btn->setFixedWidth(90);
      btn->setStyleSheet(
          "QPushButton { background:#e6a817; color:white; border:none;"
          "  border-radius:4px; padding:4px 0px; font-weight:bold; }"
          "QPushButton:hover { background:#f0b820; }");
      connect(btn, &QPushButton::clicked, this,
              [this, ext_id]() { on_action_button_clicked(ext_id); });
      btn_box->addWidget(btn);
    } else if (ext_mgr_->isInstalled(ext.id)) {
      auto* badge = new QPushButton("Installed", card);
      badge->setFixedWidth(90);
      badge->setEnabled(false);
      badge->setStyleSheet(
          "QPushButton:disabled { background:#4caf6e; color:white; border:none;"
          "  border-radius:4px; padding:4px 0px; font-weight:bold; }");
      btn_box->addWidget(badge);
    } else {
      auto* btn = new QPushButton("Install", card);
      btn->setFixedWidth(90);
      btn->setStyleSheet(
          "QPushButton { background:#2196f3; color:white; border:none;"
          "  border-radius:4px; padding:4px 0px; font-weight:bold; }"
          "QPushButton:hover { background:#42a5f5; }");
      connect(btn, &QPushButton::clicked, this,
              [this, ext_id]() { on_action_button_clicked(ext_id); });
      btn_box->addWidget(btn);
    }

    top_row->addWidget(name_lbl);
    top_row->addStretch();
    top_row->addWidget(version_lbl);
    card_layout->addLayout(top_row);

    auto* bottom_row = new QHBoxLayout();
    auto* desc_lbl = new QLabel(card);
    desc_lbl->setStyleSheet("color: palette(mid); font-size: 11px;");
    QFontMetrics fm(desc_lbl->font());
    desc_lbl->setText(fm.elidedText(ext.description, Qt::ElideRight, 400));
    bottom_row->addWidget(desc_lbl);
    bottom_row->addStretch();
    bottom_row->addLayout(btn_box);
    card_layout->addLayout(bottom_row);

    ui_->cards_layout_->insertWidget(ui_->cards_layout_->count() - 1, card);
  }
}

// ─── Event Filter (double-click on card) ─────────────────────────────────────

bool MarketplaceWindow::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::MouseButtonDblClick) {
    const QString ext_id = static_cast<QFrame*>(obj)->property("ext_id").toString();
    if (!ext_id.isEmpty()) open_detail(ext_id);
    return true;
  }
  return QDialog::eventFilter(obj, event);
}

void MarketplaceWindow::open_detail(const QString& ext_id) {
  for (const auto& ext : filtered_) {
    if (ext.id != ext_id) continue;
    const auto installed = ext_mgr_->installedExtensions();
    const QString installed_version =
        installed.contains(ext_id) ? installed[ext_id].version : QString{};
    ExtensionDetailDialog dlg(ext, installed_version, this);
    connect(&dlg, &ExtensionDetailDialog::install_requested, this,
            [this, ext_id]() { on_action_button_clicked(ext_id); });
    connect(&dlg, &ExtensionDetailDialog::uninstall_requested, this,
            [this, ext_id]() { on_uninstall_button_clicked(ext_id); });
    dlg.exec();
    return;
  }
}

// ─── Filtering ────────────────────────────────────────────────────────────────

void MarketplaceWindow::apply_filters() {
  const QString search   = ui_->search_edit_->text().toLower();
  const QString category = ui_->category_combo_->currentData().toString();

  filtered_.clear();
  for (const auto& ext : extensions_) {
    if (!category.isEmpty() && ext.category != category) continue;
    if (!search.isEmpty()) {
      bool match = ext.name.toLower().contains(search) ||
                   ext.description.toLower().contains(search);
      if (!match)
        for (const auto& tag : ext.tags)
          if (tag.toLower().contains(search)) { match = true; break; }
      if (!match) continue;
    }
    filtered_.append(ext);
  }

  populate_cards();
  set_status(QString::number(filtered_.size()) + " of " +
             QString::number(extensions_.size()) + " extensions shown");
}

void MarketplaceWindow::set_status(const QString& msg, bool is_error) {
  ui_->status_label_->setText(msg);
  ui_->status_label_->setStyleSheet(is_error ? "color: #d32f2f; font-weight: bold;" : "");
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void MarketplaceWindow::on_search_changed(const QString& /*text*/) { apply_filters(); }
void MarketplaceWindow::on_category_changed(int /*index*/)         { apply_filters(); }

void MarketplaceWindow::on_refresh_clicked() {
  set_status("Refreshing...");
  registry_mgr_->fetchRegistry(registry_url_);
}

void MarketplaceWindow::on_settings_clicked() {
  QDialog dlg(this);
  dlg.setWindowTitle("Registry Settings");
  dlg.setMinimumWidth(480);

  auto* layout = new QFormLayout(&dlg);
  auto* url_edit = new QLineEdit(registry_url_.toString(), &dlg);
  url_edit->setPlaceholderText(kDefaultRegistryUrl);
  layout->addRow("Registry URL:", url_edit);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  layout->addRow(buttons);

  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  const QUrl new_url(url_edit->text().trimmed());
  if (new_url == registry_url_) {
    return;
  }

  registry_url_ = new_url;
  QSettings("PlotJuggler", "Marketplace").setValue("registry_url", registry_url_.toString());

  set_status("Refreshing...");
  registry_mgr_->fetchRegistry(registry_url_);
}

void MarketplaceWindow::on_action_button_clicked(const QString& ext_id) {
  for (const auto& ext : filtered_) {
    if (ext.id != ext_id) continue;
    if (ext_mgr_->hasUpdate(ext))
      ext_mgr_->update(ext);
    else if (!ext_mgr_->isInstalled(ext.id))
      ext_mgr_->install(ext);
    return;
  }
}

void MarketplaceWindow::on_uninstall_button_clicked(const QString& ext_id) {
  ext_mgr_->uninstall(ext_id);
}

}  // namespace PJ
