#pragma once
#include <stddef.h>
#include <stdint.h>

// Exact-size, single-image upload lifecycle. The transport handles access
// control and calls activate() only after the entire request has completed.
template <class Updater> class FirmwareUpload {
public:
  explicit FirmwareUpload(Updater &updater) : updater_(updater) {}
  bool active() const { return phase_ == Receiving || phase_ == Received; }
  bool ready() const { return phase_ == Received; }
  const char *error() const { return error_; }
  void reset() {
    if (active()) updater_.abort();
    phase_ = Idle; expected_ = received_ = 0; error_ = "";
  }
  bool begin(size_t expected, size_t capacity) {
    if (phase_ != Idle) return fail("Send exactly one firmware file");
    if (expected < 24 || expected > capacity) return fail("Firmware does not fit the OTA partition");
    expected_ = expected;
    // Treat a failed begin as active until fail() cleans up any allocation.
    phase_ = Receiving;
    if (!updater_.begin(expected)) return fail(updater_.errorString());
    return true;
  }
  bool write(uint8_t *data, size_t size) {
    if (phase_ != Receiving) return fail("Unexpected firmware data");
    if (size > expected_ - received_) return fail("Firmware exceeds its declared size");
    if (updater_.write(data, size) != size) return fail(updater_.errorString());
    received_ += size;
    return true;
  }
  bool finishFile(size_t total) {
    if (phase_ != Receiving || total != expected_ || received_ != expected_)
      return fail("Incomplete firmware upload");
    phase_ = Received;
    return true;
  }
  bool activate() {
    if (phase_ != Received) return fail("Incomplete firmware upload");
    if (!updater_.end(false)) return fail(updater_.errorString());
    phase_ = Activated;
    return true;
  }
  bool fail(const char *reason) {
    if (phase_ == Failed) return false;
    error_ = reason; // Literal or updater-owned static error description.
    if (active()) updater_.abort();
    phase_ = Failed;
    return false;
  }
private:
  enum Phase { Idle, Receiving, Received, Failed, Activated };
  Updater &updater_;
  Phase phase_ = Idle;
  size_t expected_ = 0, received_ = 0;
  const char *error_ = "";
};
