// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "vtable_validation.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace PJ::detail {
namespace {

struct RequiredSlot {
  std::string_view name;
  bool present;
};

Status validateSlots(std::string_view family, std::initializer_list<RequiredSlot> slots) {
  for (const auto& slot : slots) {
    if (!slot.present) {
      std::string message(family);
      message += " vtable missing required slot: ";
      message += slot.name;
      return unexpected(std::move(message));
    }
  }
  return okStatus();
}

}  // namespace

Status validateRequiredSlots(const PJ_data_source_vtable_t* vtable) {
  return validateSlots(
      "DataSource", {
                        {"create", vtable->create != nullptr},
                        {"destroy", vtable->destroy != nullptr},
                        {"manifest_json", vtable->manifest_json != nullptr},
                        {"capabilities", vtable->capabilities != nullptr},
                        {"bind", vtable->bind != nullptr},
                        {"save_config", vtable->save_config != nullptr},
                        {"load_config", vtable->load_config != nullptr},
                        {"start", vtable->start != nullptr},
                        {"stop", vtable->stop != nullptr},
                        {"pause", vtable->pause != nullptr},
                        {"resume", vtable->resume != nullptr},
                        {"poll", vtable->poll != nullptr},
                        {"current_state", vtable->current_state != nullptr},
                        {"get_dialog", vtable->get_dialog != nullptr},
                        {"get_plugin_extension", vtable->get_plugin_extension != nullptr},
                    });
}

Status validateRequiredSlots(const PJ_message_parser_vtable_t* vtable) {
  return validateSlots(
      "MessageParser", {
                           {"create", vtable->create != nullptr},
                           {"destroy", vtable->destroy != nullptr},
                           {"manifest_json", vtable->manifest_json != nullptr},
                           {"bind", vtable->bind != nullptr},
                           {"bind_schema", vtable->bind_schema != nullptr},
                           {"save_config", vtable->save_config != nullptr},
                           {"load_config", vtable->load_config != nullptr},
                           {"parse", vtable->parse != nullptr},
                           {"get_plugin_extension", vtable->get_plugin_extension != nullptr},
                       });
}

Status validateRequiredSlots(const PJ_toolbox_vtable_t* vtable) {
  return validateSlots(
      "Toolbox", {
                     {"create", vtable->create != nullptr},
                     {"destroy", vtable->destroy != nullptr},
                     {"manifest_json", vtable->manifest_json != nullptr},
                     {"capabilities", vtable->capabilities != nullptr},
                     {"bind", vtable->bind != nullptr},
                     {"save_config", vtable->save_config != nullptr},
                     {"load_config", vtable->load_config != nullptr},
                     {"get_dialog", vtable->get_dialog != nullptr},
                     {"on_data_changed", vtable->on_data_changed != nullptr},
                     {"get_plugin_extension", vtable->get_plugin_extension != nullptr},
                 });
}

Status validateRequiredSlots(const PJ_dialog_vtable_t* vtable) {
  return validateSlots(
      "Dialog", {
                    {"create", vtable->create != nullptr},
                    {"destroy", vtable->destroy != nullptr},
                    {"get_manifest", vtable->get_manifest != nullptr},
                    {"get_ui_content", vtable->get_ui_content != nullptr},
                    {"get_widget_data", vtable->get_widget_data != nullptr},
                    {"on_widget_event", vtable->on_widget_event != nullptr},
                    {"on_tick", vtable->on_tick != nullptr},
                    {"on_accepted", vtable->on_accepted != nullptr},
                    {"on_rejected", vtable->on_rejected != nullptr},
                    {"save_config", vtable->save_config != nullptr},
                    {"load_config", vtable->load_config != nullptr},
                });
}

}  // namespace PJ::detail
