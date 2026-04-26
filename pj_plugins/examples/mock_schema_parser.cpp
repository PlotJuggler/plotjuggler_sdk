#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pj_base/sdk/message_parser_plugin_base.hpp"

namespace {

/// Parser demonstrating the high-throughput pattern: schema binding,
/// pre-resolved field handles via ensureField() + appendBoundRecord(),
/// config persistence, and error handling.
///
/// Payload format: two comma-separated doubles, e.g. "1.5,2.7".
/// Field names come from schema binding (or defaults to "x","y").
class MockSchemaParser : public PJ::MessageParserPluginBase {
 public:
  std::string saveConfig() const override {
    return config_;
  }

  PJ::Status loadConfig(std::string_view config_json) override {
    config_ = std::string(config_json);
    return PJ::okStatus();
  }

  PJ::Status bindSchema(std::string_view type_name, PJ::Span<const uint8_t> schema) override {
    type_name_ = std::string(type_name);
    // Interpret schema bytes as comma-separated field names, e.g. "temp,humidity".
    std::string schema_str(reinterpret_cast<const char*>(schema.data()), schema.size());
    field_names_.clear();
    fields_bound_ = false;

    size_t pos = 0;
    while (pos < schema_str.size()) {
      auto comma = schema_str.find(',', pos);
      if (comma == std::string::npos) {
        field_names_.push_back(schema_str.substr(pos));
        break;
      }
      field_names_.push_back(schema_str.substr(pos, comma - pos));
      pos = comma + 1;
    }

    if (field_names_.size() != 2) {
      return PJ::unexpected(std::string("schema must define exactly 2 fields"));
    }
    return PJ::okStatus();
  }

  PJ::Status parse(PJ::Timestamp timestamp_ns, PJ::Span<const uint8_t> payload) override {
    if (!writeHostBound()) {
      return PJ::unexpected(std::string("write host not bound"));
    }

    // Lazily bind fields on first parse.
    if (!fields_bound_) {
      auto status = bindFields();
      if (!status) {
        return status;
      }
    }

    // Parse "a,b" from payload.
    std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
    auto comma = text.find(',');
    if (comma == std::string::npos) {
      return PJ::unexpected(std::string("expected comma-separated pair"));
    }

    double a = std::strtod(text.c_str(), nullptr);
    double b = std::strtod(text.c_str() + comma + 1, nullptr);

    const PJ::sdk::BoundFieldValue fields[] = {
        {.field = field_handles_[0], .value = a},
        {.field = field_handles_[1], .value = b},
    };
    return writeHost().appendBoundRecord(timestamp_ns, PJ::Span<const PJ::sdk::BoundFieldValue>(fields, 2));
  }

 private:
  PJ::Status bindFields() {
    const std::string& name_a = field_names_.empty() ? default_a_ : field_names_[0];
    const std::string& name_b = field_names_.size() < 2 ? default_b_ : field_names_[1];

    auto handle_a = writeHost().ensureField(name_a, PJ::PrimitiveType::kFloat64);
    if (!handle_a) {
      return PJ::unexpected(handle_a.error());
    }
    auto handle_b = writeHost().ensureField(name_b, PJ::PrimitiveType::kFloat64);
    if (!handle_b) {
      return PJ::unexpected(handle_b.error());
    }

    field_handles_ = {*handle_a, *handle_b};
    fields_bound_ = true;
    return PJ::okStatus();
  }

  std::string config_ = "{}";
  std::string type_name_;
  std::vector<std::string> field_names_;
  std::vector<PJ::sdk::FieldHandle> field_handles_;
  bool fields_bound_ = false;

  static inline const std::string default_a_ = "x";
  static inline const std::string default_b_ = "y";
};

}  // namespace

PJ_MESSAGE_PARSER_PLUGIN(
    MockSchemaParser,
    R"({"id":"mock-schema-parser","name":"Mock Schema Parser","version":"1.0.0","encoding":"csv_pair"})")
