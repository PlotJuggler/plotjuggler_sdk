#include "pj_media_core/h264_utils.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
}

#include <cstring>

namespace PJ {

namespace {

// Find the next annex-B start code (3-byte 0x000001 or 4-byte 0x00000001).
// Returns the offset of the first byte of the start code, or size if not found.
size_t findStartCode(const uint8_t* data, size_t size, size_t offset) {
  while (offset + 2 < size) {
    if (data[offset] == 0x00 && data[offset + 1] == 0x00) {
      if (data[offset + 2] == 0x01) {
        return offset;
      }
      if (offset + 3 < size && data[offset + 2] == 0x00 && data[offset + 3] == 0x01) {
        return offset;
      }
    }
    ++offset;
  }
  return size;
}

// Given a start code at data[offset], return the offset of the NAL header byte
// (the byte after the start code).
size_t nalHeaderOffset(const uint8_t* data, size_t offset) {
  if (data[offset + 2] == 0x01) {
    return offset + 3;  // 3-byte start code
  }
  return offset + 4;  // 4-byte start code
}

}  // namespace

bool isH264Keyframe(const uint8_t* data, size_t size) {
  if (data == nullptr || size < 4) {
    return false;
  }

  size_t pos = findStartCode(data, size, 0);
  while (pos < size) {
    size_t nal_start = nalHeaderOffset(data, pos);
    if (nal_start < size) {
      uint8_t nal_type = data[nal_start] & 0x1F;
      if (nal_type == 5) {  // IDR slice
        return true;
      }
    }
    pos = findStartCode(data, size, nal_start);
  }
  return false;
}

std::vector<uint8_t> extractH264SpsPps(const uint8_t* data, size_t size) {
  std::vector<uint8_t> result;
  if (data == nullptr || size < 4) {
    return result;
  }

  size_t pos = findStartCode(data, size, 0);
  while (pos < size) {
    size_t nal_hdr = nalHeaderOffset(data, pos);
    if (nal_hdr >= size) {
      break;
    }

    uint8_t nal_type = data[nal_hdr] & 0x1F;

    // Find the end of this NAL unit (next start code or EOF)
    size_t next_sc = findStartCode(data, size, nal_hdr);
    size_t nal_end = next_sc;

    // SPS = 7, PPS = 8
    if (nal_type == 7 || nal_type == 8) {
      // Copy the full NAL with its start code
      result.insert(result.end(), data + pos, data + nal_end);
    }

    // Stop after we've passed the parameter sets (IDR or other slice NALs)
    if (nal_type == 5 || nal_type == 1) {
      break;
    }

    pos = next_sc;
  }
  return result;
}

AVCodecParameters* makeH264CodecParams(const uint8_t* keyframe_data, size_t keyframe_size) {
  AVCodecParameters* params = avcodec_parameters_alloc();
  if (params == nullptr) {
    return nullptr;
  }
  params->codec_type = AVMEDIA_TYPE_VIDEO;
  params->codec_id = AV_CODEC_ID_H264;

  // Extract SPS/PPS from keyframe and set as extradata for proper HW init
  if (keyframe_data != nullptr && keyframe_size > 0) {
    auto sps_pps = extractH264SpsPps(keyframe_data, keyframe_size);
    if (!sps_pps.empty()) {
      params->extradata = static_cast<uint8_t*>(av_mallocz(sps_pps.size() + AV_INPUT_BUFFER_PADDING_SIZE));
      if (params->extradata != nullptr) {
        std::memcpy(params->extradata, sps_pps.data(), sps_pps.size());
        params->extradata_size = static_cast<int>(sps_pps.size());
      }
    }
  }

  return params;
}

}  // namespace PJ
