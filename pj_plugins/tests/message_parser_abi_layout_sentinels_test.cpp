/**
 * @file message_parser_abi_layout_sentinels_test.cpp
 * @brief Compile-time pins for MessageParserPluginBase's cross-DSO prefix.
 *
 * PJ4 casts the plugin-created context to MessageParserPluginBase* and calls
 * the final classifySchema/parseScalars/parseObject methods directly. Those
 * host-compiled methods access the pre-0.21 members below, so their offsets
 * must remain fixed within PJ_ABI_VERSION 5. The host never allocates or
 * copies this object; sizeof may grow as new plugin-only members are appended
 * and is deliberately not pinned here.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>

#include "pj_plugins/sdk/message_parser_plugin_base.hpp"

// MessageParserPluginBase is polymorphic and therefore not standard-layout.
// GCC/Clang still provide the ABI offset intrinsic for this exact sentinel use.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

namespace PJ::sdk::testing {

struct MessageParserPluginBaseLayoutSentinel {
  static constexpr std::size_t kBoundTypeName = offsetof(::PJ::MessageParserPluginBase, bound_type_name_);
  static constexpr std::size_t kServiceRegistry = offsetof(::PJ::MessageParserPluginBase, service_registry_);
  static constexpr std::size_t kWriteHostView = offsetof(::PJ::MessageParserPluginBase, write_host_view_);
  static constexpr std::size_t kObjectWriteHostView = offsetof(::PJ::MessageParserPluginBase, object_write_host_view_);
  static constexpr std::size_t kConfigBuffer = offsetof(::PJ::MessageParserPluginBase, config_buf_);
  static constexpr std::size_t kHandlers = offsetof(::PJ::MessageParserPluginBase, handlers_);
};

}  // namespace PJ::sdk::testing

static_assert(PJ::sdk::testing::MessageParserPluginBaseLayoutSentinel::kBoundTypeName == 8, "v5 prefix moved");
static_assert(PJ::sdk::testing::MessageParserPluginBaseLayoutSentinel::kServiceRegistry == 40, "v5 prefix moved");
static_assert(PJ::sdk::testing::MessageParserPluginBaseLayoutSentinel::kWriteHostView == 56, "v5 prefix moved");
static_assert(PJ::sdk::testing::MessageParserPluginBaseLayoutSentinel::kObjectWriteHostView == 72, "v5 prefix moved");
static_assert(PJ::sdk::testing::MessageParserPluginBaseLayoutSentinel::kConfigBuffer == 88, "v5 prefix moved");
static_assert(PJ::sdk::testing::MessageParserPluginBaseLayoutSentinel::kHandlers == 120, "v5 prefix moved");

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
