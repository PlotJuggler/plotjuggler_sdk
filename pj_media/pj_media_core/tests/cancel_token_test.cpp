#include "pj_media_core/cancel_token.h"

#include <gtest/gtest.h>

#include <thread>

namespace PJ {
namespace {

TEST(CancelTokenTest, InitiallyNotCancelled) {
  auto token = makeCancelToken();
  EXPECT_FALSE(token->isCancelled());
}

TEST(CancelTokenTest, CancelFlipsState) {
  auto token = makeCancelToken();
  token->cancel();
  EXPECT_TRUE(token->isCancelled());
}

TEST(CancelTokenTest, SharedAcrossThreads) {
  auto token = makeCancelToken();

  std::thread worker([token]() {
    while (!token->isCancelled()) {
      // spin
    }
  });

  token->cancel();
  worker.join();
  EXPECT_TRUE(token->isCancelled());
}

TEST(CancelTokenTest, NewTokenIsIndependent) {
  auto token1 = makeCancelToken();
  auto token2 = makeCancelToken();
  token1->cancel();
  EXPECT_TRUE(token1->isCancelled());
  EXPECT_FALSE(token2->isCancelled());
}

}  // namespace
}  // namespace PJ
