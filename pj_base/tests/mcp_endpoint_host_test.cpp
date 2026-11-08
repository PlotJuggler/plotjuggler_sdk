// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/service_registry.hpp"
#include "pj_base/sdk/service_traits.hpp"
#include "pj_base/sdk/testing/fake_mcp_endpoint_host.hpp"

namespace PJ {
namespace {

using sdk::testing::FakeMcpEndpointHost;

TEST(McpEndpointHostView, AcquireWhileHeldReturnsTheSameEndpoint) {
  FakeMcpEndpointHost host;
  const sdk::McpEndpointHostView view(host.makeHost());
  ASSERT_TRUE(view.valid());

  auto first = view.acquire();
  ASSERT_TRUE(first) << first.error();
  auto second = view.acquire();
  ASSERT_TRUE(second) << second.error();
  EXPECT_EQ(first->url, "http://127.0.0.1:8765/mcp");
  EXPECT_EQ(first->bearer_token, "token-1");
  EXPECT_EQ(second->url, first->url);
  EXPECT_EQ(second->bearer_token, first->bearer_token);
  EXPECT_TRUE(host.held());
  EXPECT_EQ(host.acquireCalls(), 2);
}

TEST(McpEndpointHostView, ReleaseThenAcquireRotatesTheToken) {
  FakeMcpEndpointHost host;
  const sdk::McpEndpointHostView view(host.makeHost());

  auto first = view.acquire();
  ASSERT_TRUE(first) << first.error();
  view.release();
  EXPECT_FALSE(host.held());

  auto second = view.acquire();
  ASSERT_TRUE(second) << second.error();
  EXPECT_EQ(second->bearer_token, "token-2");
  EXPECT_NE(second->bearer_token, first->bearer_token);
  EXPECT_TRUE(host.held());
}

TEST(McpEndpointHostView, ReleaseTwiceIsSafe) {
  FakeMcpEndpointHost host;
  const sdk::McpEndpointHostView view(host.makeHost());
  view.release();  // nothing held yet
  ASSERT_TRUE(view.acquire());

  view.release();
  view.release();
  EXPECT_EQ(host.releaseCalls(), 3);
  EXPECT_FALSE(host.held());
}

TEST(McpEndpointHostView, CopiesSurviveTheHostsDestruction) {
  auto host = std::make_unique<FakeMcpEndpointHost>();
  auto endpoint = sdk::McpEndpointHostView(host->makeHost()).acquire();
  ASSERT_TRUE(endpoint) << endpoint.error();

  host.reset();  // scrubs and frees every byte the host handed out
  EXPECT_EQ(endpoint->url, "http://127.0.0.1:8765/mcp");
  EXPECT_EQ(endpoint->bearer_token, "token-1");
}

TEST(McpEndpointHostView, ListenerFailureIsInternalAndHoldsNothing) {
  FakeMcpEndpointHost host;
  const PJ_mcp_endpoint_host_t raw = host.makeHost();
  const sdk::McpEndpointHostView view(raw);
  ASSERT_TRUE(view.acquire());

  host.fail_with = "cannot listen on loopback";
  PJ_mcp_endpoint_t out{};
  PJ_error_t err{};
  EXPECT_FALSE(raw.vtable->acquire_endpoint(raw.ctx, &out, &err));
  EXPECT_EQ(err.code, PJ_ERROR_CODE_INTERNAL);
  auto failed = view.acquire();
  ASSERT_FALSE(failed);
  EXPECT_NE(failed.error().find("cannot listen on loopback"), std::string::npos);
  EXPECT_FALSE(host.held());

  host.fail_with.clear();  // a later acquire may retry, and gets a fresh token
  auto retried = view.acquire();
  ASSERT_TRUE(retried) << retried.error();
  EXPECT_EQ(retried->bearer_token, "token-2");
}

TEST(McpEndpointHostView, UnboundViewFailsAcquireAndIgnoresRelease) {
  const sdk::McpEndpointHostView view;
  EXPECT_FALSE(view.valid());
  auto endpoint = view.acquire();
  ASSERT_FALSE(endpoint);
  EXPECT_NE(endpoint.error().find("not bound"), std::string::npos);
  view.release();  // no host to call: must not crash
}

TEST(McpEndpointHostView, MissingSlotIsUnbound) {
  FakeMcpEndpointHost host;
  const PJ_mcp_endpoint_host_t full = host.makeHost();

  PJ_mcp_endpoint_host_vtable_t no_release = *full.vtable;
  no_release.release_endpoint = nullptr;
  const sdk::McpEndpointHostView without_release(PJ_mcp_endpoint_host_t{&host, &no_release});
  EXPECT_FALSE(without_release.valid());
  EXPECT_FALSE(without_release.acquire());

  PJ_mcp_endpoint_host_vtable_t no_acquire = *full.vtable;
  no_acquire.acquire_endpoint = nullptr;
  const sdk::McpEndpointHostView without_acquire(PJ_mcp_endpoint_host_t{&host, &no_acquire});
  EXPECT_FALSE(without_acquire.valid());
  EXPECT_FALSE(without_acquire.acquire());
  without_acquire.release();

  EXPECT_EQ(host.acquireCalls(), 0);
  EXPECT_EQ(host.releaseCalls(), 0);
}

TEST(McpEndpointHostView, TooSmallStructSizeIsUnbound) {
  FakeMcpEndpointHost host;
  const PJ_mcp_endpoint_host_t full = host.makeHost();

  // The allocation ends where release_endpoint would start, so ASAN catches a
  // slot read made before checking struct_size.
  constexpr auto short_size = offsetof(PJ_mcp_endpoint_host_vtable_t, release_endpoint);
  PJ_mcp_endpoint_host_vtable_t declared = *full.vtable;
  declared.struct_size = short_size;
  auto truncated = std::make_unique<unsigned char[]>(short_size);
  std::memcpy(truncated.get(), &declared, short_size);
  const sdk::McpEndpointHostView view(
      PJ_mcp_endpoint_host_t{&host, reinterpret_cast<const PJ_mcp_endpoint_host_vtable_t*>(truncated.get())});

  EXPECT_FALSE(view.valid());
  EXPECT_FALSE(view.acquire());
  view.release();
  EXPECT_EQ(host.acquireCalls(), 0);
  EXPECT_EQ(host.releaseCalls(), 0);
}

// One-service registry: proves the trait's name and Raw/Vtable/View wiring.
struct OneServiceRegistry {
  PJ_service_t service{};
};

bool getOneService(
    void* ctx, PJ_string_view_t name, uint32_t min_version, PJ_service_t* out_service, PJ_error_t* out_error) noexcept {
  if (sdk::toStringView(name) != "pj.mcp_endpoint.v1" || min_version > 1) {
    sdk::fillError(out_error, PJ_ERROR_CODE_REJECTED, "registry", "unknown service");
    return false;
  }
  *out_service = static_cast<OneServiceRegistry*>(ctx)->service;
  return true;
}

TEST(McpEndpointHostService, ResolvesThroughRegistry) {
  static_assert(sdk::detail::isValidServiceName(sdk::McpEndpointHostService::kName));
  EXPECT_EQ(std::string_view(sdk::McpEndpointHostService::kName), "pj.mcp_endpoint.v1");

  FakeMcpEndpointHost host;
  const PJ_mcp_endpoint_host_t raw = host.makeHost();
  OneServiceRegistry backing{PJ_service_t{raw.ctx, raw.vtable}};
  const PJ_service_registry_vtable_t registry_vtable{1, sizeof(PJ_service_registry_vtable_t), getOneService};
  const sdk::ServiceRegistry registry(PJ_service_registry_t{&backing, &registry_vtable});

  auto view = registry.get<sdk::McpEndpointHostService>();
  ASSERT_TRUE(view.has_value());
  auto endpoint = view->acquire();
  ASSERT_TRUE(endpoint) << endpoint.error();
  EXPECT_EQ(endpoint->url, host.url);
  EXPECT_EQ(endpoint->bearer_token, host.token());
}

}  // namespace
}  // namespace PJ
