#pragma once
#include <atomic>
#include <cstdint>

// HTTP owns the pause request; the bridge snapshots it before transmitting.
// Each transition has a revision so even a quick pause/resume forces one sync.
class OutputControl {
public:
  uint32_t snapshot() const { return state_.load(); }
  static bool paused(uint32_t state) { return (state & 1u) != 0; }

  bool setPaused(bool pause) {
    uint32_t previous = state_.load();
    for (;;) {
      if (paused(previous) == pause) return false;
      const uint32_t next = ((previous & ~1u) + 2u) | (pause ? 1u : 0u);
      if (state_.compare_exchange_weak(previous, next)) return true;
    }
  }

private:
  std::atomic<uint32_t> state_{0};
};
