#include "plugin_registry.hpp"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <vector>

#include "pj_base/diagnostic_sink.hpp"

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

TEST(PluginRegistryTest, MessageParserManifestAcceptsStringEncoding) {
  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());

  const QString plugin_dir = temp_dir.filePath("plugins");
  ASSERT_TRUE(QDir().mkpath(plugin_dir));

  const QString dst = plugin_dir + "/" + QFileInfo(QStringLiteral(PJ_MOCK_JSON_PARSER_PLUGIN_PATH)).fileName();
  ASSERT_TRUE(QFile::copy(QStringLiteral(PJ_MOCK_JSON_PARSER_PLUGIN_PATH), dst));

  PluginRegistry registry(plugin_dir.toStdString());
  registry.scanDirectory();

  const auto* parser = registry.findParserByEncoding("json");
  ASSERT_NE(parser, nullptr);
  EXPECT_EQ(parser->id, "mock-json-parser");
  EXPECT_EQ(parser->version, "1.0.0");
  EXPECT_EQ(registry.listAvailableEncodings(), "[\"json\"]");

  const auto snapshot = registry.loadedExtensionsSnapshot();
  ASSERT_TRUE(snapshot.contains("mock-json-parser"));
  EXPECT_EQ(snapshot["mock-json-parser"].version, "1.0.0");
}

// A capturing sink confirms diagnostics flow out of PluginRegistry — the GUI
// path depends on this contract, and a regression here would silently break
// every error message the user normally sees in the status bar.
TEST(PluginRegistryTest, ScanDirectoryEmitsErrorDiagnosticForBrokenDso) {
  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());

  const QString plugin_dir = temp_dir.filePath("plugins");
  ASSERT_TRUE(QDir().mkpath(plugin_dir));

  // A junk file with the right suffix exercises the DSO load failure path.
  const std::string ext = ".so";
  const QString broken = plugin_dir + "/not_a_real_plugin" + QString::fromStdString(ext);
  QFile junk(broken);
  ASSERT_TRUE(junk.open(QIODevice::WriteOnly));
  junk.write("not a shared library");
  junk.close();

  std::vector<PJ::Diagnostic> captured;
  PJ::DiagnosticSink sink = [&captured](const PJ::Diagnostic& d) { captured.push_back(d); };

  PluginRegistry registry(plugin_dir.toStdString(), sink);
  registry.scanDirectory();

  const bool any_error = std::any_of(captured.begin(), captured.end(), [](const PJ::Diagnostic& d) {
    return d.level == PJ::DiagnosticLevel::kError && d.source == "PluginRegistry"
        && d.message.find("not_a_real_plugin") != std::string::npos;
  });
  EXPECT_TRUE(any_error) << "expected a kError diagnostic mentioning the broken DSO";
}

TEST(PluginRegistryTest, MissingPluginDirectoryEmitsErrorDiagnostic) {
  std::vector<PJ::Diagnostic> captured;
  PJ::DiagnosticSink sink = [&captured](const PJ::Diagnostic& d) { captured.push_back(d); };

  PluginRegistry registry("/nonexistent/path/that/does/not/exist", sink);
  registry.scanDirectory();

  ASSERT_FALSE(captured.empty());
  EXPECT_EQ(captured.front().level, PJ::DiagnosticLevel::kError);
  EXPECT_NE(captured.front().message.find("not found"), std::string::npos);
}

}  // namespace proto
