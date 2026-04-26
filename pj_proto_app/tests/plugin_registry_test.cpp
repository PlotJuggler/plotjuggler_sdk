#include "plugin_registry.hpp"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace proto {

TEST(PluginRegistryTest, LoadedExtensionsSnapshotUsesLoadedManifestVersion) {
  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());

  const QString plugin_dir = temp_dir.filePath("plugins");
  ASSERT_TRUE(QDir().mkpath(plugin_dir));

  const QString dst = plugin_dir + "/" + QFileInfo(QStringLiteral(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH)).fileName();
  ASSERT_TRUE(QFile::copy(QStringLiteral(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH), dst));

  PluginRegistry registry(plugin_dir.toStdString());
  registry.scanDirectory();

  const auto snapshot = registry.loadedExtensionsSnapshot();
  ASSERT_TRUE(snapshot.contains("mock-data-source"));
  EXPECT_EQ(snapshot["mock-data-source"].version, "1.0.0");
}

}  // namespace proto
