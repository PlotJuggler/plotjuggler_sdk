#ifndef PJ_PLUGIN_DESCRIPTOR_SECTION_HPP
#define PJ_PLUGIN_DESCRIPTOR_SECTION_HPP

// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>

#include "pj_base/plugin_data_api.h"

// Statically discoverable plugin descriptor.
//
// The host reads a plugin's family and manifest straight out of the DSO image
// on disk, so discovery never has to dlopen the plugin. That matters because
// dlclose does not necessarily unmap what dlopen mapped: glibc pins a DSO
// NODELETE as soon as it is the FIRST PROVIDER of a name entered into the
// namespace's process-wide unique table (`do_lookup_unique` in glibc's
// `dl-lookup.c`). "First provider" is load-bearing here: a later copy of the
// same plugin whose unique names are already present in the table does not
// itself get pinned — it binds INTO the first copy's storage instead, which is
// what makes the duplicate-mapping bug so specific to the order things load in.
// Any vague-linkage static reaching `.dynsym` is a candidate for STB_GNU_UNIQUE
// binding (inline-function locals, template statics, Meyers singletons,
// thread_local, and their `__cxa_guard_*` guards). An inspect-then-close pass
// on a bundled plugin therefore leaves that plugin resident for the life of
// the process. If a second copy of the same plugin is later loaded from a
// different path, that copy binds its own references to the first copy's
// storage and finds its initialisation guards already set, so any layout drift
// between the two builds corrupts the process.
//
// Every DSO built with a PJ_*_PLUGIN macro carries one of these blobs per
// family it implements. The blob duplicates the manifest string that is also
// reachable through the vtable; the vtable copy stays authoritative once the
// plugin is genuinely loaded, and vtable-shape validation still happens there.
//
// The blob is located by SECTION, not by symbol name, so it survives stripping
// and needs no dynamic-symbol lookup.

// --- Section names -----------------------------------------------------------
//
// All three are defined on every platform: a plugin only ever emits into its
// own container's section, but the host-side reader is compiled everywhere so
// the object-file parsers can be tested on any machine.
//
// PE section names are capped at 8 bytes in the image section header, hence the
// abbreviated name on Windows. Mach-O needs the segment,section pair form.
#define PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF ".pj_manifest"
#define PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_PE ".pjmani"
#define PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_MACHO "__PJ,__manifest"

// --- Section placement -------------------------------------------------------
#if defined(_MSC_VER)
#define PJ_PLUGIN_DESCRIPTOR_SECTION_NAME PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_PE
#pragma section(".pjmani", read)
#define PJ_PLUGIN_DESCRIPTOR_PLACEMENT __declspec(allocate(".pjmani"))
// dllexport is what keeps the object alive through /OPT:REF: an unreferenced
// global in a custom section is otherwise a valid link-time removal.
#define PJ_PLUGIN_DESCRIPTOR_KEEP __declspec(dllexport)
#elif defined(__APPLE__)
#define PJ_PLUGIN_DESCRIPTOR_SECTION_NAME "__PJ,__manifest"
#define PJ_PLUGIN_DESCRIPTOR_PLACEMENT __attribute__((section("__PJ,__manifest")))
#define PJ_PLUGIN_DESCRIPTOR_KEEP __attribute__((visibility("default"), used))
#else
#define PJ_PLUGIN_DESCRIPTOR_SECTION_NAME ".pj_manifest"
#define PJ_PLUGIN_DESCRIPTOR_PLACEMENT __attribute__((section(".pj_manifest")))
// `used` keeps the compiler from dropping it; `retain` (SHF_GNU_RETAIN) keeps
// the linker from dropping it under --gc-sections. `visibility("default")`
// might look redundant next to those two, but it is deliberate belt-and-braces:
// on toolchains too old for `retain` / SHF_GNU_RETAIN, an exported symbol is
// itself a gc-root, and default visibility makes it exported. It is the same
// job `dllexport` does on MSVC.
#if defined(__has_attribute)
#if __has_attribute(retain)
#define PJ_PLUGIN_DESCRIPTOR_KEEP __attribute__((visibility("default"), used, retain))
#endif
#endif
#ifndef PJ_PLUGIN_DESCRIPTOR_KEEP
#define PJ_PLUGIN_DESCRIPTOR_KEEP __attribute__((visibility("default"), used))
#endif
#endif

