#pragma once

namespace DebugConfig {
constexpr char FIRMWARE_VERSION[] = "1.0.0-wifi";
constexpr char HOSTNAME[] = "audison-bridge";
constexpr char AP_PREFIX[] = "Audison-Bridge-";
constexpr char USERNAME[] = "admin";
// Empty = generate a random 16-character password once and keep it in NVS.
// Set a personal 8..63 character password here to override the generated one.
// The Wi-Fi and dashboard use this password. It is printed over USB at startup.
constexpr char PASSWORD[] = "";
constexpr unsigned AP_CHANNEL = 6;
constexpr unsigned MAX_CLIENTS = 2;
constexpr unsigned LOG_CAPACITY = 256;
constexpr unsigned LOG_TEXT_SIZE = 192;
constexpr unsigned LOG_BATCH_SIZE = 32;
} // namespace DebugConfig
