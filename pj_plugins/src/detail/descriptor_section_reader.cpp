// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "detail/descriptor_section_reader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <optional>
#include <string_view>

#include "pj_base/plugin_descriptor_section.hpp"

// Object-file parsing, deliberately hand-rolled against fixed field offsets
// rather than the platform's own headers (<elf.h>, <windows.h>,
// <mach-o/loader.h>). Two reasons: every format must compile on every host so
// the tests can exercise all three, and reading fields by offset sidesteps
// struct-packing and type-width differences between toolchains.
//
// Only what is needed to locate one named section is implemented. Anything not
// understood becomes an error, which the caller turns into a dlopen fallback —
// so being conservative here costs discovery speed, never correctness.

namespace PJ::detail {
namespace {

/// Copies `len` bytes at `offset` out of the image. False if the range is out
/// of bounds or unreadable, leaving `dst` untouched.
using ReadBytes = std::function<bool(uint64_t offset, size_t len, void* dst)>;

/// Bounds-checked scalar reads over an image, in the image's byte order.
class FieldReader {
 public:
  explicit FieldReader(const ReadBytes& read) : read_(read) {}

  void setBigEndian(bool big_endian) {
    big_endian_ = big_endian;
  }

  [[nodiscard]] std::optional<uint16_t> u16(uint64_t offset) const {
    return scalar<uint16_t>(offset);
  }
  [[nodiscard]] std::optional<uint32_t> u32(uint64_t offset) const {
    return scalar<uint32_t>(offset);
  }
  [[nodiscard]] std::optional<uint64_t> u64(uint64_t offset) const {
    return scalar<uint64_t>(offset);
  }

  /// Reads a 32- or 64-bit field as a 64-bit value, for the offset/size fields
  /// that differ in width between the 32- and 64-bit flavour of a format.
  [[nodiscard]] std::optional<uint64_t> word(uint64_t offset, bool wide) const {
    if (wide) {
      return u64(offset);
    }
    const auto narrow = u32(offset);
    return narrow ? std::optional<uint64_t>(*narrow) : std::nullopt;
  }

  /// Reads a fixed-width, NUL-padded name field (Mach-O segment and section
  /// names, PE section names).
  [[nodiscard]] std::optional<std::string> fixedName(uint64_t offset, size_t width) const {
    std::string raw(width, '\0');
    if (!read_(offset, width, raw.data())) {
      return std::nullopt;
    }
    const auto terminator = raw.find('\0');
    if (terminator != std::string::npos) {
      raw.resize(terminator);
    }
    return raw;
  }

  [[nodiscard]] bool bytes(uint64_t offset, size_t len, void* dst) const {
    return read_(offset, len, dst);
  }

 private:
  template <typename T>
  [[nodiscard]] std::optional<T> scalar(uint64_t offset) const {
    std::array<uint8_t, sizeof(T)> raw{};
    if (!read_(offset, sizeof(T), raw.data())) {
      return std::nullopt;
    }
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
      const size_t index = big_endian_ ? i : sizeof(T) - 1 - i;
      value = static_cast<T>((value << 8) | raw[index]);
    }
    return value;
  }

