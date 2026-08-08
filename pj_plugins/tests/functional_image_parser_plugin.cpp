// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "functional_image_parser_test_protocol.h"
#include "pj_base/builtin/image.hpp"
#include "pj_plugins/sdk/message_parser_plugin_base.hpp"

namespace {

struct UnloadObserver {
  void* ctx = nullptr;
  PJ_test_plugin_unloaded_fn_t callback = nullptr;

  ~UnloadObserver() {
    if (callback != nullptr) {
      callback(ctx);
    }
  }
};

UnloadObserver unload_observer;

class FunctionalImageParser final : public PJ::MessageParserPluginBase {
 public:
  FunctionalImageParser() {
    PJ::sdk::SchemaHandler handler;
    handler.object_type = PJ::sdk::BuiltinObjectType::kImage;
    handler.parse_object = [](PJ::Timestamp timestamp,
                              PJ::sdk::PayloadView payload) -> PJ::Expected<PJ::sdk::ObjectRecord> {
      if (!payload.anchor) {
        return PJ::unexpected(std::string("functional object route did not preserve the payload anchor"));
      }
      PJ::sdk::Image image;
      image.timestamp_ns = timestamp;
      image.frame_id = "plugin-owned-frame";
      image.width = static_cast<uint32_t>(payload.bytes.size());
      image.height = 1;
      image.encoding = "mono8";
      image.row_step = image.width;
      image.data = payload.bytes;
      image.anchor = std::move(payload.anchor);
      return PJ::sdk::ObjectRecord{.ts = std::nullopt, .object = std::move(image)};
    };
    registerSchemaHandler("test/Image", std::move(handler));
  }

  const void* pluginExtension(std::string_view id) override {
    static const PJ_test_unload_observer_v1_t extension{
        .struct_size = sizeof(PJ_test_unload_observer_v1_t),
        .observe =
            [](void* ctx, PJ_test_plugin_unloaded_fn_t callback) noexcept {
              unload_observer.ctx = ctx;
              unload_observer.callback = callback;
            },
    };
    return id == PJ_FUNCTIONAL_IMAGE_PARSER_UNLOAD_OBSERVER_V1 ? &extension : nullptr;
  }
};

}  // namespace

PJ_MESSAGE_PARSER_PLUGIN(
    FunctionalImageParser,
    R"({"id":"functional-image-parser","name":"Functional Image Parser","version":"1.0.0","encoding":["test"]})")
