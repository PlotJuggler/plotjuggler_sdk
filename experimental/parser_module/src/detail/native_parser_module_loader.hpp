#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <string>
#include <string_view>

#include "detail/library_loader.hpp"
#include "pj_base/expected.hpp"

namespace PJ::detail {

using NativeModuleHandle = void*;

/// Open a native parser module from its UTF-8 path.
inline Expected<NativeModuleHandle> openNativeParserModule(
    std::string_view path, LibraryPathIdentity* recorded_path = nullptr) {
  auto native = pathFromUtf8Argument(path);
  if (!native) {
    return unexpected("native parser-module path is not valid UTF-8");
  }
  return loadLibraryHandle(*native, recorded_path);
}

/// Resolve a required parser-module export and apply the same defining-module
/// provenance policy as the family plugin loaders.
inline Expected<void*> resolveNativeParserModuleSymbol(
    NativeModuleHandle handle, const char* name, const LibraryPathIdentity& candidate_path) {
  if (handle == nullptr) {
    return unexpected("native parser module is not loaded");
  }
  return resolveSymbol(handle, name, candidate_path);
}

}  // namespace PJ::detail
