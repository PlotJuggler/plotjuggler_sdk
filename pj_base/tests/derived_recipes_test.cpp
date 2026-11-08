// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT
//
// The pure helpers of the shared recipe library. Path resolution against a catalog is covered by
// the assistant's tool_registry tests (they drive it through the same functions); this file
// pins the text and parsing contracts the assistant relies on.
#include "pj_base/agent_tools/derived_recipes.hpp"

#include <gtest/gtest.h>

#include <sstream>

using namespace PJ::sdk::agent_tools;

TEST(DerivedRecipes, ParseTypedOutputsSplitsNameAndType) {
  const auto parsed = parseTypedOutputs(nlohmann::json::array({"cropped:kPointCloud", "count:number"}));
  ASSERT_TRUE(parsed.error.empty()) << parsed.error;
  ASSERT_EQ(parsed.outputs.size(), 2u);
  EXPECT_EQ(parsed.outputs[0].name, "cropped");
  EXPECT_EQ(parsed.outputs[0].type, "kPointCloud");
  EXPECT_EQ(parsed.outputs[1].type, "number");
}

TEST(DerivedRecipes, ParseTypedOutputsRejectsMalformedEntries) {
  EXPECT_FALSE(parseTypedOutputs(nlohmann::json::array()).error.empty());
  EXPECT_FALSE(parseTypedOutputs(nlohmann::json::array({"noType"})).error.empty());
  EXPECT_FALSE(parseTypedOutputs(nlohmann::json::array({":number"})).error.empty());
  EXPECT_FALSE(parseTypedOutputs(nlohmann::json::array({"name:"})).error.empty());
  EXPECT_FALSE(parseTypedOutputs(nlohmann::json::array({1})).error.empty());
}

TEST(DerivedRecipes, LuaStringEscapeProtectsTheLiteral) {
  EXPECT_EQ(luaStringEscape("a\"b\\c\nd\re\tf"), "a\\\"b\\\\c\\nd\\re\\tf");
}

TEST(DerivedRecipes, OnDemandChunkBindsInputsAndParamsFromVarargs) {
  const std::string chunk = buildOnDemandChunk("return {}");
  EXPECT_EQ(chunk, "-- pj-script: luau\nlocal inputs, params = ...\nreturn {}\n");
}

TEST(DerivedRecipes, ResolvedChunkAliasesEveryScriptKeyToTheQualifiedRequestKey) {
  ResolvedEvalInputs resolved;
  resolved.aliases = {{"/cloud", "run1:/cloud"}, {"run1:/cloud", "run1:/cloud"}};
  const std::string chunk = buildResolvedOnDemandChunk("return {}", resolved).script;
  EXPECT_NE(chunk.find("[\"/cloud\"] = inputs[\"run1:/cloud\"]"), std::string::npos) << chunk;
  EXPECT_NE(chunk.find("local inputs, params = ...\ninputs = {"), std::string::npos) << chunk;
  EXPECT_NE(chunk.find("return {}"), std::string::npos);
}

TEST(DerivedRecipes, LuauTransformKeepsTheValueAndExtraInputParameters) {
  const std::string script = buildTransformScript("id", "name", "g = 1", "return value + v1", 1, "luau").script;
  EXPECT_NE(script.find("return function(time, value, v1)"), std::string::npos) << script;
  EXPECT_NE(script.find("id = \"id\""), std::string::npos);
}

TEST(DerivedRecipes, ToRawNsIsEmptyWithoutAPlaybackView) {
  PJ::sdk::PlaybackHostView unbound;
  EXPECT_FALSE(toRawNs(unbound, PJ::sdk::DataSourceHandle{}, 1.0).has_value());
}

TEST(DerivedRecipes, QualifyAndJoinHelpers) {
  EXPECT_EQ(qualifyWithDataset("", "a/b"), "a/b");
  EXPECT_EQ(qualifyWithDataset("run1", "a/b"), "run1:a/b");
  EXPECT_EQ(joinSeriesPath("topic", "/field"), "topic/field");
  EXPECT_EQ(joinSeriesPath("topic", "field"), "topic/field");
  EXPECT_EQ(canonicalSeriesPath("a//b///c"), "a/b/c");
  EXPECT_TRUE(isMarkerObjectTopic("__markers__/x"));
  EXPECT_FALSE(isMarkerObjectTopic("/cloud"));
}

// --- Python on-demand chunk ---