  const ReadBytes& read_;
  bool big_endian_ = false;
};

/// File offset and byte count of the descriptor section inside an image.
struct SectionRange {
  uint64_t offset = 0;
  uint64_t size = 0;
};

/// Absent value means "parsed fine, no such section" — distinct from an error,
/// which means the container could not be parsed at all.
using FoundSection = Expected<std::optional<SectionRange>>;

// --- Blob walking ------------------------------------------------------------

/// Blobs are 8-byte aligned by construction, but the linker leaves padding
/// between the contributions of different translation units, so the section is
/// scanned for the magic rather than walked blob_size to blob_size.
constexpr uint64_t kBlobScanStride = 8;

/// Largest manifest accepted. Guards against a corrupt length turning into a
/// multi-gigabyte allocation; real manifests are well under a kilobyte.
constexpr uint32_t kMaxManifestSize = 1u << 20;

[[nodiscard]] std::optional<EmbeddedDescriptor> readBlobAt(
    const FieldReader& reader, uint64_t blob_offset, uint64_t section_end) {
  std::array<char, sizeof(kPluginDescriptorMagic)> magic{};
  if (!reader.bytes(blob_offset, magic.size(), magic.data())) {
    return std::nullopt;
  }
  if (std::memcmp(magic.data(), kPluginDescriptorMagic, magic.size()) != 0) {
    return std::nullopt;
  }

  const auto blob_version = reader.u32(blob_offset + 8);
  const auto abi_version = reader.u32(blob_offset + 16);
  const auto family = reader.u32(blob_offset + 20);
  const auto manifest_size = reader.u32(blob_offset + 24);
  if (!blob_version || !abi_version || !family || !manifest_size) {
    return std::nullopt;
  }
  // A blob written by a newer SDK may have a different shape; skipping it is
  // the only safe reading.
  if (*blob_version != kPluginDescriptorBlobVersion) {
    return std::nullopt;
  }
  if (*manifest_size == 0 || *manifest_size > kMaxManifestSize) {
    return std::nullopt;
  }

  const uint64_t manifest_offset = blob_offset + sizeof(PluginDescriptorBlobHeader);
  if (manifest_offset + *manifest_size > section_end) {
    return std::nullopt;
  }

  EmbeddedDescriptor descriptor;
  descriptor.abi_version = *abi_version;
  descriptor.family = *family;
  descriptor.manifest_json.resize(*manifest_size);
  if (!reader.bytes(manifest_offset, *manifest_size, descriptor.manifest_json.data())) {
    return std::nullopt;
  }
  return descriptor;
}

[[nodiscard]] std::vector<EmbeddedDescriptor> readBlobs(const FieldReader& reader, const SectionRange& section) {
  std::vector<EmbeddedDescriptor> descriptors;
  const uint64_t end = section.offset + section.size;
  for (uint64_t at = section.offset; at + sizeof(PluginDescriptorBlobHeader) <= end; at += kBlobScanStride) {
    if (auto descriptor = readBlobAt(reader, at, end)) {
      descriptors.push_back(std::move(*descriptor));
    }
  }
  return descriptors;
}

// --- ELF ---------------------------------------------------------------------

constexpr uint32_t kElfSectionTypeNoBits = 8;  // SHT_NOBITS: occupies no file bytes
constexpr uint16_t kElfSectionIndexXindex = 0xFFFF;
constexpr uint8_t kElfClass32 = 1;
constexpr uint8_t kElfClass64 = 2;
constexpr uint8_t kElfDataLittleEndian = 1;
constexpr uint8_t kElfDataBigEndian = 2;

struct ElfShape {
  bool is_64 = false;
  uint64_t section_header_offset = 0;
  uint16_t section_header_size = 0;
  uint64_t section_count = 0;
  uint64_t string_table_index = 0;
};

struct ElfSectionHeader {
  uint32_t name_offset = 0;
  uint32_t type = 0;
  uint64_t file_offset = 0;
  uint64_t size = 0;
};

[[nodiscard]] std::optional<ElfSectionHeader> readElfSectionHeader(
    const FieldReader& reader, const ElfShape& shape, uint64_t index) {
  const uint64_t at = shape.section_header_offset + index * shape.section_header_size;
  const auto name_offset = reader.u32(at);
  const auto type = reader.u32(at + 4);
  // sh_offset and sh_size sit at different offsets in the two ELF classes.
  const auto file_offset = reader.word(at + (shape.is_64 ? 24 : 16), shape.is_64);
  const auto size = reader.word(at + (shape.is_64 ? 32 : 20), shape.is_64);
  if (!name_offset || !type || !file_offset || !size) {
    return std::nullopt;
  }
  return ElfSectionHeader{*name_offset, *type, *file_offset, *size};
}

[[nodiscard]] FoundSection findElfSection(FieldReader& reader, std::string_view wanted) {
  // EI_CLASS and EI_DATA are position-defined bytes, not endian-encoded values.
  std::array<uint8_t, 2> ident{};
  if (!reader.bytes(4, ident.size(), ident.data())) {
    return unexpected(std::string("ELF identification is truncated"));
  }
  if (ident[0] != kElfClass32 && ident[0] != kElfClass64) {
    return unexpected(std::string("unsupported ELF class"));
  }
  if (ident[1] != kElfDataLittleEndian && ident[1] != kElfDataBigEndian) {
    return unexpected(std::string("unsupported ELF data encoding"));
  }
  reader.setBigEndian(ident[1] == kElfDataBigEndian);

  ElfShape shape;
  shape.is_64 = ident[0] == kElfClass64;

  const auto section_header_offset = reader.word(shape.is_64 ? 0x28 : 0x20, shape.is_64);
  const auto section_header_size = reader.u16(shape.is_64 ? 0x3A : 0x2E);
  const auto section_count = reader.u16(shape.is_64 ? 0x3C : 0x30);
  const auto string_table_index = reader.u16(shape.is_64 ? 0x3E : 0x32);
  if (!section_header_offset || !section_header_size || !section_count || !string_table_index) {
    return unexpected(std::string("ELF header is truncated"));
  }
  if (*section_header_offset == 0 || *section_header_size == 0) {
    return std::optional<SectionRange>{};  // stripped of its section table
  }
  shape.section_header_offset = *section_header_offset;
  shape.section_header_size = *section_header_size;
  shape.section_count = *section_count;
  shape.string_table_index = *string_table_index;

  // Section counts and name-table indices too large for the 16-bit header
  // fields escape through section 0. The two escapes are independent.
  if (shape.section_count == 0) {
    const auto zero = readElfSectionHeader(reader, shape, 0);
    if (!zero) {
      return unexpected(std::string("ELF section table is truncated"));
    }
    shape.section_count = zero->size;
  }
  if (shape.string_table_index == kElfSectionIndexXindex) {
    const auto link = reader.u32(shape.section_header_offset + (shape.is_64 ? 40 : 24));
    if (!link) {
      return unexpected(std::string("ELF section table is truncated"));
    }
    shape.string_table_index = *link;
  }
  if (shape.string_table_index >= shape.section_count) {
    return unexpected(std::string("ELF section name table index is out of range"));
  }

  const auto string_table = readElfSectionHeader(reader, shape, shape.string_table_index);
  if (!string_table) {
    return unexpected(std::string("ELF section name table is unreadable"));
  }

  // Compared with its terminator included, so ".pj_manifest2" cannot match.
  std::string candidate(wanted.size() + 1, '\0');
  for (uint64_t index = 0; index < shape.section_count; ++index) {
    const auto header = readElfSectionHeader(reader, shape, index);
    if (!header) {
      return unexpected(std::string("ELF section table is truncated"));
    }
    if (header->type == kElfSectionTypeNoBits || header->size == 0) {
      continue;
    }
    if (header->name_offset >= string_table->size) {
      continue;
    }
    if (!reader.bytes(string_table->file_offset + header->name_offset, candidate.size(), candidate.data())) {
      continue;
    }
    if (candidate.compare(0, wanted.size(), wanted) == 0 && candidate[wanted.size()] == '\0') {
      return std::optional<SectionRange>({header->file_offset, header->size});
    }
  }
  return std::optional<SectionRange>{};
}

// --- PE ----------------------------------------------------------------------

constexpr uint64_t kPeSectionHeaderSize = 40;
constexpr size_t kPeSectionNameSize = 8;
constexpr uint64_t kPeCoffHeaderSize = 20;

[[nodiscard]] FoundSection findPeSection(const FieldReader& reader, std::string_view wanted) {
  const auto nt_offset = reader.u32(0x3C);
  if (!nt_offset) {
    return unexpected(std::string("PE header is truncated"));
  }
  std::array<char, 4> signature{};
  if (!reader.bytes(*nt_offset, signature.size(), signature.data())) {
    return unexpected(std::string("PE header is truncated"));
  }
  if (signature[0] != 'P' || signature[1] != 'E' || signature[2] != '\0' || signature[3] != '\0') {
    return unexpected(std::string("missing PE signature"));
  }

  const uint64_t coff = *nt_offset + 4;
  const auto section_count = reader.u16(coff + 2);
  const auto optional_header_size = reader.u16(coff + 16);
  if (!section_count || !optional_header_size) {
    return unexpected(std::string("PE header is truncated"));
  }

  const uint64_t table = coff + kPeCoffHeaderSize + *optional_header_size;
  for (uint16_t index = 0; index < *section_count; ++index) {
    const uint64_t at = table + index * kPeSectionHeaderSize;
    const auto name = reader.fixedName(at, kPeSectionNameSize);
    if (!name) {
      return unexpected(std::string("PE section table is truncated"));
    }
    if (*name != wanted) {
      continue;
    }
    const auto virtual_size = reader.u32(at + 8);
    const auto raw_size = reader.u32(at + 16);
    const auto raw_offset = reader.u32(at + 20);
    if (!virtual_size || !raw_size || !raw_offset) {
      return unexpected(std::string("PE section table is truncated"));
    }
    // SizeOfRawData is rounded up to the file alignment, so the payload ends at
    // VirtualSize whenever that is the smaller of the two.
    const uint64_t size = *virtual_size == 0 ? *raw_size : std::min<uint64_t>(*virtual_size, *raw_size);
    return std::optional<SectionRange>({*raw_offset, size});
  }
  return std::optional<SectionRange>{};
}

// --- Mach-O ------------------------------------------------------------------

constexpr uint32_t kMachOMagic64 = 0xFEEDFACFu;
constexpr uint32_t kMachOMagic32 = 0xFEEDFACEu;
constexpr uint32_t kMachOCommandSegment32 = 0x01;
constexpr uint32_t kMachOCommandSegment64 = 0x19;
constexpr size_t kMachOFixedNameSize = 16;

/// `wanted` is the "SEGMENT,SECTION" pair, spelled as the section attribute
/// spells it.
[[nodiscard]] FoundSection findMachOSection(const FieldReader& reader, bool is_64, std::string_view wanted) {
  const auto comma = wanted.find(',');
  if (comma == std::string_view::npos) {
    return unexpected(std::string("Mach-O section name must be SEGMENT,SECTION"));
  }
  const std::string_view wanted_segment = wanted.substr(0, comma);
  const std::string_view wanted_section = wanted.substr(comma + 1);

  const auto command_count = reader.u32(16);
  if (!command_count) {
    return unexpected(std::string("Mach-O header is truncated"));
  }

  uint64_t at = is_64 ? 32 : 28;
  for (uint32_t index = 0; index < *command_count; ++index) {
    const auto command = reader.u32(at);
    const auto command_size = reader.u32(at + 4);
    if (!command || !command_size || *command_size == 0) {
      return unexpected(std::string("Mach-O load commands are truncated"));
    }
    const bool segment_64 = *command == kMachOCommandSegment64;
    const bool segment_32 = *command == kMachOCommandSegment32;
    if (segment_64 || segment_32) {
      const auto segment_name = reader.fixedName(at + 8, kMachOFixedNameSize);
      const auto section_count = reader.u32(at + (segment_64 ? 64 : 48));
      if (!segment_name || !section_count) {
        return unexpected(std::string("Mach-O load commands are truncated"));
      }
      const uint64_t sections = at + (segment_64 ? 72 : 56);
      const uint64_t section_stride = segment_64 ? 80 : 68;
      for (uint32_t section = 0; section < *section_count; ++section) {
        const uint64_t section_at = sections + section * section_stride;
        const auto section_name = reader.fixedName(section_at, kMachOFixedNameSize);
        if (!section_name) {
          return unexpected(std::string("Mach-O section table is truncated"));
        }
        if (*section_name != wanted_section || *segment_name != wanted_segment) {
          continue;
        }
        const auto size = reader.word(section_at + (segment_64 ? 40 : 36), segment_64);
        const auto file_offset = reader.u32(section_at + (segment_64 ? 48 : 40));
        if (!size || !file_offset) {
          return unexpected(std::string("Mach-O section table is truncated"));
        }
        return std::optional<SectionRange>({*file_offset, *size});
      }
    }
    at += *command_size;
  }
  return std::optional<SectionRange>{};
}

// --- Dispatch ----------------------------------------------------------------

[[nodiscard]] constexpr uint32_t byteSwap32(uint32_t value) {
  return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) | ((value & 0x00FF0000u) >> 8) |
         ((value & 0xFF000000u) >> 24);
}

