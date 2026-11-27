// Hyundai Elantra M-CAN -> Audison C8.14 ADC / RS232 volume bridge.
// Arduino IDE: ESP32 Dev Module; Serial Monitor: 115200 baud.
// Car: CAN_LISTEN_ONLY=true. Two-node USB CAN bench: set false and upload again.
// Wait for the DSP's steady blue LED; disconnect bitDrive before testing ADC.
// Set-volume commands have NO response. A bench loopback echo is not a DSP ACK.
// Removing the Waveshare termination resistor does not require code changes.
// Linear mapping: levels 0..44 cover -60..+6 dB in 1.5 dB steps. Level 45 is ignored.
#include <Arduino.h>
#include <driver/twai.h>
#include <esp_timer.h>
#include "BridgeDebug.h"

// Operating mode and optional incoming RS232 diagnostics.
constexpr bool CAN_LISTEN_ONLY = true;
constexpr bool DEBUG_RS232_RX = false;

// CAN transceiver GPIO connections.
constexpr gpio_num_t CAN_TX = GPIO_NUM_26;
constexpr gpio_num_t CAN_RX = GPIO_NUM_25;

// MAX3232 TTL-side GPIO connections.
constexpr int UART_TX = 23;
constexpr int UART_RX = 22;

// Hyundai CAN decoding: volume in 0x183 byte 0; mute in 0x181 byte 2, bit 0.
constexpr uint32_t HYUNDAI_CAN_ID = 0x183;
constexpr uint32_t HYUNDAI_MUTE_CAN_ID = 0x181;
constexpr uint8_t MUTE_BYTE_INDEX = 2;
constexpr uint8_t MUTE_MASK = 0x01;
constexpr uint8_t MAX_VOLUME = 44;

// Index is the head-unit display level (0..45); value is the CAN byte.
// Recovered from matching Hold Volume Up.asc / Hold Volume Down.asc sweeps.
// The encoding is not linear below 12 or above 39. Level 45 is decoded but ignored.
constexpr uint8_t HYUNDAI_VOLUME_RAW[] =
{
  0x00, 0x05, 0x0C, 0x12, 0x16, 0x19, 0x1C, 0x1F, 0x21, 0x23, // 0..9
  0x25, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, // 10..19
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, // 20..29
  0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, // 30..39
  0x45, 0x47, 0x48, 0x49, 0x4A, 0x4B                           // 40..45
};

// Audison volume coding and minimum time between commands.
constexpr uint8_t AUDISON_MIN_VOLUME = 0xA8;
constexpr uint8_t AUDISON_ZERO_DB = 0x30;
constexpr uint8_t AUDISON_CODES_PER_STEP = 3; // Three 0.5 dB codes per head-unit step.
constexpr uint32_t COMMAND_GAP_MS = 50;

static_assert(sizeof(HYUNDAI_VOLUME_RAW) == 46, "Hyundai table needs 46 entries");

// Audison UART, effective mute, and last observed Hyundai mute state.
HardwareSerial AudisonSerial(2);
int requestedVolume = -1;
bool requestedMute = false;
bool muteStateKnown = false;
bool lastCanMute = false;

// Pending/sent Audison commands and CAN initialization state.
int pendingCode = -1;
int lastSentCode = -1;
uint32_t lastSendMs = 0;
bool canReady = false;
BridgeStats liveStats;
uint32_t lastStatsMs = 0;

void printPacket(const char *label, const uint8_t *packet, size_t length)
{
  char text[193];
  size_t used = snprintf(text, sizeof(text), "%s", label);
  for (size_t i = 0; i < length; ++i)
  {
    if (used + 4 >= sizeof(text)) break;
    used += snprintf(text + used, sizeof(text) - used, "%02X%s",
                     packet[i], i + 1 == length ? "" : " ");
  }
  Console.println(text);
}

int decodeHyundaiVolume(uint8_t raw)
{
  for (size_t volume = 0; volume < sizeof(HYUNDAI_VOLUME_RAW); ++volume)
  {
    if (HYUNDAI_VOLUME_RAW[volume] == raw)
    {
      return int(volume);
    }
  }
  return -1;
}

void updatePendingVolume()
{
  if (requestedMute)
  {
    pendingCode = AUDISON_MIN_VOLUME;
  }
  else if (requestedVolume >= 0 && requestedVolume <= MAX_VOLUME)
  {
    pendingCode = AUDISON_MIN_VOLUME - requestedVolume * AUDISON_CODES_PER_STEP;
  }
  else
  {
    pendingCode = -1;
  }
}

void processCanMessage(const twai_message_t &message)
{
  if (message.extd || message.rtr || message.data_length_code < 1 ||
      message.data_length_code > 8)
  {
    return;
  }

  if (message.identifier == HYUNDAI_MUTE_CAN_ID)
  {
    if (message.data_length_code <= MUTE_BYTE_INDEX)
    {
      return;
    }
    bool muted = (message.data[MUTE_BYTE_INDEX] & MUTE_MASK) != 0;
    if (muteStateKnown && muted == lastCanMute)
    {
      return;
    }
    muteStateKnown = true;
    lastCanMute = muted;
    requestedMute = muted;
    updatePendingVolume();
    Console.printf("CAN mute received: %s\n", requestedMute ? "ON" : "OFF");
    return;
  }

  if (message.identifier != HYUNDAI_CAN_ID)
  {
    return;
  }

  uint8_t raw = message.data[0];
  int volume = decodeHyundaiVolume(raw);
  if (volume < 0 || volume > MAX_VOLUME || volume == requestedVolume)
  {
    return;
  }
  // The first volume frame establishes a baseline. A subsequent level change
  // cancels mute, matching the head unit. Repeated frames cannot cancel mute.
  if (requestedVolume >= 0 && requestedMute)
  {
    requestedMute = false;
    Console.println("CAN volume change: mute cleared");
  }
  requestedVolume = volume;
  updatePendingVolume();
  double pendingDb = (int(AUDISON_ZERO_DB) - pendingCode) * 0.5;
  Console.printf("CAN volume received: %d (raw 0x%02X) -> Audison: %.1f dB (0x%02X)%s\n",
                requestedVolume, raw, pendingDb, pendingCode, requestedMute ? " (muted)" : "");
  // Level 45 and values absent from the captured Hyundai table are ignored.
  // Repeated mute frames are compared with lastCanMute, so a stale ON frame
  // cannot re-mute after a volume change. A new OFF -> ON transition can.
  // Logs still occur on receipt, independently of the Audison command timer.
}

