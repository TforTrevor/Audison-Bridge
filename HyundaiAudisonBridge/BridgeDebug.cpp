#include "BridgeDebug.h"
#include "DebugConfig.h"
#include "Dashboard.h"
#include "LogRing.h"
#include "FirmwareUpload.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <atomic>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
using History = LogRing<DebugConfig::LOG_CAPACITY, DebugConfig::LOG_TEXT_SIZE>;
History history;
History::Entry logBatch[DebugConfig::LOG_BATCH_SIZE];
SemaphoreHandle_t logMutex = nullptr;
char partialLine[DebugConfig::LOG_TEXT_SIZE] = {};
size_t partialLength = 0;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
BridgeStats bridgeStats;
std::atomic<bool> maintenance{false};
WebServer server(80);
String password, ssid, bootId, csrfToken;
uint32_t rebootAt = 0;
bool rebootPending = false;
bool uploadSeen = false;
FirmwareUpload<UpdateClass> firmwareUpload(Update);
uint32_t lastUploadMs = 0;
String uploadError;

uint64_t uptimeMs() { return uint64_t(esp_timer_get_time()) / 1000; }

String number(uint64_t value) {
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
  return String(buffer);
}

String jsonString(const char *text) {
  String result = "\"";
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
    switch (*p) {
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (*p < 0x20 || *p >= 0x7f) {
        char escaped[7];
        snprintf(escaped, sizeof(escaped), "\\u%04x", unsigned(*p));
        result += escaped;
      } else result += char(*p);
    }
  }
  result += '"';
  return result;
}

void headers() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("Connection", "close");
}

void errorResponse(int code, const String &message) {
  headers();
  server.send(code, "application/json", "{\"error\":" + jsonString(message.c_str()) + "}");
}

bool allowedHost() {
  String host = server.header("Host");
  String ip = WiFi.softAPIP().toString();
  String name = String(DebugConfig::HOSTNAME) + ".local";
  return host == ip || host == ip + ":80" || host == name || host == name + ":80";
}

bool authenticated() {
  if (!allowedHost()) { errorResponse(403, "Unrecognized host"); return false; }
  if (server.authenticate(DebugConfig::USERNAME, password.c_str())) return true;
  headers();
  server.requestAuthentication(BASIC_AUTH, "Audison Bridge");
  return false;
}

bool validAction() {
  String origin = server.header("Origin");
  return allowedHost() && server.header("X-Bridge-Token") == csrfToken &&
         (origin.isEmpty() || origin == "http://" + server.header("Host"));
}

bool authorizeAction() {
  if (!authenticated()) return false;
  if (!validAction()) { errorResponse(403, "Refresh the dashboard and try again"); return false; }
  return true;
}

const char *resetReason() {
  switch (esp_reset_reason()) {
  case ESP_RST_POWERON: return "Power on";
  case ESP_RST_EXT: return "External reset";
  case ESP_RST_SW: return "Software restart";
  case ESP_RST_PANIC: return "Panic / exception";
  case ESP_RST_INT_WDT: return "Interrupt watchdog";
  case ESP_RST_TASK_WDT: return "Task watchdog";
  case ESP_RST_WDT: return "Watchdog";
  case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
  case ESP_RST_BROWNOUT: return "Brownout";
  default: return "Other / unknown";
  }
}

void sendSession() {
  if (!authenticated()) return;
  headers();
  server.send(200, "application/json", "{\"bootId\":" + jsonString(bootId.c_str()) +
              ",\"token\":" + jsonString(csrfToken.c_str()) + "}");
}

