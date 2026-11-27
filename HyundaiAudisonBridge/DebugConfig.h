#pragma once

#if __has_include("LocalSecrets.h")
#include "LocalSecrets.h"
#endif

#ifndef AUDISON_BRIDGE_PASSWORD
#define AUDISON_BRIDGE_PASSWORD ""
#endif

namespace DebugConfig {
constexpr char FIRMWARE_VERSION[] = "1.0.1-wifi";
constexpr char HOSTNAME[] = "audison-bridge";
constexpr char AP_PREFIX[] = "Audison-Bridge-";
constexpr char USERNAME[] = "admin";
// Empty = generate a random 16-character password once and keep it in NVS.
// Set a personal password in ignored LocalSecrets.h; keep this file public.
// The Wi-Fi and dashboard use this password. It is printed over USB at startup.
constexpr char PASSWORD[] = AUDISON_BRIDGE_PASSWORD;
constexpr unsigned AP_CHANNEL = 6;
constexpr unsigned MAX_CLIENTS = 2;
constexpr unsigned LOG_CAPACITY = 256;
constexpr unsigned LOG_TEXT_SIZE = 192;
constexpr unsigned LOG_BATCH_SIZE = 32;
} // namespace DebugConfig