void sendPendingVolume()
{
  if (debugMaintenanceActive() || pendingCode < 0 || pendingCode == lastSentCode ||
      uint32_t(millis() - lastSendMs) < COMMAND_GAP_MS)
  {
    return;
  }

  uint8_t packet[] = {0x01, 0x04, 0x00, 0x01, uint8_t(pendingCode), 0x00, 0x00};
  uint16_t checksum = 0;
  for (size_t i = 0; i < 5; ++i)
  {
    checksum += packet[i];
  }
  packet[5] = uint8_t(checksum >> 8);
  packet[6] = uint8_t(checksum);
  AudisonSerial.write(packet, sizeof(packet));
  AudisonSerial.flush();
  lastSendMs = millis();
  lastSentCode = pendingCode;
  ++liveStats.serialPackets;
  printPacket("Audison TX: ", packet, sizeof(packet));
}

void readSerialInput()
{
  // Drain optional incoming data without waiting for a response.
  uint8_t received[64];
  size_t count = 0;
  while (count < sizeof(received) && AudisonSerial.available())
  {
    received[count++] = uint8_t(AudisonSerial.read());
  }
  liveStats.serialRxBytes += uint32_t(count);
  if (DEBUG_RS232_RX && count)
  {
    // Chunk diagnostic packets to keep every byte visible in bounded log lines.
    for (size_t i = 0; i < count; i += 32)
      printPacket("RS232 RX bytes: ", received + i, min(size_t(32), count - i));
  }
}

void initializeCan()
{
  // Hold transceiver TX recessive while initializing the CAN controller.
  digitalWrite(int(CAN_TX), HIGH);
  pinMode(int(CAN_TX), OUTPUT);
  AudisonSerial.begin(115200, SERIAL_8N1, UART_RX, UART_TX);

  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
      CAN_TX, CAN_RX, CAN_LISTEN_ONLY ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
  general.rx_queue_len = 64;
  general.tx_queue_len = 0;
  twai_timing_config_t timing = TWAI_TIMING_CONFIG_100KBITS();
  twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t error = twai_driver_install(&general, &timing, &filter);
  if (error != ESP_OK)
  {
    Console.printf("CAN install failed: %s\n", esp_err_to_name(error));
    return;
  }

  error = twai_start();
  if (error != ESP_OK)
  {
    Console.printf("CAN start failed: %s\n", esp_err_to_name(error));
    twai_driver_uninstall();
    return;
  }

  canReady = true;
  lastSendMs = millis();
  Console.printf("Ready: CAN 100 kbps, volume 0x183, mute 0x181, %s; Audison 115200 8N1; linear 0..44 -> -60..+6 dB\n",
                CAN_LISTEN_ONLY ? "listen-only (car)" : "normal (bench)");
}

void publishStats()
{
  liveStats.canReady = canReady;
  liveStats.listenOnly = CAN_LISTEN_ONLY;
  liveStats.volume = requestedVolume;
  liveStats.sentCode = lastSentCode;
  liveStats.muted = requestedMute;
  liveStats.muteKnown = muteStateKnown;
  twai_status_info_t status;
  if (canReady && twai_get_status_info(&status) == ESP_OK)
  {
    if (liveStats.canState != int(status.state))
      Console.printf("CAN controller state: %d\n", int(status.state));
    liveStats.canState = int(status.state);
    liveStats.rxMissed = status.rx_missed_count;
    liveStats.rxOverruns = status.rx_overrun_count;
    liveStats.busErrors = status.bus_error_count;
    liveStats.rxErrors = status.rx_error_counter;
    liveStats.txErrors = status.tx_error_counter;
    liveStats.rxQueued = status.msgs_to_rx;
  }
  else liveStats.canState = -1;
  debugPublishStats(liveStats);
}

void setup()
{
  Serial.begin(115200);
  debugInitConsole();
  delay(200);
  Console.println("Audison Bridge boot: initializing CAN and RS232 before Wi-Fi");
  initializeCan();
  publishStats();
  // Diagnostics stay available even when CAN initialization fails.
  debugBegin();
}

void loop()
{
  const uint64_t started = esp_timer_get_time();
  if (canReady)
  {
    twai_message_t message;
    // Bound work per loop so CAN traffic cannot starve serial output.
    for (int i = 0; i < 64 && twai_receive(&message, 0) == ESP_OK; ++i)
    {
      ++liveStats.canFrames;
      liveStats.lastCanMs = millis();
      processCanMessage(message);
    }
    // During OTA, keep decoding the latest CAN state but pause RS232 commands.
    sendPendingVolume();
  }
  readSerialInput();
  ++liveStats.loops;
  if (uint32_t(millis() - lastStatsMs) >= 250)
  {
    lastStatsMs = millis();
    publishStats();
  }
  liveStats.maxLoopUs = max(liveStats.maxLoopUs, uint32_t(esp_timer_get_time() - started));
  delay(1);
}
