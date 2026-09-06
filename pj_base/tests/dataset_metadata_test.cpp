// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Tests for the set_dataset_metadata tail slot (0.32.0):
//
//   1. DataSourceRuntimeHostView::setDatasetMetadata flows the borrowed JSON
//      through the slot; the host copies during the call and owns the bytes
//      afterwards. Repeat calls reach the host again (replace semantics are
//      the host's business; the wrapper never dedupes).
//   2. Old-host negotiation: short struct_size / NULL slot yields an explicit
//      error and the plugin proceeds without metadata; struct_size alone
//      gates (a stale non-null pointer past the reported size is never read).
//   3. A host rejection (false + error) surfaces the host's reason verbatim.
//   4. DatasetIngestHostView forwards to the same slot.

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/sdk/data_source_host_views.hpp"

namespace {

// Mock runtime host — copies the borrowed document during the call, exactly
// as a real host must.
class MockRuntimeHost {
 public:
  MockRuntimeHost() {
    vtable_.protocol_version = 1;
    vtable_.struct_size = sizeof(PJ_data_source_runtime_host_vtable_t);
    vtable_.set_dataset_metadata = &MockRuntimeHost::setMetadataThunk;
    host_.ctx = this;
    host_.vtable = &vtable_;
  }

  void dropSetDatasetMetadata() {
    vtable_.set_dataset_metadata = nullptr;
    vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, set_dataset_metadata);
  }

  void shrinkStructSizeOnly() {
    vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, set_dataset_metadata);
  }

  PJ::DataSourceRuntimeHostView view() const {
    return PJ::DataSourceRuntimeHostView(host_);
  }

  int call_count = 0;
  std::vector<std::string> received;
  std::string reject_reason;  // non-empty: the thunk refuses with this reason

 private:
  static bool setMetadataThunk(void* ctx, PJ_string_view_t metadata_json, PJ_error_t* err) noexcept {
    auto* self = static_cast<MockRuntimeHost*>(ctx);
    self->call_count++;
    self->received.emplace_back(metadata_json.data, metadata_json.size);
    if (!self->reject_reason.empty()) {
      if (err != nullptr) {
        std::snprintf(err->message, sizeof(err->message), "%s", self->reject_reason.c_str());
      }
      return false;
    }
    return true;
  }

  PJ_data_source_runtime_host_vtable_t vtable_{};
  PJ_data_source_runtime_host_t host_{};
};

TEST(DatasetMetadataTest, DocumentFlowsThroughAndReplacesOnRepeat) {
  MockRuntimeHost host;
  auto status = host.view().setDatasetMetadata(R"({"file":{"messages":42}})");
  ASSERT_TRUE(status) << status.error();
  status = host.view().setDatasetMetadata("{}");
  ASSERT_TRUE(status) << status.error();
  EXPECT_EQ(host.call_count, 2);
  EXPECT_EQ(host.received, (std::vector<std::string>{R"({"file":{"messages":42}})", "{}"}));
}

TEST(DatasetMetadataTest, ReturnsErrorWhenSlotMissing) {
  MockRuntimeHost host;
  host.dropSetDatasetMetadata();
  auto status = host.view().setDatasetMetadata("{}");
  EXPECT_FALSE(status);  // explicit: the plugin proceeds without metadata
  EXPECT_EQ(host.call_count, 0);
}

TEST(DatasetMetadataTest, ShortStructSizeAloneGatesTheSlot) {
  MockRuntimeHost host;
  host.shrinkStructSizeOnly();  // stale non-null pointer past the reported size
  auto status = host.view().setDatasetMetadata("{}");
  EXPECT_FALSE(status);
  EXPECT_EQ(host.call_count, 0);
}

TEST(DatasetMetadataTest, HostRejectionSurfacesReason) {
  MockRuntimeHost host;
  host.reject_reason = "document exceeds host bounds";
  auto status = host.view().setDatasetMetadata(R"({"huge":true})");
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("document exceeds host bounds"), std::string::npos);
}

TEST(DatasetMetadataTest, UnboundHostReportsNotBound) {
  PJ::DataSourceRuntimeHostView view;
  auto status = view.setDatasetMetadata("{}");
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("not bound"), std::string::npos);
}

TEST(DatasetMetadataTest, DatasetIngestViewForwards) {
  MockRuntimeHost host;
  auto status = host.view().datasetIngest().setDatasetMetadata(R"({"origin":"file"})");
  ASSERT_TRUE(status) << status.error();
  EXPECT_EQ(host.received, (std::vector<std::string>{R"({"origin":"file"})"}));
}

}  // namespace
