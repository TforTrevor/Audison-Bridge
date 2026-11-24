#pragma once
#include <Arduino.h>

// All application logs go to both USB serial and the current-boot RAM history.
// Bootloader and ESP-IDF internal diagnostics still go only to USB serial.
class BridgeConsole : public Print {
public:
  using Print::write;
  // Keep each complete line in one write when network and CAN tasks both log.
  size_t println(const char *text) { return printf("%s\n", text); }
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *data, size_t length) override;
};
extern BridgeConsole Console;

struct BridgeStats {
  bool canReady = false;
  bool listenOnly = true;
  int canState = -1;
  uint32_t canFrames = 0;
  uint32_t lastCanMs = 0;
  uint32_t rxMissed = 0;
  uint32_t rxOverruns = 0;
  uint32_t busErrors = 0;
  uint32_t rxErrors = 0;
  uint32_t txErrors = 0;
  uint32_t rxQueued = 0;
  uint32_t serialPackets = 0;
  uint32_t serialRxBytes = 0;
  uint32_t loops = 0;
  uint32_t maxLoopUs = 0;
  int volume = -1;
  int sentCode = -1;
  bool muted = false;
  bool muteKnown = false;
};

void debugInitConsole();
void debugBegin();
void debugPublishStats(const BridgeStats &stats);
bool debugMaintenanceActive();
