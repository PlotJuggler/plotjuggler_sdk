// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "pj_base/parser_module/module.hpp"

class ToyCdrPointCloudParser : public pj::FunctionalParser {
 public:
  pj::Status bind(const pj::BindingInfo& info) override {
    pj::CdrFieldLocator locator(info.schemaText());
    if (!locator.status().isOk()) {
      return pj::Status::decline("unsupported toy schema: " + std::string(locator.status().message()));
    }
    auto plan = locator.locate({"width", "frame_id", "data"});
    if (!plan) {
      return pj::Status::decline("unsupported toy schema revision: " + std::string(plan.status().message()));
    }
    auto width = plan->field("width");
    auto frame = plan->field("frame_id");
    auto data = plan->field("data");
    if (!width || !frame || !data) {
      return pj::Status::decline("unsupported toy schema field plan");
    }
    plan_ = std::move(*plan);
    width_ = *width;
    frame_ = *frame;
    data_ = *data;
    claim_index_ = info.claimIndex();
    return pj::Status::ok();
  }

  pj::Status parseObject(pj::PayloadView payload, pj::Timestamp timestamp, pj::ObjectWriter& output) override {
    pj::CdrReader reader(payload, plan_);
    auto width = reader.u32(width_);
    auto frame = reader.string(frame_);
    auto data = reader.bytes(data_);
    if (!width || !frame || !data) {
      return pj::Status::error("toy CDR payload does not match the bound schema");
    }
    if (*width > std::numeric_limits<uint32_t>::max() / 4U) {
      return pj::Status::error("toy point-cloud width overflows row_step");
    }

    auto cloud = output.pointCloud();
    if (timestamp.has_value) {
      if (auto status = cloud.setTimestamp(timestamp.nanoseconds); !status.isOk()) {
        return status;
      }
    }
    if (auto status = cloud.setWidth(*width); !status.isOk()) {
      return status;
    }
    if (auto status = cloud.setHeight(1); !status.isOk()) {
      return status;
    }
    if (auto status = cloud.setPointStep(4); !status.isOk()) {
      return status;
    }
    if (auto status = cloud.setRowStep(*width * 4U); !status.isOk()) {
      return status;
    }
    if (auto status = cloud.setDense(true); !status.isOk()) {
      return status;
    }
    if (auto status = cloud.setFrameId(*frame); !status.isOk()) {
      return status;
    }
    if (claim_index_ == 1) {
      auto reference = reader.spanRef(data_);
      if (!reference) {
        return reference.status();
      }
      return cloud.setDataFromInput(*reference);
    }
    return cloud.setData(*data);
  }

 private:
  pj::CdrTraversalPlan plan_;
  pj::CdrFieldId width_ = 0;
  pj::CdrFieldId frame_ = 0;
  pj::CdrFieldId data_ = 0;
  uint32_t claim_index_ = 0;
};

PJ_FUNCTIONAL_PARSER(ToyCdrPointCloudParser)
