#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "nanocdr/nanocdr.hpp"
#include "pj_media_core/scene_decoder.h"

namespace PJ {
namespace {

// Stable color per class_id. Vivid hues with sufficient contrast to tell
// neighbouring classes apart on top of arbitrary camera frames. Reused across
// frames so the same class keeps the same color throughout playback.
constexpr ColorRGBA kClassPalette[] = {
    {  0, 255,   0, 255},  // green
    {255,  64,  64, 255},  // red
    { 64, 128, 255, 255},  // blue
    {255, 192,   0, 255},  // amber
    {255,   0, 255, 255},  // magenta
    {  0, 255, 255, 255},  // cyan
    {255, 128,   0, 255},  // orange
    {128, 255,   0, 255},  // lime
    {200, 100, 255, 255},  // violet
    {255, 200, 200, 255},  // pink
};
constexpr ColorRGBA colorForClass(int32_t class_id) {
  // Negative ids fold into the palette via unsigned wrap.
  auto idx = static_cast<uint32_t>(class_id) % (sizeof(kClassPalette) / sizeof(kClassPalette[0]));
  return kClassPalette[idx];
}

// CDR decoder for ROS 2 vision_msgs/msg/Detection2DArray.
//
// Wire layout (CDR, after the 4-byte CDR header):
//   header.stamp.sec       uint32
//   header.stamp.nanosec   uint32
//   header.frame_id        string
//   detections             Detection2D[]
//
// Detection2D:
//   header                 std_msgs/Header
//   results                ObjectHypothesisWithPose[]
//   bbox                   BoundingBox2D
//   id                     string
//
// We discard everything except the bboxes — each bounding box becomes a
// 4-corner LineLoop in the resulting SceneFrame.
class CdrDetection2DArrayDecoder final : public ISceneDecoder {
 public:
  Expected<SceneFrame> decode(const uint8_t* data, size_t size) override {
    if (data == nullptr || size < 4) {
      return unexpected(std::string("CDR Detection2DArray: buffer too small"));
    }
    try {
      nanocdr::Decoder dec(nanocdr::ConstBuffer(data, size));

      // Outer std_msgs/Header
      uint32_t sec = 0;
      uint32_t nanosec = 0;
      dec.decode(sec);
      dec.decode(nanosec);
      std::string outer_frame_id;
      dec.decode(outer_frame_id);
      const Timestamp top_ts = static_cast<int64_t>(sec) * 1'000'000'000LL + static_cast<int64_t>(nanosec);

      // detections[]
      std::vector<PointsAnnotation> bboxes;
      uint32_t n_detections = 0;
      dec.decode(n_detections);
      bboxes.reserve(n_detections);

      for (uint32_t i = 0; i < n_detections; ++i) {
        // per-detection Header
        uint32_t det_sec = 0;
        uint32_t det_nanosec = 0;
        dec.decode(det_sec);
        dec.decode(det_nanosec);
        std::string det_frame_id;
        dec.decode(det_frame_id);

        // results[]: skip everything (class_id + score + PoseWithCovariance{7 doubles + 36 doubles})
        uint32_t n_results = 0;
        dec.decode(n_results);
        for (uint32_t j = 0; j < n_results; ++j) {
          std::string class_id;
          dec.decode(class_id);
          double score = 0;
          dec.decode(score);
          // PoseWithCovariance: position(3) + orientation(4) + covariance[36]
          for (int k = 0; k < 7 + 36; ++k) {
            double dummy = 0;
            dec.decode(dummy);
          }
        }

        // BoundingBox2D: center(Pose2D = x, y, theta) + size_x + size_y
        double cx = 0, cy = 0, theta = 0, sx = 0, sy = 0;
        dec.decode(cx);
        dec.decode(cy);
        dec.decode(theta);
        dec.decode(sx);
        dec.decode(sy);

        // id
        std::string id;
        dec.decode(id);

        PointsAnnotation bbox;
        bbox.topology = AnnotationTopology::kLineLoop;
        bbox.points = {
            {cx - sx / 2.0, cy - sy / 2.0},
            {cx + sx / 2.0, cy - sy / 2.0},
            {cx + sx / 2.0, cy + sy / 2.0},
            {cx - sx / 2.0, cy + sy / 2.0},
        };
        // vision_msgs/Detection2D doesn't carry a top-level class_id; the
        // class lives inside results[0].hypothesis. Until we parse that,
        // rotate through the palette by detection index so neighbouring
        // bboxes are at least visually distinguishable.
        bbox.color = colorForClass(static_cast<int32_t>(i));
        bbox.thickness = 2.0;
        bboxes.push_back(std::move(bbox));
      }

      ImageAnnotation ia;
      ia.timestamp = top_ts;
      ia.image_topic = outer_frame_id;  // best-effort link; demo wires explicitly
      ia.points = std::move(bboxes);

      SceneFrame sf;
      sf.timestamp = top_ts;
      sf.annotations.push_back(std::move(ia));
      return sf;
    } catch (const std::exception& e) {
      return unexpected(std::string("CDR Detection2DArray decode failed: ") + e.what());
    } catch (...) {
      return unexpected(std::string("CDR Detection2DArray decode failed: unknown error"));
    }
  }
};

// CDR decoder for yolo_msgs/msg/DetectionArray (https://github.com/mgonzs13/yolo_ros).
// Each Detection's BoundingBox2D becomes a 4-point LineLoop colored by class_id.
// Mask/keypoint fields exist in the wire payload but we decode-and-discard them.
class CdrYoloDetectionArrayDecoder final : public ISceneDecoder {
 public:
  Expected<SceneFrame> decode(const uint8_t* data, size_t size) override {
    if (data == nullptr || size < 4) {
      return unexpected(std::string("CDR yolo DetectionArray: buffer too small"));
    }
    try {
      nanocdr::Decoder dec(nanocdr::ConstBuffer(data, size));

      // Top-level std_msgs/Header
      uint32_t sec = 0;
      uint32_t nanosec = 0;
      dec.decode(sec);
      dec.decode(nanosec);
      std::string outer_frame_id;
      dec.decode(outer_frame_id);
      const Timestamp top_ts = static_cast<int64_t>(sec) * 1'000'000'000LL + static_cast<int64_t>(nanosec);

      std::vector<PointsAnnotation> bboxes;
      uint32_t n_detections = 0;
      dec.decode(n_detections);
      bboxes.reserve(n_detections);

      for (uint32_t i = 0; i < n_detections; ++i) {
        // Detection fields, in order
        int32_t class_id = 0;
        dec.decode(class_id);
        std::string class_name;
        dec.decode(class_name);
        double score = 0;
        dec.decode(score);
        std::string id;
        dec.decode(id);

        // BoundingBox2D = Pose2D{Point2D{x,y}, theta} + Vector2{x,y}
        double cx = 0, cy = 0, theta = 0, sx = 0, sy = 0;
        dec.decode(cx);
        dec.decode(cy);
        dec.decode(theta);
        dec.decode(sx);
        dec.decode(sy);

        // BoundingBox3D = Pose{Point3, Quaternion} + Vector3 + frame_id
        // 3 + 4 + 3 = 10 doubles, then string
        for (int k = 0; k < 10; ++k) {
          double dummy = 0;
          dec.decode(dummy);
        }
        std::string bbox3d_frame_id;
        dec.decode(bbox3d_frame_id);

        // Mask = int32 height + int32 width + Point2D[] data
        int32_t mask_h = 0, mask_w = 0;
        dec.decode(mask_h);
        dec.decode(mask_w);
        uint32_t n_mask = 0;
        dec.decode(n_mask);
        for (uint32_t j = 0; j < n_mask; ++j) {
          double mx = 0, my = 0;
          dec.decode(mx);
          dec.decode(my);
        }

        // KeyPoint2DArray = KeyPoint2D[] data, where KeyPoint2D = int32 id + Point2D + double score
        uint32_t n_kp2 = 0;
        dec.decode(n_kp2);
        for (uint32_t j = 0; j < n_kp2; ++j) {
          int32_t kp_id = 0;
          dec.decode(kp_id);
          double kx = 0, ky = 0, ks = 0;
          dec.decode(kx);
          dec.decode(ky);
          dec.decode(ks);
        }

        // KeyPoint3DArray = KeyPoint3D[] data + frame_id
        uint32_t n_kp3 = 0;
        dec.decode(n_kp3);
        for (uint32_t j = 0; j < n_kp3; ++j) {
          int32_t kp_id = 0;
          dec.decode(kp_id);
          double px = 0, py = 0, pz = 0, ks = 0;
          dec.decode(px);
          dec.decode(py);
          dec.decode(pz);
          dec.decode(ks);
        }
        std::string kp3_frame_id;
        dec.decode(kp3_frame_id);

        PointsAnnotation bbox;
        bbox.topology = AnnotationTopology::kLineLoop;
        bbox.points = {
            {cx - sx / 2.0, cy - sy / 2.0},
            {cx + sx / 2.0, cy - sy / 2.0},
            {cx + sx / 2.0, cy + sy / 2.0},
            {cx - sx / 2.0, cy + sy / 2.0},
        };
        bbox.color = colorForClass(class_id);
        bbox.thickness = 2.0;
        bboxes.push_back(std::move(bbox));
      }

      ImageAnnotation ia;
      ia.timestamp = top_ts;
      ia.image_topic = outer_frame_id;
      ia.points = std::move(bboxes);

      SceneFrame sf;
      sf.timestamp = top_ts;
      sf.annotations.push_back(std::move(ia));
      return sf;
    } catch (const std::exception& e) {
      return unexpected(std::string("CDR yolo DetectionArray decode failed: ") + e.what());
    } catch (...) {
      return unexpected(std::string("CDR yolo DetectionArray decode failed: unknown error"));
    }
  }
};

}  // namespace

std::unique_ptr<ISceneDecoder> makeSceneDecoderCdrDetection2DArray() {
  return std::make_unique<CdrDetection2DArrayDecoder>();
}

std::unique_ptr<ISceneDecoder> makeSceneDecoderCdrYoloDetectionArray() {
  return std::make_unique<CdrYoloDetectionArrayDecoder>();
}

}  // namespace PJ