[[nodiscard]] FoundSection findDescriptorSection(FieldReader& reader, const std::array<uint8_t, 4>& head) {
  if (head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F') {
    return findElfSection(reader, PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_ELF);
  }
  if (head[0] == 'M' && head[1] == 'Z') {
    return findPeSection(reader, PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_PE);
  }

  const uint32_t magic = static_cast<uint32_t>(head[0]) | (static_cast<uint32_t>(head[1]) << 8) |
                         (static_cast<uint32_t>(head[2]) << 16) | (static_cast<uint32_t>(head[3]) << 24);
  const uint32_t swapped = byteSwap32(magic);
  const bool native = magic == kMachOMagic64 || magic == kMachOMagic32;
  const bool foreign = swapped == kMachOMagic64 || swapped == kMachOMagic32;
  if (!native && !foreign) {
    // Universal ("fat") Mach-O archives land here too: they are not parsed, and
    // the caller falls back to dlopen.
    return unexpected(std::string("unrecognised object file format"));
  }
  reader.setBigEndian(foreign);
  return findMachOSection(reader, (native ? magic : swapped) == kMachOMagic64, PJ_PLUGIN_DESCRIPTOR_SECTION_NAME_MACHO);
}

[[nodiscard]] Expected<std::vector<EmbeddedDescriptor>> readDescriptors(const ReadBytes& read) {
  FieldReader reader(read);
  std::array<uint8_t, 4> head{};
  if (!read(0, head.size(), head.data())) {
    return unexpected(std::string("image is too small to identify"));
  }

  auto section = findDescriptorSection(reader, head);
  if (!section) {
    return unexpected(section.error());
  }
  if (!section->has_value()) {
    return std::vector<EmbeddedDescriptor>{};
  }
  return readBlobs(reader, **section);
}

}  // namespace

