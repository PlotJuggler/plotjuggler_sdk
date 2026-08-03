// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Object-file parsing tests for the static plugin-descriptor reader.
//
// The containers here are built by hand rather than by compiling fixture DSOs:
// it keeps the suite hermetic (no cross-compiler needed for PE or Mach-O) and,
// more importantly, it pins the wire format. A test that produced its blobs
// with the same helper the emitter uses would cancel out a layout bug instead
// of catching it, so the bytes below are written out field by field.

#include "detail/descriptor_section_reader.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/plugin_descriptor_section.hpp"

namespace PJ::detail {
namespace {

/// Little- or big-endian byte assembler for synthetic images.
class ImageWriter {
 public:
  explicit ImageWriter(bool big_endian = false) : big_endian_(big_endian) {}

  void u8(uint8_t value) {
    bytes_.push_back(static_cast<std::byte>(value));
  }
  void u16(uint16_t value) {
    scalar(value, 2);
  }
  void u32(uint32_t value) {
    scalar(value, 4);
  }
  void u64(uint64_t value) {
    scalar(value, 8);
  }

  /// Writes `text` NUL-padded to exactly `width` bytes.
  void fixedName(std::string_view text, size_t width) {
    for (size_t i = 0; i < width; ++i) {
      u8(i < text.size() ? static_cast<uint8_t>(text[i]) : 0);
    }
  }

  void raw(const std::vector<std::byte>& blob) {
    bytes_.insert(bytes_.end(), blob.begin(), blob.end());
  }

  void padTo(size_t offset) {
    while (bytes_.size() < offset) {
      u8(0);
    }
  }

  /// Overwrites a previously reserved 32-bit field once its value is known.
  void patchU32(size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) {
      const size_t index = big_endian_ ? 3 - i : i;
      bytes_[offset + index] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
    }
  }

  [[nodiscard]] size_t size() const {
    return bytes_.size();
  }
  [[nodiscard]] const std::vector<std::byte>& bytes() const {
    return bytes_;
  }

