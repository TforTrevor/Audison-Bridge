#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The caller provides synchronization. No allocation or flash writes.
template <size_t Capacity, size_t TextSize> class LogRing {
public:
  static_assert(Capacity > 0 && TextSize > 1, "Invalid log ring dimensions");
  struct Entry {
    uint64_t id;
    uint64_t ms;
    char text[TextSize];
  };
  void push(uint64_t ms, const char *text) {
    Entry &entry = entries_[(next_ - 1) % Capacity];
    entry.id = next_++;
    entry.ms = ms;
    size_t length = 0;
    while (length < TextSize - 1 && text[length]) ++length;
    memcpy(entry.text, text, length);
    entry.text[length] = '\0';
    if (count_ < Capacity) ++count_;
  }
  uint64_t oldest() const { return next_ - count_; }
  uint64_t latest() const { return next_ - 1; }
  size_t count() const { return count_; }
  // after=0 requests the oldest retained entry. A cursor past the latest entry
  // also restarts history (e.g. a client reconnecting after a device reboot).
  size_t copyAfter(uint64_t after, Entry *out, size_t limit, uint64_t &missed) const {
    missed = 0;
    uint64_t start = oldest();
    if (after && after < latest()) {
      start = after + 1;
      if (start < oldest()) { missed = oldest() - start; start = oldest(); }
    } else if (after == latest()) {
      return 0;
    }
    size_t copied = 0;
    for (uint64_t id = start; id < next_ && copied < limit; ++id) {
      out[copied++] = entries_[(id - 1) % Capacity];
    }
    return copied;
  }
private:
  Entry entries_[Capacity] = {};
  uint64_t next_ = 1;
  size_t count_ = 0;
};
