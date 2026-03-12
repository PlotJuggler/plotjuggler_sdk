#include "pj_base/sdk/message_parser_plugin_base.hpp"

#include <cstdlib>
#include <string>

namespace {

/// Minimal parser that treats each payload as a text-encoded double,
/// writing one "value" field per parse() call.
class MockJsonParser : public PJ::MessageParserPluginBase {
 public:
  PJ::Status parse(PJ::Timestamp timestamp_ns, PJ::Span<const uint8_t> payload) override {
    if (!writeHostBound()) {
      return PJ::unexpected(std::string("write host not bound"));
    }
    std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
    double value = std::strtod(text.c_str(), nullptr);

    return writeHost().appendRecord(timestamp_ns, {{.name = "value", .value = value}});
  }
};

}  // namespace

PJ_MESSAGE_PARSER_PLUGIN(MockJsonParser,
                         R"({"name":"Mock JSON Parser","version":"1.0.0","encoding":"json"})")
