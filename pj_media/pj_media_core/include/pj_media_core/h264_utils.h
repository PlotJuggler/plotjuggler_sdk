#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct AVCodecParameters;

namespace PJ {

/// Scan annex-B H.264 NAL units. Returns true if an IDR slice (NAL type 5) is found.
bool isH264Keyframe(const uint8_t* data, size_t size);

/// Extract SPS and PPS NAL units from an annex-B H.264 keyframe.
/// Returns the concatenated SPS+PPS data (with start codes), suitable
/// for use as AVCodecParameters::extradata.
std::vector<uint8_t> extractH264SpsPps(const uint8_t* data, size_t size);

/// Build AVCodecParameters for H.264 with SPS/PPS extradata from a keyframe.
/// The extradata enables proper HW decoder surface initialization (VAAPI/CUDA).
/// Caller must free with avcodec_parameters_free(&params).
AVCodecParameters* makeH264CodecParams(const uint8_t* keyframe_data = nullptr, size_t keyframe_size = 0);

}  // namespace PJ
