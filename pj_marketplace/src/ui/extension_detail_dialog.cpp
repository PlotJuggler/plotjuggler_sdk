#include "extension_detail_dialog.hpp"
#include "ui_extension_detail_dialog.h"

#include <QDesktopServices>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QUrl>

namespace PJ {

ExtensionDetailDialog::ExtensionDetailDialog(const Extension& ext, const QString& installed_version,
                                             QWidget* parent)
    : QDialog(parent), ui_(new Ui::ExtensionDetailDialog) {
  ui_->setupUi(this);
  setWindowTitle(ext.name + " — Details");

  // ── Title ──────────────────────────────────────────────────────────────────
  ui_->title_lbl->setText(ext.name + "  v" + ext.version);
  QFont title_font = ui_->title_lbl->font();
  title_font.setPointSize(title_font.pointSize() + 4);
  title_font.setBold(true);
  ui_->title_lbl->setFont(title_font);

  // ── Metadata row ───────────────────────────────────────────────────────────
  QStringList meta;
  if (!ext.publisher.isEmpty())              meta << ext.publisher;
  if (!ext.category.isEmpty())               meta << ext.category;
  if (!ext.license.isEmpty())                meta << ext.license;
  if (!ext.min_plotjuggler_version.isEmpty())
    meta << "requires PJ " + ext.min_plotjuggler_version + "+";
  if (!installed_version.isEmpty())
    meta << "installed: v" + installed_version;
  ui_->meta_lbl->setText(meta.join("  \u2022  "));

  // ── Tags (chips) ── dynamic: created per extension ────────────────────────
  for (int i = 0; i < ext.tags.size(); ++i) {
    auto* chip = new QLabel(ext.tags[i], ui_->tags_container);
    chip->setStyleSheet(
        "QLabel { background: palette(alternate-base);"
        "         border: 1px solid palette(mid);"
        "         border-radius: 3px;"
        "         padding: 1px 6px;"
        "         font-size: 11px; }");
    ui_->tags_layout->insertWidget(i, chip);
  }

  // ── Description ────────────────────────────────────────────────────────────
  ui_->desc_lbl->setText(ext.description);

  // ── Changelog ──────────────────────────────────────────────────────────────
  QString changelog_html;
  const auto keys = ext.changelog.keys();
  for (int i = static_cast<int>(keys.size()) - 1; i >= 0; --i) {
    const QString& ver = keys[i];
    changelog_html += "<b>v" + ver + "</b><br/>";
    changelog_html += ext.changelog[ver] + "<br/><br/>";
  }
  ui_->changelog_browser->setHtml(changelog_html);

  // ── Buttons ── state-dependent visibility and style ────────────────────────
  const bool installed  = !installed_version.isEmpty();
  const bool has_update = installed && installed_version != ext.version;

  ui_->github_btn->setEnabled(!ext.website.isEmpty());
  const QString website = ext.website;
  connect(ui_->github_btn, &QPushButton::clicked, this,
          [website]() { if (!website.isEmpty()) QDesktopServices::openUrl(QUrl(website)); });

  if (!installed || has_update) {
    const QString lbl   = has_update ? "Update \u2B06" : "Install";
    const QString style = has_update
        ? "QPushButton { background:#e6a817; color:white; border:none; border-radius:4px; padding:4px 14px; }"
          "QPushButton:hover { background:#f0b82a; }"
        : "QPushButton { background:#2196f3; color:white; border:none; border-radius:4px; padding:4px 14px; }"
          "QPushButton:hover { background:#42a5f5; }";
    ui_->action_btn->setText(lbl);
    ui_->action_btn->setStyleSheet(style);
    ui_->action_btn->setVisible(true);
    connect(ui_->action_btn, &QPushButton::clicked, this, [this]() {
      emit install_requested();
      accept();
    });
  }

  if (installed) {
    ui_->uninstall_btn->setVisible(true);
    connect(ui_->uninstall_btn, &QPushButton::clicked, this, [this]() {
      emit uninstall_requested();
      accept();
    });
  }

  connect(ui_->close_btn, &QPushButton::clicked, this, &QDialog::accept);
}

ExtensionDetailDialog::~ExtensionDetailDialog() {
  delete ui_;
}

}  // namespace PJ
