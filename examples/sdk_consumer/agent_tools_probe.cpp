// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Install-test probe for the agent_tools component: builds the tool registry
// and runs a recipe helper through the exported CMake target.

#include <cstdio>
#include <pj_base/agent_tools/derived_recipes.hpp>
#include <pj_base/agent_tools/tool_registry.hpp>

int main() {
  const PJ::sdk::agent_tools::ToolRegistry registry;
  const auto tools = registry.toMcpToolsList();
  const std::string escaped = PJ::sdk::agent_tools::luaStringEscape("a\"b");
  std::printf("agent_tools: %zu tools, %s\n", tools.size(), escaped.c_str());
  return tools.empty() || escaped.empty() ? 1 : 0;
}
