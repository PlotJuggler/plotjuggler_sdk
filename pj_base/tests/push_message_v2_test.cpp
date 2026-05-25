// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Tests for the SDK template `DataSourceRuntimeHostView::pushMessage` and
// its delegation to the C ABI slot `push_message_v2`. We exercise:
//
//   1. Vector closure → the captured FetchMessageData callable yields the
//      same bytes when invoked from the host.
//   2. PayloadView closure → ditto, with the producer-supplied anchor
//      flowing through the C ABI.
//   3. Multiple invocations are idempotent (same bytes each time).
//   4. The heap-held closure context is destroyed exactly once when the
//      host calls `release`.
//   5. When the host does not expose push_message_v2 (struct_size short
//      or field NULL), pushMessage returns an explicit error rather than
//      degrading silently.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/data_source_protocol.h"
#include "pj_base/sdk/data_source_host_views.hpp"

namespace {

// Captured state from a push_message_v2 invocation.
struct CapturedPush {
  PJ_parser_binding_handle_t handle{};
  int64_t timestamp_ns = 0;
  PJ_message_data_fetcher_t fetch_message_data{};
  bool received = false;
};

// Mock runtime host — exposes a vtable that captures push_message_v2 calls
// and, alternatively, push_raw_message calls (for the legacy fallback).
class MockHost {
 public:
  MockHost() {
    vtable_.protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION;
    vtable_.struct_size = sizeof(PJ_data_source_runtime_host_vtable_t);
    vtable_.push_raw_message = &MockHost::pushRawMessageThunk;
    vtable_.push_message_v2 = &MockHost::pushMessageV2Thunk;
    host_.ctx = this;
    host_.vtable = &vtable_;
  }

  // Drop the v2 slot — both clearing the field and shrinking struct_size,
  // matching the runtime scenario where the host predates the addition.
  void disablePushMessageV2() {
    vtable_.push_message_v2 = nullptr;
    vtable_.struct_size = offsetof(PJ_data_source_runtime_host_vtable_t, push_message_v2);
  }

  PJ::DataSourceRuntimeHostView view() const {
    return PJ::DataSourceRuntimeHostView(host_);
  }

  CapturedPush& captured() {
    return captured_;
  }
  std::vector<uint8_t>& receivedRawBytes() {
    return raw_bytes_;
  }

 private:
  static bool pushRawMessageThunk(
      void* ctx, PJ_parser_binding_handle_t /*handle*/, int64_t /*ts*/, PJ_bytes_view_t payload,
      PJ_error_t* /*err*/) noexcept {
    auto* self = static_cast<MockHost*>(ctx);
    self->raw_bytes_.assign(payload.data, payload.data + payload.size);
    return true;
  }

  static bool pushMessageV2Thunk(
      void* ctx, PJ_parser_binding_handle_t handle, int64_t ts, PJ_message_data_fetcher_t fetch_message_data,
      PJ_error_t* /*err*/) noexcept {
    auto* self = static_cast<MockHost*>(ctx);
    self->captured_.handle = handle;
    self->captured_.timestamp_ns = ts;
    self->captured_.fetch_message_data = fetch_message_data;
    self->captured_.received = true;
    return true;
  }

