#include "preferences_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>

namespace proto {

PreferencesDialog::PreferencesDialog(const QString& builtin_plugin_dir, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle("Preferences");
  resize(560, 520);

  QSettings settings;

  auto* tabs = new QTabWidget(this);

  // ================================================================
  // Tab 1: Appearance
  // ================================================================
  {
    auto* page = new QWidget();
    auto* form = new QFormLayout(page);
    form->setContentsMargins(12, 12, 12, 12);
    form->setVerticalSpacing(10);

    auto* combo_theme = new QComboBox();
    combo_theme->addItems({"Light Theme", "Dark Theme"});
    combo_theme->setCurrentText(settings.value("Appearance/theme", "Light Theme").toString());
    form->addRow("Theme:", combo_theme);

    auto* chk_tree = new QCheckBox("enabled (using separator \"/\" in the name)");
    chk_tree->setChecked(settings.value("Appearance/treeViewEnabled", true).toBool());
    form->addRow("Tree view:", chk_tree);

    auto* chk_opengl = new QCheckBox("enabled");
    chk_opengl->setChecked(settings.value("Appearance/openGLEnabled", true).toBool());
    form->addRow("OpenGL:", chk_opengl);

    auto* combo_precision = new QComboBox();
    for (int i = 1; i <= 9; ++i) combo_precision->addItem(QString::number(i));
    combo_precision->setCurrentIndex(settings.value("Appearance/floatPrecision", 3).toInt() - 1);
    combo_precision->setFixedWidth(60);
    form->addRow("Float Precision:", combo_precision);

    auto* chk_splash = new QCheckBox("enabled");
    chk_splash->setChecked(settings.value("Appearance/skipSplash", false).toBool());
    form->addRow("Skip Splash Screen on Startup:", chk_splash);

    tabs->addTab(page, "Appearance");

    connect(this, &QDialog::accepted, this, [=]() {
      QSettings s;
      s.setValue("Appearance/theme", combo_theme->currentText());
      s.setValue("Appearance/treeViewEnabled", chk_tree->isChecked());
      s.setValue("Appearance/openGLEnabled", chk_opengl->isChecked());
      s.setValue("Appearance/floatPrecision", combo_precision->currentIndex() + 1);
      s.setValue("Appearance/skipSplash", chk_splash->isChecked());

      // Apply theme immediately
      if (combo_theme->currentText() == "Dark Theme") {
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(53, 53, 53));
        dark.setColor(QPalette::WindowText, Qt::white);
        dark.setColor(QPalette::Base, QColor(35, 35, 35));
        dark.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        dark.setColor(QPalette::ToolTipBase, QColor(25, 25, 25));
        dark.setColor(QPalette::ToolTipText, Qt::white);
        dark.setColor(QPalette::Text, Qt::white);
        dark.setColor(QPalette::Button, QColor(53, 53, 53));
        dark.setColor(QPalette::ButtonText, Qt::white);
        dark.setColor(QPalette::BrightText, Qt::red);
        dark.setColor(QPalette::Highlight, QColor(42, 130, 218));
        dark.setColor(QPalette::HighlightedText, Qt::black);
        qApp->setPalette(dark);
      } else {
        qApp->setPalette(qApp->style()->standardPalette());
      }
    });
  }