void sendStats() {
  if (!authenticated()) return;
  BridgeStats s;
  portENTER_CRITICAL(&statsMux);
  s = bridgeStats;
  portEXIT_CRITICAL(&statsMux);
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t *running = esp_ota_get_running_partition();
  uint64_t oldest, latest;
  size_t retained;
  xSemaphoreTake(logMutex, portMAX_DELAY);
  oldest = history.oldest(); latest = history.latest(); retained = history.count();
  xSemaphoreGive(logMutex);
  String out;
  out.reserve(2000);
  out = "{\"bootId\":" + jsonString(bootId.c_str());
  out += ",\"firmware\":" + jsonString(DebugConfig::FIRMWARE_VERSION);
  out += ",\"build\":" + jsonString(__DATE__ " " __TIME__);
  out += ",\"uptimeMs\":" + number(uptimeMs());
  out += ",\"resetReason\":" + jsonString(resetReason());
  out += ",\"chip\":" + jsonString(ESP.getChipModel());
  out += ",\"revision\":" + String(ESP.getChipRevision());
  out += ",\"cores\":" + String(ESP.getChipCores());
  out += ",\"cpuMHz\":" + String(ESP.getCpuFreqMHz());
  out += ",\"heapFree\":" + String(ESP.getFreeHeap());
  out += ",\"heapMin\":" + String(ESP.getMinFreeHeap());
  out += ",\"heapLargest\":" + String(ESP.getMaxAllocHeap());
  out += ",\"flashBytes\":" + String(ESP.getFlashChipSize());
  out += ",\"sketchBytes\":" + String(ESP.getSketchSize());
  out += ",\"otaBytes\":" + String(slot ? slot->size : 0);
  out += ",\"partition\":" + jsonString(running ? running->label : "unknown");
  out += ",\"ssid\":" + jsonString(ssid.c_str());
  out += ",\"ip\":" + jsonString(WiFi.softAPIP().toString().c_str());
  out += ",\"clients\":" + String(WiFi.softAPgetStationNum());
  out += ",\"maintenance\":" + String(maintenance.load() ? "true" : "false");
  out += ",\"rebootPending\":" + String(rebootPending ? "true" : "false");
  out += ",\"canReady\":" + String(s.canReady ? "true" : "false");
  out += ",\"listenOnly\":" + String(s.listenOnly ? "true" : "false");
  out += ",\"canState\":" + String(s.canState);
  out += ",\"canFrames\":" + String(s.canFrames);
  out += ",\"canAgeMs\":" + (s.canFrames ? String(uint32_t(millis() - s.lastCanMs)) : "null");
  out += ",\"rxMissed\":" + String(s.rxMissed);
  out += ",\"rxOverruns\":" + String(s.rxOverruns);
  out += ",\"busErrors\":" + String(s.busErrors);
  out += ",\"rxErrors\":" + String(s.rxErrors);
  out += ",\"txErrors\":" + String(s.txErrors);
  out += ",\"rxQueued\":" + String(s.rxQueued);
  out += ",\"serialPackets\":" + String(s.serialPackets);
  out += ",\"serialRxBytes\":" + String(s.serialRxBytes);
  out += ",\"loops\":" + String(s.loops);
  out += ",\"maxLoopUs\":" + String(s.maxLoopUs);
  out += ",\"volume\":" + (s.volume < 0 ? String("null") : String(s.volume));
  out += ",\"muted\":" + (s.muteKnown ? String(s.muted ? "true" : "false") : "null");
  out += ",\"audisonDb\":" + (s.sentCode < 0 ? String("null") : String((0x30 - s.sentCode) * 0.5, 1));
  out += ",\"logOldest\":" + number(oldest);
  out += ",\"logLatest\":" + number(latest);
  out += ",\"logRetained\":" + String(retained);
  out += ",\"logCapacity\":" + String(DebugConfig::LOG_CAPACITY) + "}";
  headers();
  server.send(200, "application/json", out);
}

void sendLogs() {
  if (!authenticated()) return;
  uint64_t after = strtoull(server.arg("after").c_str(), nullptr, 10);
  uint64_t missed, oldest, latest;
  xSemaphoreTake(logMutex, portMAX_DELAY);
  size_t count = history.copyAfter(after, logBatch, DebugConfig::LOG_BATCH_SIZE, missed);
  oldest = history.oldest(); latest = history.latest();
  xSemaphoreGive(logMutex);
  String out;
  out.reserve(count * (DebugConfig::LOG_TEXT_SIZE + 64) + 200);
  out = "{\"bootId\":" + jsonString(bootId.c_str()) + ",\"oldest\":" + number(oldest);
  out += ",\"latest\":" + number(latest) + ",\"missed\":" + number(missed);
  out += ",\"next\":" + number(count ? logBatch[count - 1].id : latest) + ",\"lines\":[";
  for (size_t i = 0; i < count; ++i) {
    if (i) out += ',';
    out += "{\"id\":" + number(logBatch[i].id) + ",\"ms\":" + number(logBatch[i].ms);
    out += ",\"text\":" + jsonString(logBatch[i].text) + "}";
  }
  out += "]}";
  headers();
  server.send(200, "application/json", out);
}

