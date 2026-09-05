// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Tests for the attach_source_record runtime-host tail slot:
//
//   1. DataSourceRuntimeHostView::attachSourceRecord flows the descriptor
//      bytes through the slot; the host copies during the call.
//   2. A host error (e.g. exceeding descriptor bounds) surfaces as the host's own
//      message, never as a silent success.
//   3. When the host predates the slot (short struct_size / NULL field), the
//      call returns an explicit error — a NEW plugin on an OLD host detects
//      "no caching" instead of degrading silently.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <string>

#include "pj_base/data_source_protocol.h"
#include "pj_base/sdk/data_source_host_views.hpp"

namespace {

// Mock runtime host — captures attach_source_record calls.
class MockHost {
 public:
  MockHost() {
    vtable_.protocol_version = 1;
    vtable_.struct_size = sizeof(PJ_data_source_runtime_host_vtable_t);
    vtable_.attach_source_record = &MockHost::attachThunk;
    host_.ctx = this;
    host_.vtable = &vtable_;
  }

  // Simulate an older host that predates the slot.
  // Old host: shrink struct_size AND null the field.
  void dropAttachSourceRecord() {
    vtable_.attach_source_record = nullptr;
    vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, attach_source_record);
  }

  // Host that reports a short struct_size but left a stale non-null pointer:
  // the size gate alone must keep the slot unreachable.
  void shrinkStructSizeOnly() {
    vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, attach_source_record);
  }

  PJ::DataSourceRuntimeHostView view() const {
    return PJ::DataSourceRuntimeHostView(host_);
  }

  std::string captured;
  int call_count = 0;
  bool refuse = false;

 private:
  static bool attachThunk(void* ctx, PJ_string_view_t descriptor_json, PJ_error_t* err) noexcept {
    auto* self = static_cast<MockHost*>(ctx);
    self->call_count++;
    self->captured.assign(descriptor_json.data, descriptor_json.size);
    if (self->refuse) {
      if (err != nullptr) {
        std::snprintf(err->message, sizeof(err->message), "descriptor exceeds host policy bounds");
      }
      return false;
    }
    return true;
  }

  PJ_data_source_runtime_host_vtable_t vtable_{};
  PJ_data_source_runtime_host_t host_{};
};

TEST(AttachSourceRecordTest, DescriptorFlowsThroughSlot) {
  MockHost host;
  const std::string descriptor = R"({"kind":"example-request","v":1,"topics":["/a","/b"]})";

  auto status = host.view().attachSourceRecord(descriptor);
  ASSERT_TRUE(status) << (status ? "" : status.error());
  EXPECT_EQ(host.call_count, 1);
  EXPECT_EQ(host.captured, descriptor);
}

TEST(AttachSourceRecordTest, HostRefusalCarriesTheHostsReason) {
  MockHost host;
  host.refuse = true;

  auto status = host.view().attachSourceRecord(R"({"v":1})");
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("policy bounds"), std::string::npos);
}

TEST(AttachSourceRecordTest, ReturnsErrorWhenSlotMissing) {
  MockHost host;
  host.dropAttachSourceRecord();

  auto status = host.view().attachSourceRecord(R"({"v":1})");
  EXPECT_FALSE(status);  // explicit failure so a new plugin can fall back
  EXPECT_EQ(host.call_count, 0);
}

TEST(AttachSourceRecordTest, ShortStructSizeAloneGatesTheSlot) {
  MockHost host;
  host.shrinkStructSizeOnly();  // stale non-null pointer past the reported size

  auto status = host.view().attachSourceRecord(R"({"v":1})");
  EXPECT_FALSE(status);
  EXPECT_EQ(host.call_count, 0);
}

TEST(AttachSourceRecordTest, UnboundHostReportsNotBound) {
  PJ::DataSourceRuntimeHostView view;  // default: no host
  auto status = view.attachSourceRecord(R"({"v":1})");
  ASSERT_FALSE(status);
  EXPECT_NE(status.error().find("not bound"), std::string::npos);
}

}  // namespace
