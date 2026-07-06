// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/data_source_host_views.hpp"

#include <vector>

namespace PJ {

std::string errorToString(const PJ_error_t& err) {
  std::string out;
  if (err.domain[0] != '\0') {
    out.append(err.domain);
    out.append(": ");
  }
  if (err.message[0] != '\0') {
    out.append(err.message);
  }
  if (out.empty()) {
    out = "unspecified error";
  }
  return out;
}

Status DataSourceRuntimeHostView::progressStart(std::string_view label, uint64_t total_steps, bool cancellable) const {
  if (!valid() || host_.vtable->progress_start == nullptr) {
    return unexpected("runtime host is not bound");
  }
  PJ_error_t err{};
  if (!host_.vtable->progress_start(host_.ctx, sdk::toAbiString(label), total_steps, cancellable, &err)) {
    return unexpected(errorToString(err));
  }
  return okStatus();
}

Expected<ParserBindingHandle> DataSourceRuntimeHostView::ensureParserBinding(
    const ParserBindingRequest& request) const {
  if (!valid() || host_.vtable->ensure_parser_binding == nullptr) {
    return unexpected("runtime host is not bound");
  }

  PJ_parser_binding_request_t raw{
      .topic_name = sdk::toAbiString(request.topic_name),
      .parser_encoding = sdk::toAbiString(request.parser_encoding),
      .type_name = sdk::toAbiString(request.type_name),
      .schema = sdk::toAbiBytes(request.schema),
      .parser_config_json = sdk::toAbiString(request.parser_config_json),
  };

  ParserBindingHandle handle{};
  PJ_error_t err{};
  if (!host_.vtable->ensure_parser_binding(host_.ctx, &raw, &handle, &err)) {
    return unexpected(errorToString(err));
  }
  return handle;
}

Status DataSourceRuntimeHostView::notifyAvailableTopics(Span<const AvailableTopic> topics) const {
  if (!valid()) {
    return unexpected(std::string("runtime host is not bound"));
  }
  if (!PJ_HAS_TAIL_SLOT(PJ_data_source_runtime_host_vtable_t, host_.vtable, notify_available_topics)) {
    return unexpected(std::string("runtime host does not expose notify_available_topics"));
  }
  std::vector<PJ_available_topic_t> raw;
  raw.reserve(topics.size());
  for (const auto& t : topics) {
    raw.push_back(
        PJ_available_topic_t{
            .topic_name = sdk::toAbiString(t.topic_name),
            .parser_encoding = sdk::toAbiString(t.parser_encoding),
            .type_name = sdk::toAbiString(t.type_name),
            .schema = sdk::toAbiBytes(t.schema),
        });
  }
  PJ_error_t err{};
  if (!host_.vtable->notify_available_topics(host_.ctx, raw.data(), raw.size(), &err)) {
    return unexpected(errorToString(err));
  }
  return okStatus();
}

MessageBoxButton DataSourceRuntimeHostView::showMessageBox(
    MessageBoxType type, std::string_view title, std::string_view message, int buttons) const {
  if (!valid() || host_.vtable->show_message_box == nullptr) {
    if (buttons & static_cast<int>(MessageBoxButton::kContinue)) {
      return MessageBoxButton::kContinue;
    }
    if (buttons & static_cast<int>(MessageBoxButton::kYes)) {
      return MessageBoxButton::kYes;
    }
    return MessageBoxButton::kOk;
  }
  int result = host_.vtable->show_message_box(
      host_.ctx, static_cast<PJ_message_box_type_t>(type), sdk::toAbiString(title), sdk::toAbiString(message),
      buttons == 0 ? PJ_MSG_BTN_OK : buttons);
  return static_cast<MessageBoxButton>(result);
}

}  // namespace PJ
