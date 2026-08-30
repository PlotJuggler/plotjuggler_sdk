// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Install-test probe for the descriptor_import_support component: exercises
// one symbol from each public header through the exported CMake target.

#include <cstdio>
#include <filesystem>
#include <pj_base/sdk/descriptor_import/origin.hpp>
#include <pj_base/sdk/descriptor_import/provider_job.hpp>
#include <pj_base/sdk/descriptor_import/request_cache.hpp>
#include <pj_base/sdk/descriptor_import/source_descriptor.hpp>
#include <string>

int main() {
  namespace di = PJ::sdk::descriptor_import;

  di::OriginPolicy origin_policy;
  origin_policy.allowed_schemes = {"grpc", "grpc+tls"};
  const auto origin = di::parseOrigin("grpc+tls://example.org:6726", origin_policy);

  di::SourceDescriptorPolicy descriptor_policy;
  descriptor_policy.identity_fields = {"v", "kind"};
  descriptor_policy.identity = di::IdentityScheme{"probe:v1:sha256/128:", 32};
  const auto descriptor = di::parseSourceDescriptor(R"({"v":1,"kind":"probe"})", descriptor_policy);
  const std::string identity =
      descriptor ? di::sourceDescriptorIdentity(*descriptor, descriptor_policy) : descriptor.error();

  di::CacheSpec spec;
  spec.root = std::filesystem::temp_directory_path() / "pj-descriptor-import-probe";
  spec.artifact_suffix = ".probe";
  spec.identity = descriptor_policy.identity;
  di::RequestArtifactCache cache(
      spec, [](const std::filesystem::path&, const std::string&, std::string*) { return true; });

  di::SettlementLatch latch;
  latch.settle(true, "probe");

  std::printf(
      "origin=%s identity=%s path=%s settled=%d\n", origin ? di::originKey(*origin).c_str() : "-", identity.c_str(),
      descriptor ? cache.pathFor(identity).string().c_str() : "-", latch.settled() ? 1 : 0);
  return 0;
}
