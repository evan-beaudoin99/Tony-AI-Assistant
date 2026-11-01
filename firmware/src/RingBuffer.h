#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <atomic>

// Single-producer / single-consumer byte ring: one task writes, another reads, no locks.
// Capacity must be a power of two so the free-running indices can wrap.
class ByteRing {
 public:
  bool begin(size_t capacity, bool psram) {
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) return false;
    uint32_t caps = (psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL) | MALLOC_CAP_8BIT;
    buf_ = static_cast<uint8_t*>(heap_caps_malloc(capacity, caps));
    cap_ = buf_ ? capacity : 0;
    return buf_ != nullptr;
  }

  size_t used() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  size_t space() const { return cap_ - used(); }

  // Producer side.
  size_t write(const uint8_t* src, size_t n) {
    size_t head = head_.load(std::memory_order_relaxed);
    size_t tail = tail_.load(std::memory_order_acquire);
    n = min(n, cap_ - (head - tail));
    size_t at = head & (cap_ - 1);
    size_t first = min(n, cap_ - at);
    memcpy(buf_ + at, src, first);
    memcpy(buf_, src + first, n - first);
    head_.store(head + n, std::memory_order_release);
    return n;
  }

  // Consumer side.
  size_t read(uint8_t* dst, size_t n) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t head = head_.load(std::memory_order_acquire);
    n = min(n, head - tail);
    size_t at = tail & (cap_ - 1);
    size_t first = min(n, cap_ - at);
    memcpy(dst, buf_ + at, first);
    memcpy(dst + first, buf_, n - first);
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }

  // Consumer side: drop everything buffered.
  void clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

 private:
  uint8_t* buf_ = nullptr;
  size_t cap_ = 0;
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
};
