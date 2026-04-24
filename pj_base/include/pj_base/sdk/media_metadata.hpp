#pragma once

#include <string>
#include <string_view>

namespace PJ::sdk {

/// Builder for the `metadata_json` string attached to an ObjectStore topic
/// at registration time. Viewers and parsers read this to pick a renderer
/// or decoder. The builder emits minimal valid JSON with no external
/// dependency; the three documented keys from OBJECT_STORE_DESIGN.md §4
/// become typed methods so typos fail to compile.
///
/// Example:
///   auto meta = MediaMetadataBuilder()
///       .mediaClass("image")
///       .encoding("jpeg")
///       .schema("sensor_msgs/CompressedImage")
///       .build();
///   host.registerTopic(name, meta);
///
/// Custom keys via `extra()` for format-specific metadata.
class MediaMetadataBuilder {
 public:
  MediaMetadataBuilder& mediaClass(std::string_view v) {
    media_class_ = v;
    return *this;
  }

  MediaMetadataBuilder& encoding(std::string_view v) {
    encoding_ = v;
    return *this;
  }

  MediaMetadataBuilder& schema(std::string_view v) {
    schema_ = v;
    return *this;
  }

  /// Append a raw JSON key/value pair. `value_json` must itself be valid
  /// JSON (a quoted string, number, bool, object, or array). For a plain
  /// string value prefer `extraString()`.
  MediaMetadataBuilder& extra(std::string_view key, std::string_view value_json) {
    appendExtra(key, value_json, /*quoted=*/false);
    return *this;
  }

  /// Append a key whose value is a plain string — the builder quotes and
  /// escapes it.
  MediaMetadataBuilder& extraString(std::string_view key, std::string_view value) {
    appendExtra(key, value, /*quoted=*/true);
    return *this;
  }

  [[nodiscard]] std::string build() const {
    std::string out;
    out.reserve(64 + media_class_.size() + encoding_.size() + schema_.size() + extras_.size());
    out.push_back('{');
    bool first = true;
    auto kv_string = [&](std::string_view key, std::string_view value) {
      if (value.empty()) {
        return;
      }
      if (!first) {
        out.push_back(',');
      }
      first = false;
      out.push_back('"');
      out.append(key);
      out.append("\":\"");
      appendEscaped(out, value);
      out.push_back('"');
    };
    kv_string("media_class", media_class_);
    kv_string("encoding", encoding_);
    kv_string("schema", schema_);
    if (!extras_.empty()) {
      if (!first) {
        out.push_back(',');
      }
      // extras_ is pre-formatted as "key1":value1,"key2":value2 ... with
      // embedded separators; append as-is.
      out.append(extras_);
    }
    out.push_back('}');
    return out;
  }

 private:
  std::string media_class_;
  std::string encoding_;
  std::string schema_;
  std::string extras_;  // pre-formatted inner fragments separated by ','.

  void appendExtra(std::string_view key, std::string_view value, bool quoted) {
    if (!extras_.empty()) {
      extras_.push_back(',');
    }
    extras_.push_back('"');
    extras_.append(key);
    extras_.append("\":");
    if (quoted) {
      extras_.push_back('"');
      appendEscaped(extras_, value);
      extras_.push_back('"');
    } else {
      extras_.append(value);
    }
  }

  /// Minimal JSON-string escape for ", \, and control chars < 0x20.
  static void appendEscaped(std::string& out, std::string_view s) {
    for (char c : s) {
      switch (c) {
        case '"':
          out.append("\\\"");
          break;
        case '\\':
          out.append("\\\\");
          break;
        case '\b':
          out.append("\\b");
          break;
        case '\f':
          out.append("\\f");
          break;
        case '\n':
          out.append("\\n");
          break;
        case '\r':
          out.append("\\r");
          break;
        case '\t':
          out.append("\\t");
          break;
        default:
          if (static_cast<unsigned char>(c) < 0x20) {
            static constexpr char kHex[] = "0123456789abcdef";
            out.append("\\u00");
            out.push_back(kHex[(c >> 4) & 0xF]);
            out.push_back(kHex[c & 0xF]);
          } else {
            out.push_back(c);
          }
          break;
      }
    }
  }
};

}  // namespace PJ::sdk
