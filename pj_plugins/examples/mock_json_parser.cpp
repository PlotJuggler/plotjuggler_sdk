// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <string_view>

#include "pj_base/number_parse.hpp"
#include "pj_plugins/sdk/message_parser_plugin_base.hpp"

namespace {

/// Minimal parser that treats each payload as a text-encoded double,
/// writing one "value" field per parse() call.
class MockJsonParser : public PJ::MessageParserPluginBase {
 public:
  PJ::Status parse(PJ::Timestamp timestamp_ns, PJ::Span<const uint8_t> payload) override {
    if (!writeHostBound()) {
      return PJ::unexpected("write host not bound");
    }
    const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
    const auto value = PJ::parseNumber<double>(text);
    if (!value.has_value()) {
      return PJ::unexpected("payload is not a valid number");
    }
    return writeHost().appendRecord(timestamp_ns, {{.name = "value", .value = *value}});
  }
};

}  // namespace

PJ_MESSAGE_PARSER_PLUGIN(
    MockJsonParser, R"({"id":"mock-json-parser","name":"Mock JSON Parser","version":"1.0.0","encoding":["json"]})")
