// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/service_registry_builder.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/service_traits.hpp"

// This TU is compiled with PJ_ASSERT_THROWS so the convenience overload's
// invariant is observable in every build type (a plain assert() is compiled
// away under NDEBUG, which is exactly how the missing check went unnoticed).
#ifndef PJ_ASSERT_THROWS
#error "service_registry_builder_test must be built with PJ_ASSERT_THROWS"
#endif

namespace {

// The builder only stores the fat pointer; it never dereferences the vtable,
// so a zeroed one is enough to make a service "valid".
const PJ_source_write_host_vtable_t kWriteVtable{};
int ctx_a = 0;
int ctx_b = 0;

PJ_source_write_host_t makeWriteHost(int* ctx) {
  return PJ_source_write_host_t{ctx, &kWriteVtable};
}

constexpr const char* kName = PJ::sdk::SourceWriteHostService::kName;

TEST(ServiceRegistryBuilderTest, FirstRegistrationSucceeds) {
  PJ::ServiceRegistryBuilder builder;

  const PJ::Status status = builder.tryRegisterService(kName, 1, PJ_service_t{&ctx_a, &kWriteVtable});

  EXPECT_TRUE(status.has_value()) << status.error();
  EXPECT_EQ(builder.size(), 1U);
}

TEST(ServiceRegistryBuilderTest, DuplicateNameIsRejectedAndKeepsTheFirstEntry) {
  PJ::ServiceRegistryBuilder builder;
  ASSERT_TRUE(builder.tryRegisterService(kName, 1, PJ_service_t{&ctx_a, &kWriteVtable}).has_value());

  const PJ::Status status = builder.tryRegisterService(kName, 2, PJ_service_t{&ctx_b, &kWriteVtable});

  ASSERT_FALSE(status.has_value());
  EXPECT_NE(status.error().find("duplicate name"), std::string::npos) << status.error();
  EXPECT_NE(status.error().find(kName), std::string::npos) << status.error();
  EXPECT_EQ(builder.size(), 1U);
}

TEST(ServiceRegistryBuilderTest, NullCtxOrVtableIsRejected) {
  PJ::ServiceRegistryBuilder builder;

  EXPECT_FALSE(builder.tryRegisterService(kName, 1, PJ_service_t{nullptr, &kWriteVtable}).has_value());
  EXPECT_FALSE(builder.tryRegisterService(kName, 1, PJ_service_t{&ctx_a, nullptr}).has_value());
  EXPECT_EQ(builder.size(), 0U);
}

// The convenience overload documents that it asserts; without that assert a
// duplicate silently changes the service surface a plugin binds against.
TEST(ServiceRegistryBuilderTest, ConvenienceOverloadAssertsOnDuplicate) {
  PJ::ServiceRegistryBuilder builder;
  builder.registerService(kName, 1, PJ_service_t{&ctx_a, &kWriteVtable});

  EXPECT_THROW(builder.registerService(kName, 1, PJ_service_t{&ctx_b, &kWriteVtable}), std::runtime_error);
  EXPECT_EQ(builder.size(), 1U);
}

TEST(ServiceRegistryBuilderTest, ConvenienceOverloadAssertsOnNullService) {
  PJ::ServiceRegistryBuilder builder;

  EXPECT_THROW(builder.registerService(kName, 1, PJ_service_t{nullptr, nullptr}), std::runtime_error);
  EXPECT_EQ(builder.size(), 0U);
}

// The templated path is the one every host actually calls, so it must inherit
// the same invariant rather than quietly dropping the second registration.
TEST(ServiceRegistryBuilderTest, TemplatedOverloadAssertsOnDuplicate) {
  PJ::ServiceRegistryBuilder builder;
  builder.registerService<PJ::sdk::SourceWriteHostService>(makeWriteHost(&ctx_a));

  EXPECT_THROW(builder.registerService<PJ::sdk::SourceWriteHostService>(makeWriteHost(&ctx_b)), std::runtime_error);
  EXPECT_EQ(builder.size(), 1U);
}

}  // namespace
