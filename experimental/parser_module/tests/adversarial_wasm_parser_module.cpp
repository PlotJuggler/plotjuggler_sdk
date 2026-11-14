// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <limits>

#include "pj_base/parser_module/module.hpp"

class AdversarialWasmParser : public pj::FunctionalParser {
 public:
  pj::Status bind(const pj::BindingInfo&) override {
    return pj::Status::ok();
  }

  pj::Status parseObject(pj::PayloadView payload, pj::Timestamp, pj::ObjectWriter& output) override {
    if (payload.size == 0 || payload.data == nullptr) {
      return pj::Status::error("adversarial fixture requires a behavior byte");
    }
    switch (payload.data[0]) {
      case 0:
        __builtin_trap();
        return pj::Status::error("unreachable returned");
      case 1: {
        volatile uint64_t progress = 0;
        while (true) {
          ++progress;
        }
      }
      case 2: {
        const size_t before = __builtin_wasm_memory_size(0);
        const size_t result = __builtin_wasm_memory_grow(0, std::numeric_limits<size_t>::max());
        const size_t after = __builtin_wasm_memory_size(0);
        if (result != std::numeric_limits<size_t>::max() || before != after) {
          return pj::Status::error("memory growth escaped the declared maximum");
        }
        return pj::Status::error("memory growth rejected by declared maximum");
      }
      default:
        break;
    }

    auto cloud = output.pointCloud();
    if (auto status = cloud.setWidth(1); !status.isOk()) {
      return status;
    }
    if (auto status = cloud.setHeight(1); !status.isOk()) {
      return status;
    }
    if (auto status = cloud.setPointStep(1); !status.isOk()) {
      return status;
    }
    if (auto status = cloud.setRowStep(1); !status.isOk()) {
      return status;
    }
    if (auto status = cloud.setDense(true); !status.isOk()) {
      return status;
    }
    const uint8_t data = 42;
    return cloud.setData(pj::PayloadView(&data, 1));
  }
};

PJ_FUNCTIONAL_PARSER(AdversarialWasmParser)
