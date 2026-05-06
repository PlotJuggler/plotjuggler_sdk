#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "pj_base/data_source_protocol.h"
#include "pj_base/expected.hpp"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/toolbox_protocol.h"

namespace PJ::detail {

struct RequiredSlot {
  std::string_view name;
  bool present;
};

inline Status validateRequiredSlots(std::string_view family, std::initializer_list<RequiredSlot> slots) {
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

inline Status validateRequiredSlots(const PJ_data_source_vtable_t* vtable) {
  return validateRequiredSlots(
      "DataSource",
      {
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

inline Status validateRequiredSlots(const PJ_message_parser_vtable_t* vtable) {
  return validateRequiredSlots(
      "MessageParser",
      {
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

inline Status validateRequiredSlots(const PJ_toolbox_vtable_t* vtable) {
  return validateRequiredSlots(
      "Toolbox",
      {
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

}  // namespace PJ::detail