TEST(DerivedRecipes, PythonChunkDefinesEvaluateWithIndentedBody) {
  ResolvedEvalInputs resolved;
  const std::string chunk = buildOnDemandChunkPython("x = 1\n\nreturn {'a': x}", resolved).script;
  EXPECT_EQ(chunk, "# pj-script: python\ndef evaluate(inputs, params):\n    x = 1\n\n    return {'a': x}\n");
}

TEST(DerivedRecipes, PythonChunkAliasesInputsAndKeepsAnEmptyBodyValid) {
  ResolvedEvalInputs resolved;
  resolved.aliases = {{"/cloud", "run1:/cloud"}};
  const std::string chunk = buildOnDemandChunkPython("", resolved).script;
  EXPECT_NE(chunk.find("def evaluate(inputs, params):\n    inputs = {\n"), std::string::npos) << chunk;
  EXPECT_NE(chunk.find("\"/cloud\": inputs[\"run1:/cloud\"],"), std::string::npos) << chunk;
  EXPECT_EQ(chunk.find("\n    pass"), std::string::npos) << "the alias table is already a statement";
  EXPECT_NE(buildOnDemandChunkPython("  \n", ResolvedEvalInputs{}).script.find("    pass\n"), std::string::npos);
}

namespace {

// The 1-based number of the first line of `script` that is exactly `line`.
int lineOf(const std::string& script, const std::string& line) {
  std::istringstream in(script);
  std::string text;
  for (int n = 1; std::getline(in, text); ++n) {
    if (text == line) {
      return n;
    }
  }
  return -1;
}

}  // namespace

// --- Script layout ---

TEST(DerivedRecipes, TransformLayoutPointsAtTheBodyAndTheGlobals) {
  const BuiltScript luau = buildTransformScript("id", "name", "g = 1\nh = 2", "local y = value\nreturn y", 1, "luau");
  EXPECT_EQ(luau.script.rfind("-- pj-script: luau\n", 0), 0u);
  EXPECT_EQ(luau.layout.globals_first_line, lineOf(luau.script, "g = 1"));
  EXPECT_EQ(luau.layout.globals_lines, 2);
  EXPECT_EQ(luau.layout.body_first_line, lineOf(luau.script, "local y = value"));
  EXPECT_EQ(luau.layout.body_lines, 2);
  const BuiltScript no_globals = buildTransformScript("id", "name", "", "return value", 0, "luau");
  EXPECT_EQ(no_globals.layout.globals_lines, 0);
  EXPECT_EQ(no_globals.layout.body_first_line, lineOf(no_globals.script, "return value"));

  const BuiltScript python = buildTransformScript("id", "name", "g = 1", "y = value\nreturn y", 0, "python");
  EXPECT_EQ(python.layout.globals_first_line, lineOf(python.script, "g = 1"));
  EXPECT_EQ(python.layout.body_first_line, lineOf(python.script, "    y = value"));
  const BuiltScript python_plain = buildTransformScript("id", "name", "", "return value", 0, "python");
  EXPECT_EQ(python_plain.layout.body_first_line, lineOf(python_plain.script, "    return value"));
  const BuiltScript python_empty = buildTransformScript("id", "name", "", "", 0, "python");
  EXPECT_EQ(python_empty.layout.body_first_line, lineOf(python_empty.script, "    return value"));
}

TEST(DerivedRecipes, TransformScriptEscapesTheIdAndName) {
  const BuiltScript built = buildTransformScript("a\"b", "n\nm", "", "return value", 0, "luau");
  EXPECT_NE(built.script.find("id = \"a\\\"b\""), std::string::npos) << built.script;
  EXPECT_NE(built.script.find("name = \"n\\nm\""), std::string::npos) << built.script;
}

TEST(DerivedRecipes, OnDemandChunksReportWhereTheBodyStarts) {
  ResolvedEvalInputs resolved;
  resolved.aliases = {{"/cloud", "run1:/cloud"}, {"run1:/cloud", "run1:/cloud"}};
  const BuiltScript luau = buildResolvedOnDemandChunk("local a = 1\nreturn {a}", resolved);
  EXPECT_EQ(luau.layout.body_first_line, lineOf(luau.script, "local a = 1"));
  EXPECT_EQ(luau.layout.body_lines, 2);
  const BuiltScript python = buildOnDemandChunkPython("a = 1\nreturn {'a': a}", resolved);
  EXPECT_EQ(python.layout.body_first_line, lineOf(python.script, "    a = 1"));
  EXPECT_EQ(python.layout.body_lines, 2);
  const BuiltScript bare = buildResolvedOnDemandChunk("return {}", ResolvedEvalInputs{});
  EXPECT_EQ(bare.layout.body_first_line, lineOf(bare.script, "return {}"));
}
