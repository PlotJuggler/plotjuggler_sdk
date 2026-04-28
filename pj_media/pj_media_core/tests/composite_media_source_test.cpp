#include "pj_media_core/composite_media_source.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "pj_media_core/media_source.h"

namespace PJ {
namespace {

// Mock source that emits a configurable MediaFrame on the next takeFrame()
// after setTimestamp(). Tracks how many times setTimestamp was called.
class MockSource final : public MediaSource {
 public:
  int set_calls = 0;
  bool emit_base = false;
  int n_overlays = 0;
  int64_t base_w = 0;

  void setTimestamp(int64_t /*ts_ns*/) override {
    ++set_calls;
    pending_.emplace();
    if (emit_base) {
      DecodedFrame df;
      df.width = static_cast<int>(base_w);
      pending_->base = df;
    }
    for (int i = 0; i < n_overlays; ++i) {
      SceneFrame sf;
      sf.timestamp = static_cast<int64_t>(1000 + i);
      pending_->overlays.push_back(std::move(sf));
    }
  }

  std::optional<MediaFrame> takeFrame() override {
    if (!pending_.has_value()) {
      return std::nullopt;
    }
    auto out = std::move(*pending_);
    pending_.reset();
    return out;
  }

 private:
  std::optional<MediaFrame> pending_;
};

TEST(CompositeMediaSourceTest, EmptyCompositorReturnsNullopt) {
  CompositeMediaSource composite;
  composite.setTimestamp(0);
  EXPECT_FALSE(composite.takeFrame().has_value());
  EXPECT_EQ(composite.layerCount(), 0u);
}

TEST(CompositeMediaSourceTest, NullLayerIgnored) {
  CompositeMediaSource composite;
  composite.addLayer(nullptr);
  EXPECT_EQ(composite.layerCount(), 0u);
}

TEST(CompositeMediaSourceTest, ImageOnlyLayerForwarded) {
  auto image = std::make_unique<MockSource>();
  image->emit_base = true;
  image->base_w = 640;

  CompositeMediaSource composite;
  composite.addLayer(std::move(image));
  composite.setTimestamp(100);

  auto frame = composite.takeFrame();
  ASSERT_TRUE(frame.has_value());
  ASSERT_TRUE(frame->base.has_value());
  EXPECT_EQ(frame->base->width, 640);
  EXPECT_TRUE(frame->overlays.empty());
}

TEST(CompositeMediaSourceTest, OverlayOnlyLayerForwarded) {
  auto markers = std::make_unique<MockSource>();
  markers->n_overlays = 2;

  CompositeMediaSource composite;
  composite.addLayer(std::move(markers));
  composite.setTimestamp(100);

  auto frame = composite.takeFrame();
  ASSERT_TRUE(frame.has_value());
  EXPECT_FALSE(frame->base.has_value());
  EXPECT_EQ(frame->overlays.size(), 2u);
}

TEST(CompositeMediaSourceTest, ImagePlusMarkersFused) {
  auto image = std::make_unique<MockSource>();
  image->emit_base = true;
  image->base_w = 1920;
  auto markers = std::make_unique<MockSource>();
  markers->n_overlays = 3;

  CompositeMediaSource composite;
  composite.addLayer(std::move(image));
  composite.addLayer(std::move(markers));
  composite.setTimestamp(0);

  auto frame = composite.takeFrame();
  ASSERT_TRUE(frame.has_value());
  ASSERT_TRUE(frame->base.has_value());
  EXPECT_EQ(frame->base->width, 1920);
  EXPECT_EQ(frame->overlays.size(), 3u);
}

TEST(CompositeMediaSourceTest, MultipleOverlayLayersConcatenateInOrder) {
  auto first = std::make_unique<MockSource>();
  first->n_overlays = 2;
  auto second = std::make_unique<MockSource>();
  second->n_overlays = 1;

  CompositeMediaSource composite;
  composite.addLayer(std::move(first));
  composite.addLayer(std::move(second));
  composite.setTimestamp(0);

  auto frame = composite.takeFrame();
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->overlays.size(), 3u);
  // first layer's overlays come first (timestamps 1000, 1001), then second (1000)
  EXPECT_EQ(frame->overlays[0].timestamp, 1000);
  EXPECT_EQ(frame->overlays[1].timestamp, 1001);
  EXPECT_EQ(frame->overlays[2].timestamp, 1000);
}

TEST(CompositeMediaSourceTest, MultipleBasesFirstWins) {
  auto a = std::make_unique<MockSource>();
  a->emit_base = true;
  a->base_w = 100;
  auto b = std::make_unique<MockSource>();
  b->emit_base = true;
  b->base_w = 200;

  CompositeMediaSource composite;
  composite.addLayer(std::move(a));
  composite.addLayer(std::move(b));
  composite.setTimestamp(0);

  auto frame = composite.takeFrame();
  ASSERT_TRUE(frame.has_value());
  ASSERT_TRUE(frame->base.has_value());
  EXPECT_EQ(frame->base->width, 100);  // first layer's base wins
}

TEST(CompositeMediaSourceTest, SetTimestampForwardsToAllLayers) {
  auto a = std::make_unique<MockSource>();
  auto b = std::make_unique<MockSource>();
  auto* a_ptr = a.get();
  auto* b_ptr = b.get();

  CompositeMediaSource composite;
  composite.addLayer(std::move(a));
  composite.addLayer(std::move(b));

  composite.setTimestamp(100);
  composite.setTimestamp(200);
  composite.setTimestamp(300);

  EXPECT_EQ(a_ptr->set_calls, 3);
  EXPECT_EQ(b_ptr->set_calls, 3);
}

TEST(CompositeMediaSourceTest, NoNewDataReturnsNullopt) {
  auto layer = std::make_unique<MockSource>();
  CompositeMediaSource composite;
  composite.addLayer(std::move(layer));

  // No setTimestamp call → mock's pending_ is empty → takeFrame returns nullopt
  EXPECT_FALSE(composite.takeFrame().has_value());
}

}  // namespace
}  // namespace PJ