  // ================================================================
  // Tab 2: Behavior
  // ================================================================
  {
    auto* page = new QWidget();
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(6);

    // Reset plot zoom
    vbox->addWidget(new QLabel("Reset plot zoom when:"));
    auto* chk_zoom_curve = new QCheckBox("  Curve added to plot");
    auto* chk_zoom_vis = new QCheckBox("  Curve visibility toggled (in curves legend)");
    auto* chk_zoom_filter = new QCheckBox("  Filters applied to curves");
    chk_zoom_curve->setChecked(settings.value("Behavior/zoomOnCurveAdded", true).toBool());
    chk_zoom_vis->setChecked(settings.value("Behavior/zoomOnVisibilityToggled", true).toBool());
    chk_zoom_filter->setChecked(settings.value("Behavior/zoomOnFilterApplied", true).toBool());
    vbox->addWidget(chk_zoom_curve);
    vbox->addWidget(chk_zoom_vis);
    vbox->addWidget(chk_zoom_filter);

    // Curves color
    vbox->addSpacing(6);
    vbox->addWidget(new QLabel("Curves color:"));
    auto* radio_global = new QRadioButton("  global color sequence");
    auto* radio_reset = new QRadioButton("  reset color sequence in each plot area");
    auto* radio_remember = new QRadioButton("  remember color");
    int color_mode = settings.value("Behavior/curvesColorMode", 0).toInt();
    radio_global->setChecked(color_mode == 0);
    radio_reset->setChecked(color_mode == 1);
    radio_remember->setChecked(color_mode == 2);
    vbox->addWidget(radio_global);
    vbox->addWidget(radio_reset);
    vbox->addWidget(radio_remember);

    // Export Plots
    vbox->addSpacing(6);
    vbox->addWidget(new QLabel("Export Plots:"));
    auto* form_export = new QFormLayout();
    auto* spin_w = new QSpinBox();
    spin_w->setRange(320, 7680);
    spin_w->setValue(settings.value("Behavior/exportWidth", 1920).toInt());
    auto* spin_h = new QSpinBox();
    spin_h->setRange(240, 4320);
    spin_h->setValue(settings.value("Behavior/exportHeight", 1200).toInt());
    form_export->addRow("  Width", spin_w);
    form_export->addRow("  Height", spin_h);
    vbox->addLayout(form_export);

    // Mouse Interaction
    vbox->addSpacing(6);
    vbox->addWidget(new QLabel("Mouse Interaction:"));
    auto* chk_swap_mouse = new QCheckBox("  Swap pan/zoom mouse action modifiers");
    chk_swap_mouse->setChecked(settings.value("Behavior/swapMouseModifiers", false).toBool());
    vbox->addWidget(chk_swap_mouse);

    // Parsing
    vbox->addSpacing(6);
    vbox->addWidget(new QLabel("Parsing"));
    auto* chk_strict = new QCheckBox("  Strict Truncation check (uncheck at your own risk)");
    chk_strict->setChecked(settings.value("Behavior/strictTruncation", true).toBool());
    vbox->addWidget(chk_strict);

    vbox->addStretch();
    tabs->addTab(page, "Behavior");

    connect(this, &QDialog::accepted, this, [=]() {
      QSettings s;
      s.setValue("Behavior/zoomOnCurveAdded", chk_zoom_curve->isChecked());
      s.setValue("Behavior/zoomOnVisibilityToggled", chk_zoom_vis->isChecked());
      s.setValue("Behavior/zoomOnFilterApplied", chk_zoom_filter->isChecked());
      int mode = radio_global->isChecked() ? 0 : radio_reset->isChecked() ? 1 : 2;
      s.setValue("Behavior/curvesColorMode", mode);
      s.setValue("Behavior/exportWidth", spin_w->value());
      s.setValue("Behavior/exportHeight", spin_h->value());
      s.setValue("Behavior/swapMouseModifiers", chk_swap_mouse->isChecked());
      s.setValue("Behavior/strictTruncation", chk_strict->isChecked());
    });
  }

  // ================================================================
  // Tab 3: Plugins
  // ================================================================
  {
    auto* page = new QWidget();
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(6);

    // Header row with +/trash buttons
    auto* hdr = new QHBoxLayout();
    hdr->addWidget(new QLabel("<b>Plugin folders</b> (will be loaded in this order):"));
    hdr->addStretch();
    auto* btn_add = new QPushButton("+");
    btn_add->setFixedSize(24, 24);
    auto* btn_remove =
        new QPushButton(QApplication::style()->standardIcon(QStyle::SP_TrashIcon), QString{});
    btn_remove->setFixedSize(24, 24);
    hdr->addWidget(btn_add);
    hdr->addWidget(btn_remove);
    vbox->addLayout(hdr);

    vbox->addWidget(new QLabel("Add custom folders. Drag and drop the items to change the order."));

    auto* list_custom = new QListWidget();
    list_custom->setDragDropMode(QAbstractItemView::InternalMove);
    list_custom->setSelectionMode(QAbstractItemView::SingleSelection);
    const QStringList custom_dirs = settings.value("Plugins/customFolders").toStringList();
    list_custom->addItems(custom_dirs);
    vbox->addWidget(list_custom);

    vbox->addWidget(new QLabel("List of built-in folders:"));
    auto* txt_builtin = new QPlainTextEdit(builtin_plugin_dir);
    txt_builtin->setReadOnly(true);
    txt_builtin->setMaximumHeight(100);
    vbox->addWidget(txt_builtin);

    vbox->addWidget(
        new QLabel("<i>Note: this change will take effect the next time the app is started</i>"));

    connect(btn_add, &QPushButton::clicked, this, [this, list_custom]() {
      auto dir = QFileDialog::getExistingDirectory(this, "Select Plugin Folder");
      if (!dir.isEmpty()) list_custom->addItem(dir);
    });
    connect(btn_remove, &QPushButton::clicked, this,
            [list_custom]() { delete list_custom->currentItem(); });

    tabs->addTab(page, "Plugins");

    connect(this, &QDialog::accepted, this, [=]() {
      QStringList dirs;
      for (int i = 0; i < list_custom->count(); ++i) dirs << list_custom->item(i)->text();
      QSettings s;
      s.setValue("Plugins/customFolders", dirs);
    });
  }

  // ================================================================
  // Dialog buttons
  // ================================================================
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* main_layout = new QVBoxLayout(this);
  main_layout->addWidget(tabs);
  main_layout->addWidget(buttons);
}

}  // namespace proto
