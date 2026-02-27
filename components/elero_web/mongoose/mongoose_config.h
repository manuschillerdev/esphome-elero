// Mongoose configuration for ESP32 with ESPHome
#pragma once

// Use ESP32 architecture (works for both Arduino and ESP-IDF frameworks)
#define MG_ARCH MG_ARCH_ESP32

// Enable HTTP server
#define MG_ENABLE_HTTP 1

// Disable features we don't need to save flash
#define MG_ENABLE_MQTT 0
#define MG_ENABLE_DNS 0
#define MG_ENABLE_IPV6 0
#define MG_ENABLE_WEBSOCKET 0  // We use SSE, not WebSocket
#define MG_ENABLE_FILE 0
#define MG_ENABLE_DIRECTORY_LISTING 0
#define MG_ENABLE_SSI 0
#define MG_ENABLE_CUSTOM_RANDOM 0
#define MG_ENABLE_PACKED_FS 0
#define MG_ENABLE_TLS 0

// Memory settings
#define MG_IO_SIZE 512