 private:
  void scalar(uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) {
      const size_t shift = 8 * (big_endian_ ? width - 1 - i : i);
      u8(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
  }

  std::vector<std::byte> bytes_;
  bool big_endian_;
};

constexpr uint32_t kAbi = PJ_ABI_VERSION;

/// One descriptor blob, laid out field by field, padded to a multiple of 8 the
/// way alignas(8) pads the real thing. `big_endian` must match the container
/// the blob is embedded in — the blob carries no endianness marker of its own,
/// it inherits the image's.
std::vector<std::byte> makeBlob(
    uint32_t family, std::string_view manifest, uint32_t blob_version = kPluginDescriptorBlobVersion,
    uint32_t manifest_size_override = 0, bool big_endian = false) {
  ImageWriter writer(big_endian);
  for (char c : kPluginDescriptorMagic) {
    writer.u8(static_cast<uint8_t>(c));
  }
  const uint32_t json_bytes = static_cast<uint32_t>(manifest.size()) + 1;
  uint32_t blob_size = static_cast<uint32_t>(sizeof(PluginDescriptorBlobHeader)) + json_bytes;
  blob_size = (blob_size + 7U) & ~7U;
  writer.u32(blob_version);
  writer.u32(blob_size);
  writer.u32(kAbi);
  writer.u32(family);
  writer.u32(manifest_size_override != 0 ? manifest_size_override : static_cast<uint32_t>(manifest.size()));
  writer.u32(0);  // reserved
  for (char c : manifest) {
    writer.u8(static_cast<uint8_t>(c));
  }
  writer.padTo(blob_size);
  return writer.bytes();
}

// --- Synthetic containers ----------------------------------------------------

constexpr uint32_t kElfSectionTypeProgBits = 1;
constexpr uint32_t kElfSectionTypeStrTab = 3;

/// A minimal ELF with three sections: null, the payload section, .shstrtab.
std::vector<std::byte> makeElf(
    std::string_view section_name, const std::vector<std::byte>& payload, bool is_64 = true, bool big_endian = false) {
  const std::string names = std::string("\0", 1) + std::string(section_name) + std::string("\0.shstrtab\0", 11);
  const size_t header_size = is_64 ? 64 : 52;
  const size_t entry_size = is_64 ? 64 : 40;

  const size_t payload_offset = header_size;
  const size_t names_offset = payload_offset + payload.size();
  const size_t table_offset = names_offset + names.size();

  ImageWriter writer(big_endian);
  writer.u8(0x7F);
  writer.u8('E');
  writer.u8('L');
  writer.u8('F');
  writer.u8(is_64 ? 2 : 1);       // EI_CLASS
  writer.u8(big_endian ? 2 : 1);  // EI_DATA
  writer.padTo(16);               // rest of e_ident
  writer.u16(3);                  // e_type = ET_DYN
  writer.u16(62);                 // e_machine
  writer.u32(1);                  // e_version
  if (is_64) {
    writer.u64(0);             // e_entry
    writer.u64(0);             // e_phoff
    writer.u64(table_offset);  // e_shoff
  } else {
    writer.u32(0);
    writer.u32(0);
    writer.u32(static_cast<uint32_t>(table_offset));
  }
  writer.u32(0);                                   // e_flags
  writer.u16(static_cast<uint16_t>(header_size));  // e_ehsize
  writer.u16(0);                                   // e_phentsize
  writer.u16(0);                                   // e_phnum
  writer.u16(static_cast<uint16_t>(entry_size));   // e_shentsize
  writer.u16(3);                                   // e_shnum
  writer.u16(2);                                   // e_shstrndx
  writer.padTo(payload_offset);
  writer.raw(payload);
  for (char c : names) {
    writer.u8(static_cast<uint8_t>(c));
  }

  const auto section = [&](uint32_t name_offset, uint32_t type, uint64_t offset, uint64_t size) {
    writer.u32(name_offset);
    writer.u32(type);
    if (is_64) {
      writer.u64(0);       // sh_flags
      writer.u64(0);       // sh_addr
      writer.u64(offset);  // sh_offset
      writer.u64(size);    // sh_size
      writer.u32(0);       // sh_link
      writer.u32(0);       // sh_info
      writer.u64(8);       // sh_addralign
      writer.u64(0);       // sh_entsize
    } else {
      writer.u32(0);
      writer.u32(0);
      writer.u32(static_cast<uint32_t>(offset));
      writer.u32(static_cast<uint32_t>(size));
      writer.u32(0);
      writer.u32(0);
      writer.u32(8);
      writer.u32(0);
    }
  };
  section(0, 0, 0, 0);  // SHT_NULL
  section(1, kElfSectionTypeProgBits, payload_offset, payload.size());
  section(static_cast<uint32_t>(1 + section_name.size() + 1), kElfSectionTypeStrTab, names_offset, names.size());
  return writer.bytes();
}

/// A minimal PE image with one section. `raw_size` defaults to the payload size;
/// pass a larger value to model file-alignment padding past VirtualSize.
std::vector<std::byte> makePe(
    std::string_view section_name, const std::vector<std::byte>& payload, uint32_t raw_size = 0) {
  constexpr uint32_t kNtOffset = 0x80;
  const uint32_t virtual_size = static_cast<uint32_t>(payload.size());
  const uint32_t on_disk = raw_size != 0 ? raw_size : virtual_size;

  ImageWriter writer;
  writer.u8('M');
  writer.u8('Z');
  writer.padTo(0x3C);
  writer.u32(kNtOffset);
  writer.padTo(kNtOffset);
  writer.u8('P');
  writer.u8('E');
  writer.u8(0);
  writer.u8(0);
  writer.u16(0x8664);  // Machine
  writer.u16(1);       // NumberOfSections
  writer.u32(0);       // TimeDateStamp
  writer.u32(0);       // PointerToSymbolTable
  writer.u32(0);       // NumberOfSymbols
  writer.u16(0);       // SizeOfOptionalHeader
  writer.u16(0x2000);  // Characteristics: DLL

  const size_t payload_offset_field = writer.size() + 20;
  writer.fixedName(section_name, 8);
  writer.u32(virtual_size);
  writer.u32(0x1000);  // VirtualAddress
  writer.u32(on_disk);
  writer.u32(0);           // PointerToRawData — patched below
  writer.u32(0);           // PointerToRelocations
  writer.u32(0);           // PointerToLinenumbers
  writer.u16(0);           // NumberOfRelocations
  writer.u16(0);           // NumberOfLinenumbers
  writer.u32(0x40000040);  // Characteristics: initialised, read

  const auto payload_offset = static_cast<uint32_t>(writer.size());
  writer.patchU32(payload_offset_field, payload_offset);
  writer.raw(payload);
  writer.padTo(payload_offset + on_disk);
  return writer.bytes();
}

constexpr uint32_t kMachOCommandSegment64 = 0x19;

/// A minimal 64-bit Mach-O with a single segment holding a single section.
std::vector<std::byte> makeMachO(
    std::string_view segment_name, std::string_view section_name, const std::vector<std::byte>& payload) {
  constexpr uint32_t kSegmentCommandSize = 72 + 80;
  constexpr size_t kPayloadOffset = 32 + kSegmentCommandSize;

  ImageWriter writer;
  writer.u32(0xFEEDFACFu);  // MH_MAGIC_64
  writer.u32(0x0100000C);   // cputype
  writer.u32(0);            // cpusubtype
  writer.u32(6);            // filetype = MH_DYLIB
  writer.u32(1);            // ncmds
  writer.u32(kSegmentCommandSize);
  writer.u32(0);  // flags
  writer.u32(0);  // reserved

  writer.u32(kMachOCommandSegment64);
  writer.u32(kSegmentCommandSize);
  writer.fixedName(segment_name, 16);
  writer.u64(0);               // vmaddr
  writer.u64(payload.size());  // vmsize
  writer.u64(kPayloadOffset);  // fileoff
  writer.u64(payload.size());  // filesize
  writer.u32(1);               // maxprot
  writer.u32(1);               // initprot
  writer.u32(1);               // nsects
  writer.u32(0);               // flags

  writer.fixedName(section_name, 16);
  writer.fixedName(segment_name, 16);
  writer.u64(0);                                      // addr
  writer.u64(payload.size());                         // size
  writer.u32(static_cast<uint32_t>(kPayloadOffset));  // offset
  writer.u32(3);                                      // align
  writer.u32(0);                                      // reloff
  writer.u32(0);                                      // nreloc
  writer.u32(0);                                      // flags
  writer.u32(0);                                      // reserved1
  writer.u32(0);                                      // reserved2
  writer.u32(0);                                      // reserved3

  writer.padTo(kPayloadOffset);
  writer.raw(payload);
  return writer.bytes();
}

constexpr std::string_view kManifestA = R"({"id":"a","name":"A","version":"1.0.0"})";
constexpr std::string_view kManifestB = R"({"id":"b","name":"B","version":"2.0.0"})";

std::vector<std::byte> concat(const std::vector<std::byte>& first, const std::vector<std::byte>& second) {
  std::vector<std::byte> joined = first;
  joined.insert(joined.end(), second.begin(), second.end());
  return joined;
}

// --- ELF ---------------------------------------------------------------------

TEST(DescriptorSectionReader, ReadsDescriptorFromElf64) {
  const auto image = makeElf(PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF, makeBlob(kDescriptorFamilyDataSource, kManifestA));

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  ASSERT_EQ(descriptors->size(), 1U);
  EXPECT_EQ(descriptors->front().family, kDescriptorFamilyDataSource);
  EXPECT_EQ(descriptors->front().abi_version, kAbi);
  EXPECT_EQ(descriptors->front().manifest_json, kManifestA);
}

TEST(DescriptorSectionReader, ReadsDescriptorFromElf32) {
  const auto image =
      makeElf(PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF, makeBlob(kDescriptorFamilyToolbox, kManifestA), /*is_64=*/false);

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  ASSERT_EQ(descriptors->size(), 1U);
  EXPECT_EQ(descriptors->front().family, kDescriptorFamilyToolbox);
  EXPECT_EQ(descriptors->front().manifest_json, kManifestA);
}

/// Blob fields are written in the DSO's native byte order, so a big-endian
/// image needs both the section table and the blob read big-endian.
TEST(DescriptorSectionReader, ReadsDescriptorFromBigEndianElf) {
  const auto blob = makeBlob(
      kDescriptorFamilyMessageParser, kManifestB, kPluginDescriptorBlobVersion, /*manifest_size_override=*/0,
      /*big_endian=*/true);
  const auto image = makeElf(PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF, blob, /*is_64=*/true, /*big_endian=*/true);

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  ASSERT_EQ(descriptors->size(), 1U);
  EXPECT_EQ(descriptors->front().family, kDescriptorFamilyMessageParser);
  EXPECT_EQ(descriptors->front().abi_version, kAbi);
  EXPECT_EQ(descriptors->front().manifest_json, kManifestB);
}

/// The linker pads between the contributions of different translation units, so
/// blobs are not adjacent. This is the case that forces a magic scan.
TEST(DescriptorSectionReader, ReadsSeveralDescriptorsSeparatedByPadding) {
  auto payload = makeBlob(kDescriptorFamilyDataSource, kManifestA);
  payload.resize(payload.size() + 24);  // linker padding
  payload = concat(payload, makeBlob(kDescriptorFamilyDialog, kManifestB));

  const auto image = makeElf(PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF, payload);
  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  ASSERT_EQ(descriptors->size(), 2U);
  EXPECT_EQ(descriptors->at(0).family, kDescriptorFamilyDataSource);
  EXPECT_EQ(descriptors->at(0).manifest_json, kManifestA);
  EXPECT_EQ(descriptors->at(1).family, kDescriptorFamilyDialog);
  EXPECT_EQ(descriptors->at(1).manifest_json, kManifestB);
}

TEST(DescriptorSectionReader, ElfWithoutDescriptorSectionYieldsNoDescriptors) {
  const auto image = makeElf(".rodata", makeBlob(kDescriptorFamilyDataSource, kManifestA));

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  EXPECT_TRUE(descriptors->empty()) << "a plugin with no descriptor section must fall back, not fail";
}

TEST(DescriptorSectionReader, SectionNameMustMatchExactly) {
  const auto image = makeElf(".pj_manifest_extra", makeBlob(kDescriptorFamilyDataSource, kManifestA));

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  EXPECT_TRUE(descriptors->empty()) << "a longer name sharing our prefix must not match";
}

// --- PE ----------------------------------------------------------------------

TEST(DescriptorSectionReader, ReadsDescriptorFromPe) {
  const auto image = makePe(PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_PE, makeBlob(kDescriptorFamilyMessageParser, kManifestB));

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  ASSERT_EQ(descriptors->size(), 1U);
  EXPECT_EQ(descriptors->front().family, kDescriptorFamilyMessageParser);
  EXPECT_EQ(descriptors->front().manifest_json, kManifestB);
}

/// SizeOfRawData is rounded up to the file alignment; the extra bytes are not
/// part of the section and must not be walked.
TEST(DescriptorSectionReader, PeIgnoresFileAlignmentPadding) {
  const auto blob = makeBlob(kDescriptorFamilyToolbox, kManifestA);
  const auto image = makePe(PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_PE, blob, static_cast<uint32_t>(blob.size()) + 512);

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  ASSERT_EQ(descriptors->size(), 1U);
  EXPECT_EQ(descriptors->front().manifest_json, kManifestA);
}

TEST(DescriptorSectionReader, PeWithoutDescriptorSectionYieldsNoDescriptors) {
  const auto image = makePe(".text", makeBlob(kDescriptorFamilyDataSource, kManifestA));

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  EXPECT_TRUE(descriptors->empty());
}

// --- Mach-O ------------------------------------------------------------------

TEST(DescriptorSectionReader, ReadsDescriptorFromMachO) {
  const auto image = makeMachO("__PJ", "__manifest", makeBlob(kDescriptorFamilyDialog, kManifestB));

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  ASSERT_EQ(descriptors->size(), 1U);
  EXPECT_EQ(descriptors->front().family, kDescriptorFamilyDialog);
  EXPECT_EQ(descriptors->front().manifest_json, kManifestB);
}

TEST(DescriptorSectionReader, MachOSegmentNameMustMatch) {
  const auto image = makeMachO("__DATA", "__manifest", makeBlob(kDescriptorFamilyDialog, kManifestB));

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  EXPECT_TRUE(descriptors->empty()) << "the right section name in the wrong segment is not our section";
}

// --- Malformed input ---------------------------------------------------------

TEST(DescriptorSectionReader, UnknownContainerIsAnError) {
  const std::vector<std::byte> image(64, std::byte{0x5A});

  auto descriptors = readEmbeddedDescriptors(image);
  EXPECT_FALSE(descriptors.has_value());
}

TEST(DescriptorSectionReader, EmptyImageIsAnError) {
  auto descriptors = readEmbeddedDescriptors(std::span<const std::byte>{});
  EXPECT_FALSE(descriptors.has_value());
}

TEST(DescriptorSectionReader, TruncatedElfIsAnError) {
  auto image = makeElf(PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF, makeBlob(kDescriptorFamilyDataSource, kManifestA));
  image.resize(40);  // cut inside the ELF header

  auto descriptors = readEmbeddedDescriptors(image);
  EXPECT_FALSE(descriptors.has_value());
}

TEST(DescriptorSectionReader, BlobFromAFutureLayoutIsSkipped) {
  const auto image = makeElf(
      PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF,
      makeBlob(kDescriptorFamilyDataSource, kManifestA, kPluginDescriptorBlobVersion + 1));

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  EXPECT_TRUE(descriptors->empty()) << "an unknown blob version must be skipped, not guessed at";
}

TEST(DescriptorSectionReader, ManifestRunningPastTheSectionIsSkipped) {
  const auto image = makeElf(
      PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF,
      makeBlob(kDescriptorFamilyDataSource, kManifestA, kPluginDescriptorBlobVersion, /*manifest_size_override=*/9999));

  auto descriptors = readEmbeddedDescriptors(image);
  ASSERT_TRUE(descriptors.has_value()) << descriptors.error();
  EXPECT_TRUE(descriptors->empty());
}

// --- File overload -----------------------------------------------------------

TEST(DescriptorSectionReader, FileAndMemoryOverloadsAgree) {
  const auto image = makeElf(PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF, makeBlob(kDescriptorFamilyDataSource, kManifestA));

  const auto path =
      std::filesystem::temp_directory_path() /
      ("pj_descriptor_reader_" +
       std::to_string(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())) + ".bin");
  {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
  }

  auto from_file = readEmbeddedDescriptors(path);
  std::error_code ec;
  std::filesystem::remove(path, ec);

  ASSERT_TRUE(from_file.has_value()) << from_file.error();
  ASSERT_EQ(from_file->size(), 1U);
  EXPECT_EQ(from_file->front().manifest_json, kManifestA);
}

TEST(DescriptorSectionReader, MissingFileIsAnError) {
  auto descriptors = readEmbeddedDescriptors(std::filesystem::path("/nonexistent/plugin.so"));
  EXPECT_FALSE(descriptors.has_value());
}

}  // namespace
}  // namespace PJ::detail
