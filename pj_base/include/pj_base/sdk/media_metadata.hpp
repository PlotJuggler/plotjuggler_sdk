#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <string_view>

#include "pj_base/sdk/detail/json.hpp"

namespace PJ::sdk {

/// Builder for supplemental media fields in the `metadata_json` string
/// attached to an ObjectStore topic at registration time. `media_class`,
/// `encoding`, and `schema` describe media or decoder details; they do not
/// select a canonical built-in renderer. In particular, `media_class` is not
/// an alias for the canonical `builtin_object_type` discovery key.
///
/// Example:
///   auto meta = MediaMetadataBuilder()
///       .mediaClass("image")
///       .encoding("jpeg")
///       .schema("sensor_msgs/CompressedImage")
///       .build();
///   host.registerTopic(name, meta);
///
/// For a renderable built-in object, use `ObjectTopicMetadataBuilder`, call
/// `builtinObjectType()`, and compose supplemental fields with its `string()`
/// method. Custom, untyped object topics may continue to use this builder.
/// Custom keys are available through `extra()` for format-specific metadata.
class MediaMetadataBuilder {
 public:
  /// Set a supplemental media classification. This does not select a built-in
  /// renderer; use `ObjectTopicMetadataBuilder::builtinObjectType()` for that.
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
      detail::appendJsonEscaped(out, value);
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
      detail::appendJsonEscaped(extras_, value);
      extras_.push_back('"');
    } else {
      extras_.append(value);
    }
  }
};

}  // namespace PJ::sdk