  PJ_data_source_runtime_host_vtable_t vtable_{};
  PJ_data_source_runtime_host_t host_{};
  CapturedPush captured_;
  std::vector<uint8_t> raw_bytes_;
};

// Helper: invoke a captured FetchMessageData callable and assert the produced bytes match
// the expected content. Releases the payload anchor.
void invokeFetchMessageDataAndExpect(
    PJ_message_data_fetcher_t& fetch_message_data, const std::vector<uint8_t>& expected) {
  PJ_payload_t payload{};
  PJ_error_t err{};
  ASSERT_NE(fetch_message_data.fetchMessageData, nullptr);
  ASSERT_TRUE(fetch_message_data.fetchMessageData(fetch_message_data.ctx, &payload, &err));
  ASSERT_EQ(payload.size, expected.size());
  EXPECT_EQ(0, std::memcmp(payload.data, expected.data(), expected.size()));
  if (payload.anchor.release) {
    payload.anchor.release(payload.anchor.ctx);
  }
}

// ---------- Tests against the new push_message_v2 path ----------

TEST(PushMessageV2Test, VectorClosureFlowsThroughSlot) {
  MockHost host;
  std::vector<uint8_t> expected{1, 2, 3, 4, 5};

  auto status = host.view().pushMessage(PJ::ParserBindingHandle{42}, 1000, [bytes = expected]() { return bytes; });

  ASSERT_TRUE(status);
  ASSERT_TRUE(host.captured().received);
  EXPECT_EQ(host.captured().handle.id, 42U);
  EXPECT_EQ(host.captured().timestamp_ns, 1000);
  invokeFetchMessageDataAndExpect(host.captured().fetch_message_data, expected);
  host.captured().fetch_message_data.release(host.captured().fetch_message_data.ctx);
}

TEST(PushMessageV2Test, PayloadViewClosureFlowsThroughSlot) {
  MockHost host;
  std::vector<uint8_t> expected{10, 20, 30};
  auto owned = std::make_shared<std::vector<uint8_t>>(expected);

  auto status = host.view().pushMessage(PJ::ParserBindingHandle{7}, 2000, [owned]() -> PJ::sdk::PayloadView {
    return {PJ::Span<const uint8_t>(owned->data(), owned->size()), owned};
  });

  ASSERT_TRUE(status);
  invokeFetchMessageDataAndExpect(host.captured().fetch_message_data, expected);
  host.captured().fetch_message_data.release(host.captured().fetch_message_data.ctx);
}

TEST(PushMessageV2Test, FetchIsIdempotent) {
  MockHost host;
  std::vector<uint8_t> expected{0x42, 0x43};

  ASSERT_TRUE(host.view().pushMessage(PJ::ParserBindingHandle{1}, 0, [bytes = expected]() { return bytes; }));

  // Multiple invocations must yield the same bytes each time.
  for (int i = 0; i < 3; ++i) {
    invokeFetchMessageDataAndExpect(host.captured().fetch_message_data, expected);
  }
  host.captured().fetch_message_data.release(host.captured().fetch_message_data.ctx);
}

TEST(PushMessageV2Test, FetchMessageDataCtxReleasedAfterHostCalls) {
  MockHost host;
  auto canary = std::make_shared<int>(42);
  std::weak_ptr<int> witness = canary;

  ASSERT_TRUE(host.view().pushMessage(PJ::ParserBindingHandle{1}, 0, [canary]() { return std::vector<uint8_t>{}; }));

  // Drop our local reference; the heap-held closure copy keeps the canary
  // alive while the fetch_message_data is owned by the host.
  canary.reset();
  EXPECT_FALSE(witness.expired()) << "closure should still keep the canary alive (held in heap fetch_message_data ctx)";

  // Host releases the fetch_message_data → closure destroyed → captured shared_ptr
  // destroyed → canary's last reference drops.
  host.captured().fetch_message_data.release(host.captured().fetch_message_data.ctx);
  EXPECT_TRUE(witness.expired()) << "after release, the captured shared_ptr should have been the last reference";
}

TEST(PushMessageV2Test, PayloadAnchorPropagates) {
  MockHost host;
  auto owned = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{0x99, 0x9A});
  std::weak_ptr<std::vector<uint8_t>> witness = owned;

  ASSERT_TRUE(host.view().pushMessage(PJ::ParserBindingHandle{1}, 0, [owned]() -> PJ::sdk::PayloadView {
    return {PJ::Span<const uint8_t>(owned->data(), owned->size()), owned};
  }));

  // The closure holds the owned vector via its shared_ptr capture.
  // After releasing our local owned, the closure's copy keeps it alive.
  owned.reset();
  EXPECT_FALSE(witness.expired());

  // Invoke the fetch_message_data: it builds a PayloadView into the same buffer; the
  // anchor returned to the host is yet another shared_ptr copy, so the
  // buffer survives even past the closure's release.
  PJ_payload_t payload{};
  PJ_error_t err{};
  ASSERT_TRUE(
      host.captured().fetch_message_data.fetchMessageData(host.captured().fetch_message_data.ctx, &payload, &err));
  EXPECT_EQ(payload.size, 2U);

  // Releasing the fetch_message_data (closure dies) does NOT kill the buffer because
  // the active payload anchor still holds a reference.
  host.captured().fetch_message_data.release(host.captured().fetch_message_data.ctx);
  EXPECT_FALSE(witness.expired()) << "active payload anchor should still keep the buffer alive";

  // Releasing the payload anchor drops the last reference.
  if (payload.anchor.release) {
    payload.anchor.release(payload.anchor.ctx);
  }
  EXPECT_TRUE(witness.expired());
}

// ---------- Host without push_message_v2 returns explicit error ----------

TEST(PushMessageV2Test, ReturnsErrorWhenSlotMissing) {
  MockHost host;
  host.disablePushMessageV2();

  std::vector<uint8_t> expected{0xA, 0xB, 0xC};
  auto status = host.view().pushMessage(PJ::ParserBindingHandle{1}, 100, [bytes = expected]() { return bytes; });
  EXPECT_FALSE(status);  // explicit failure — no silent fallback to push_raw_message
  EXPECT_FALSE(host.captured().received);
  EXPECT_TRUE(host.receivedRawBytes().empty());
}

}  // namespace