void downloadLogs() {
  if (!authenticated()) return;
  uint64_t end, cursor = 0;
  xSemaphoreTake(logMutex, portMAX_DELAY);
  end = history.latest();
  xSemaphoreGive(logMutex);
  headers();
  server.sendHeader("Content-Disposition", "attachment; filename=audison-" + bootId + ".log");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain; charset=utf-8", "");
  server.sendContent("Audison Bridge | boot " + bootId + " | " + DebugConfig::FIRMWARE_VERSION +
                     "\nCurrent-boot RAM history; oldest entries are overwritten when full.\n");
  while (cursor < end && server.client().connected()) {
    uint64_t missed;
    xSemaphoreTake(logMutex, portMAX_DELAY);
    size_t count = history.copyAfter(cursor, logBatch, DebugConfig::LOG_BATCH_SIZE, missed);
    xSemaphoreGive(logMutex);
    if (!count) break;
    String chunk;
    if (missed) chunk += "[history overwritten during download]\n";
    for (size_t i = 0; i < count && logBatch[i].id <= end; ++i) {
      chunk += "[" + number(logBatch[i].ms) + " ms] " + logBatch[i].text + "\n";
      cursor = logBatch[i].id;
    }
    if (chunk.isEmpty() || logBatch[0].id > end) break;
    server.sendContent(chunk);
  }
  server.sendContent("");
}

void scheduleReboot() { rebootAt = millis() + 2000; rebootPending = true; }

void failUpload(const String &reason) {
  // Copy before abort, because Update's current error may be reset by abort().
  String description = reason;
  firmwareUpload.reset();
  maintenance.store(false);
  if (uploadError.isEmpty()) {
    uploadError = description;
    Console.printf("OTA failed: %s\n", description.c_str());
  }
}

void receiveUpload() {
  HTTPUpload &file = server.upload();
  if (file.status == UPLOAD_FILE_START) {
    if (uploadSeen) { failUpload("Send exactly one firmware file"); return; }
    uploadSeen = true;
    uploadError = "";
    firmwareUpload.reset();
    if (!server.authenticate(DebugConfig::USERNAME, password.c_str()) || !validAction()) {
      uploadError = "Unauthorized upload"; return;
    }
    if (rebootPending) { uploadError = "A restart is already pending"; return; }
    String size = server.header("X-Firmware-Size");
    bool digits = !size.isEmpty();
    for (size_t i = 0; i < size.length(); ++i) digits = digits && isDigit(size[i]);
    uint64_t declared = strtoull(size.c_str(), nullptr, 10);
    const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
    String filename = file.filename;
    filename.toLowerCase();
    if (!filename.endsWith(".bin") || !digits || declared < 24 || !slot || declared > slot->size) {
      uploadError = "Choose an application .bin that fits the OTA partition"; return;
    }
    maintenance.store(true);
    if (!firmwareUpload.begin(size_t(declared), slot->size)) {
      failUpload(firmwareUpload.error()); return;
    }
    lastUploadMs = millis();
    Console.printf("OTA started: %u bytes; bridge output paused\n", unsigned(declared));
  } else if (file.status == UPLOAD_FILE_WRITE && firmwareUpload.active()) {
    lastUploadMs = millis();
    if (!firmwareUpload.write(file.buf, file.currentSize)) {
      failUpload(firmwareUpload.error()); return;
    }
  } else if (file.status == UPLOAD_FILE_END && firmwareUpload.active()) {
    if (!firmwareUpload.finishFile(file.totalSize)) {
      failUpload(firmwareUpload.error()); return;
    }
    // Activate only after the entire multipart request completes. A second
    // file, aborted body, or failed final authorization must not select it.
  } else if (file.status == UPLOAD_FILE_ABORTED) {
    failUpload("Upload disconnected or aborted");
    uploadSeen = false;
  }
}

void finishUpload() {
  if (!authorizeAction()) {
    if (firmwareUpload.active()) failUpload("Unauthorized upload");
    uploadSeen = false; return;
  }
  if (firmwareUpload.ready() && uploadError.isEmpty()) {
    if (!firmwareUpload.activate()) {
      failUpload(firmwareUpload.error());
      errorResponse(400, uploadError);
    } else {
      Console.println("OTA verified; restarting in 2 seconds");
      // Restart even if the browser loses the success response.
      scheduleReboot();
      headers();
      server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    }
  } else {
    if (firmwareUpload.active()) failUpload("Incomplete firmware upload");
    errorResponse(400, uploadError.isEmpty() ? String("No firmware file received") : uploadError);
  }
  uploadSeen = false;
  firmwareUpload.reset();
  uploadError = "";
}