Expected<std::vector<EmbeddedDescriptor>> readEmbeddedDescriptors(const std::filesystem::path& dso_path) {
  std::error_code ec;
  const auto file_size = static_cast<uint64_t>(std::filesystem::file_size(dso_path, ec));
  if (ec) {
    return unexpected("cannot size " + dso_path.string() + ": " + ec.message());
  }
  std::ifstream file(dso_path, std::ios::binary);
  if (!file) {
    return unexpected("cannot open " + dso_path.string());
  }

  const ReadBytes read = [&file, file_size](uint64_t offset, size_t len, void* dst) {
    if (len == 0 || offset > file_size || len > file_size - offset) {
      return false;
    }
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
      return false;
    }
    file.read(static_cast<char*>(dst), static_cast<std::streamsize>(len));
    return static_cast<bool>(file);
  };
  return readDescriptors(read);
}

Expected<std::vector<EmbeddedDescriptor>> readEmbeddedDescriptors(std::span<const std::byte> image) {
  const ReadBytes read = [image](uint64_t offset, size_t len, void* dst) {
    if (len == 0 || offset > image.size() || len > image.size() - offset) {
      return false;
    }
    std::memcpy(dst, image.data() + static_cast<size_t>(offset), len);
    return true;
  };
  return readDescriptors(read);
}

}  // namespace PJ::detail