namespace PJ::detail {

/// Family tags stored in the blob. Wire values — never renumber; the host's
/// PluginFamily mirrors them and static_asserts the correspondence.
enum PluginDescriptorFamily : uint32_t {
  kDescriptorFamilyUnknown = 0,
  kDescriptorFamilyDataSource = 1,
  kDescriptorFamilyMessageParser = 2,
  kDescriptorFamilyToolbox = 3,
  kDescriptorFamilyDialog = 4,
};

/// Identifies a blob when scanning the raw section bytes. Not NUL-terminated.
inline constexpr char kPluginDescriptorMagic[8] = {'P', 'J', 'P', 'L', 'U', 'G', 'I', 'N'};

/// Bumped only if the header below changes shape. A reader that does not know
/// a blob's version skips that blob rather than misreading it.
inline constexpr uint32_t kPluginDescriptorBlobVersion = 1;

/// Fixed-size prologue of every blob. All fields are written in the DSO's
/// native byte order; a reader parsing a foreign-endian image byte-swaps them
/// using the endianness declared by the container format's own header.
struct PluginDescriptorBlobHeader {
  char magic[8];
  uint32_t blob_version;
  /// Total bytes of this blob, header and trailing padding included. Lets a
  /// reader walk a section holding several blobs without parsing each manifest.
  uint32_t blob_size;
  /// PJ_ABI_VERSION the plugin was compiled against.
  uint32_t abi_version;
  /// One of PluginDescriptorFamily.
  uint32_t family;
  /// Manifest length in bytes, excluding the NUL terminator.
  uint32_t manifest_size;
  uint32_t reserved;
};
static_assert(sizeof(PluginDescriptorBlobHeader) == 32, "descriptor blob header is a wire format");

/// A header immediately followed by the manifest text. 8-byte alignment makes
/// every blob_size a multiple of 8, so consecutive blobs contributed by
/// different translation units stay walkable without gaps.
template <std::size_t JsonBytes>
struct alignas(8) PluginDescriptorBlob {
  PluginDescriptorBlobHeader header;
  char manifest_json[JsonBytes];
};

/// Length of a NUL-terminated manifest, excluding the terminator.
///
/// Exists so the macro can size the blob exactly. Plugins spell their manifest
/// either as a `char[]` (the CMake-generated headers) or as a
/// `constexpr const char*` (hand-written ones), so the length cannot simply be
/// deduced from an array parameter.
constexpr std::size_t manifestLength(const char* manifest_json) {
  std::size_t length = 0;
  while (manifest_json[length] != '\0') {
    ++length;
  }
  return length;
}

/// Builds a blob from a NUL-terminated manifest. `JsonBytes` counts the
/// terminator, so it is manifestLength() + 1.
template <std::size_t JsonBytes>
constexpr PluginDescriptorBlob<JsonBytes> makePluginDescriptorBlob(uint32_t family, const char* manifest_json) {
  static_assert(JsonBytes >= 2, "manifest must be a non-empty NUL-terminated string");
  PluginDescriptorBlob<JsonBytes> blob{};
  for (std::size_t i = 0; i < sizeof(kPluginDescriptorMagic); ++i) {
    blob.header.magic[i] = kPluginDescriptorMagic[i];
  }
  blob.header.blob_version = kPluginDescriptorBlobVersion;
  blob.header.blob_size = static_cast<uint32_t>(sizeof(PluginDescriptorBlob<JsonBytes>));
  blob.header.abi_version = PJ_ABI_VERSION;
  blob.header.family = family;
  blob.header.manifest_size = static_cast<uint32_t>(JsonBytes - 1);
  blob.header.reserved = 0;
  for (std::size_t i = 0; i < JsonBytes; ++i) {
    blob.manifest_json[i] = manifest_json[i];
  }
  return blob;
}

}  // namespace PJ::detail

/// Emits one descriptor blob. `SymbolSuffix` keeps the names distinct when a
/// single DSO implements more than one family (a data source that also ships a
/// dialog, say).
///
/// The variable is non-const for the same reason `pj_plugin_abi_version` is: a
/// namespace-scope `const` has internal linkage in C++, which MSVC then refuses
/// to place with __declspec(dllexport). Nothing writes to it. Being a plain
/// global rather than a vague-linkage entity, it never acquires STB_GNU_UNIQUE
/// binding itself.
///
/// `constinit` is load-bearing, not decoration: the blob has to be present in
/// the image on disk. Without it, a manifest that is not a constant expression
/// would compile into a dynamic initialiser, leaving the section zero-filled on
/// disk and the whole static-discovery path silently reading nothing. It turns
/// that into a compile error instead.
#define PJ_EMBED_PLUGIN_DESCRIPTOR(SymbolSuffix, FamilyValue, ManifestJson)                                          \
  extern "C" {                                                                                                       \
  PJ_PLUGIN_DESCRIPTOR_KEEP PJ_PLUGIN_DESCRIPTOR_PLACEMENT constinit auto pj_plugin_descriptor_##SymbolSuffix =      \
      PJ::detail::makePluginDescriptorBlob<PJ::detail::manifestLength(ManifestJson) + 1>(FamilyValue, ManifestJson); \
  }

// Statically linked builds have no DSO to inspect, and one blob symbol per
// family would collide across the plugins folded into the host binary.
#ifdef PJ_STATIC_PLUGINS
#undef PJ_EMBED_PLUGIN_DESCRIPTOR
#define PJ_EMBED_PLUGIN_DESCRIPTOR(SymbolSuffix, FamilyValue, ManifestJson)
#endif

#endif  // PJ_PLUGIN_DESCRIPTOR_SECTION_HPP
