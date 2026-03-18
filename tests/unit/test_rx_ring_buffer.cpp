/// @file test_rx_ring_buffer.cpp
/// @brief Unit tests for RxRingBuffer — push, pop, overflow, capacity.

#include <gtest/gtest.h>
#include "elero/rx_ring_buffer.h"

namespace esphome {
namespace elero {

// Simple test item — smaller than RfPacketInfo but exercises the same mechanics
struct TestItem {
  int id{0};
  float value{0.0f};
};

TEST(RxRingBuffer, EmptyOnConstruction) {
  RxRingBuffer<TestItem, 4> buf;
  EXPECT_TRUE(buf.empty());
  EXPECT_FALSE(buf.full());
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.capacity(), 4u);
}

TEST(RxRingBuffer, PushAndPop) {
  RxRingBuffer<TestItem, 4> buf;

  TestItem in{.id = 42, .value = 3.14f};
  EXPECT_TRUE(buf.push(in));
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf.size(), 1u);

  TestItem out{};
  EXPECT_TRUE(buf.pop(out));
  EXPECT_EQ(out.id, 42);
  EXPECT_FLOAT_EQ(out.value, 3.14f);
  EXPECT_TRUE(buf.empty());
}

TEST(RxRingBuffer, FIFO_Order) {
  RxRingBuffer<TestItem, 4> buf;

  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(buf.push({.id = i, .value = static_cast<float>(i)}));
  }
  EXPECT_TRUE(buf.full());

  for (int i = 0; i < 4; ++i) {
    TestItem out{};
    EXPECT_TRUE(buf.pop(out));
    EXPECT_EQ(out.id, i) << "FIFO order violated at index " << i;
  }
  EXPECT_TRUE(buf.empty());
}

TEST(RxRingBuffer, FullRejectsPush) {
  RxRingBuffer<TestItem, 2> buf;

  EXPECT_TRUE(buf.push({.id = 1}));
  EXPECT_TRUE(buf.push({.id = 2}));
  EXPECT_TRUE(buf.full());

  // Push to full buffer returns false
  EXPECT_FALSE(buf.push({.id = 3}));
  EXPECT_EQ(buf.size(), 2u);

  // Existing items unaffected
  TestItem out{};
  EXPECT_TRUE(buf.pop(out));
  EXPECT_EQ(out.id, 1);
}

TEST(RxRingBuffer, EmptyPopReturnsFalse) {
  RxRingBuffer<TestItem, 4> buf;
  TestItem out{};
  EXPECT_FALSE(buf.pop(out));
}

TEST(RxRingBuffer, WrapAround) {
  RxRingBuffer<TestItem, 3> buf;

  // Fill and drain twice to force wrap-around
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 3; ++i) {
      EXPECT_TRUE(buf.push({.id = round * 10 + i}));
    }
    EXPECT_TRUE(buf.full());

    for (int i = 0; i < 3; ++i) {
      TestItem out{};
      EXPECT_TRUE(buf.pop(out));
      EXPECT_EQ(out.id, round * 10 + i);
    }
    EXPECT_TRUE(buf.empty());
  }
}

TEST(RxRingBuffer, InterleavedPushPop) {
  RxRingBuffer<TestItem, 4> buf;

  // Push 2, pop 1, push 2, pop 1 — exercises wrap-around with partial fill
  EXPECT_TRUE(buf.push({.id = 1}));
  EXPECT_TRUE(buf.push({.id = 2}));
  EXPECT_EQ(buf.size(), 2u);

  TestItem out{};
  EXPECT_TRUE(buf.pop(out));
  EXPECT_EQ(out.id, 1);

  EXPECT_TRUE(buf.push({.id = 3}));
  EXPECT_TRUE(buf.push({.id = 4}));
  EXPECT_EQ(buf.size(), 3u);

  EXPECT_TRUE(buf.pop(out));
  EXPECT_EQ(out.id, 2);
  EXPECT_TRUE(buf.pop(out));
  EXPECT_EQ(out.id, 3);
  EXPECT_TRUE(buf.pop(out));
  EXPECT_EQ(out.id, 4);
  EXPECT_TRUE(buf.empty());
}

TEST(RxRingBuffer, CapacityOne) {
  RxRingBuffer<TestItem, 1> buf;

  EXPECT_TRUE(buf.push({.id = 99}));
  EXPECT_TRUE(buf.full());
  EXPECT_FALSE(buf.push({.id = 100}));

  TestItem out{};
  EXPECT_TRUE(buf.pop(out));
  EXPECT_EQ(out.id, 99);
  EXPECT_TRUE(buf.empty());

  // Can reuse after drain
  EXPECT_TRUE(buf.push({.id = 100}));
  EXPECT_TRUE(buf.pop(out));
  EXPECT_EQ(out.id, 100);
}

}  // namespace elero
}  // namespace esphome