String randomSecret(size_t length) {
  static const char alphabet[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  String value;
  value.reserve(length);
  for (size_t i = 0; i < length; ++i) value += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  return value;
}

void networkTask(void *) {
  // This task owns WebServer, Wi-Fi, Update, and all HTTP response allocations.
  // Arduino's loop continues processing CAN on its usual core.
  Preferences settings;
  if (!settings.begin("bridge-debug", false)) {
    Console.println("Debug disabled: cannot open NVS settings");
    vTaskDelete(nullptr); return;
  }
  password = DebugConfig::PASSWORD;
  if (password.isEmpty()) {
    password = settings.getString("password", "");
    if (password.isEmpty()) {
      password = randomSecret(16);
      if (!settings.putString("password", password)) {
        settings.end();
        Console.println("Debug disabled: cannot save Wi-Fi password");
        vTaskDelete(nullptr); return;
      }
    }
  }
  settings.end();
  if (password.length() < 8 || password.length() > 63) {
    Console.println("Debug disabled: password must have 8..63 characters");
    vTaskDelete(nullptr); return;
  }
  bootId = randomSecret(12);
  csrfToken = randomSecret(32);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  ssid = String(DebugConfig::AP_PREFIX) + mac.substring(mac.length() - 4);
  IPAddress ip(192, 168, 4, 1);
  if (!WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(ssid.c_str(), password.c_str(), DebugConfig::AP_CHANNEL, false, DebugConfig::MAX_CLIENTS)) {
    Console.println("Debug disabled: Wi-Fi access point failed");
    vTaskDelete(nullptr); return;
  }
  const char *collected[] = {"Host", "Origin", "X-Bridge-Token", "X-Firmware-Size"};
  server.collectHeaders(collected, 4);
  server.on("/", HTTP_GET, []() {
    if (!authenticated()) return;
    headers();
    server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
  });
  server.on("/api/session", HTTP_GET, sendSession);
  server.on("/api/stats", HTTP_GET, sendStats);
  server.on("/api/logs", HTTP_GET, sendLogs);
  server.on("/api/logs.txt", HTTP_GET, downloadLogs);
  server.on("/api/reboot", HTTP_POST, []() {
    if (!authorizeAction()) return;
    if (maintenance.load() || rebootPending) { errorResponse(409, "Maintenance already in progress"); return; }
    Console.println("Dashboard requested a restart; current-boot history will be cleared");
    scheduleReboot();
    headers();
    server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
  });
  server.on("/api/firmware", HTTP_POST, finishUpload, receiveUpload);
  server.onNotFound([]() { if (authenticated()) errorResponse(404, "Not found"); });
  server.begin();
  if (MDNS.begin(DebugConfig::HOSTNAME)) MDNS.addService("http", "tcp", 80);
  // Keep credentials out of remotely downloadable logs.
  Serial.printf("\nWi-Fi: %s\nWi-Fi / dashboard password: %s\nDashboard username: %s\n",
                ssid.c_str(), password.c_str(), DebugConfig::USERNAME);
  Console.printf("Dashboard ready: http://192.168.4.1 (or http://%s.local)\n", DebugConfig::HOSTNAME);
  for (;;) {
    server.handleClient();
    if (firmwareUpload.active() && uint32_t(millis() - lastUploadMs) > 15000) {
      failUpload("Upload timed out"); uploadSeen = false;
    }
    if (rebootPending && int32_t(millis() - rebootAt) >= 0) ESP.restart();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
} // namespace

BridgeConsole Console;

void debugInitConsole() { logMutex = xSemaphoreCreateMutex(); }

size_t BridgeConsole::write(uint8_t value) { return write(&value, 1); }

size_t BridgeConsole::write(const uint8_t *data, size_t length) {
  if (!logMutex) return Serial.write(data, length);
  xSemaphoreTake(logMutex, portMAX_DELAY);
  for (size_t i = 0; i < length; ++i) {
    char c = char(data[i]);
    if (c == '\r') continue;
    if (c == '\n') {
      partialLine[partialLength] = '\0';
      history.push(uptimeMs(), partialLine);
      partialLength = 0;
    } else {
      // Preserve long application lines as consecutive bounded entries.
      if (partialLength == sizeof(partialLine) - 1) {
        partialLine[partialLength] = '\0';
        history.push(uptimeMs(), partialLine);
        partialLength = 0;
      }
      partialLine[partialLength++] = c ? c : '?';
    }
  }
  // Serial and RAM observe the same write order. HTTP holds this mutex only
  // while copying a bounded batch, never while transmitting to a browser.
  size_t written = Serial.write(data, length);
  xSemaphoreGive(logMutex);
  return written;
}

void debugBegin() {
  if (!logMutex) { Serial.println("Debug disabled: log mutex allocation failed"); return; }
  if (xTaskCreatePinnedToCore(networkTask, "bridge-debug", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
    Console.println("Debug disabled: network task allocation failed");
  }
}

void debugPublishStats(const BridgeStats &stats) {
  portENTER_CRITICAL(&statsMux);
  bridgeStats = stats;
  portEXIT_CRITICAL(&statsMux);
}

bool debugMaintenanceActive() { return maintenance.load(); }
