---
name: esp32-development
description: ESP32 memory management, ISRs, FreeRTOS tasks, SPI, power management, ESPHome components. Use when writing ESP32 code, handling interrupts, managing memory, configuring SPI, or implementing ESPHome components.
---

# ESP32 Development Best Practices

Use this skill when writing code for ESP32 microcontrollers, whether using ESP-IDF, Arduino framework, or ESPHome components.

**Sources:**
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/)
- [Arduino-ESP32 Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [ESPHome Developer Guide](https://esphome.io/guides/)

---

## Memory Architecture

### Memory Types

| Type | Size | Use For | Access |
|------|------|---------|--------|
| **DRAM** | 320 KB | Variables, heap, stack | Read/write, byte-aligned |
| **IRAM** | 128 KB | ISRs, timing-critical code | Execute, 4-byte aligned |
| **IROM** | Flash | Application code (via cache) | Execute only |
| **DROM** | Flash | Constants, strings (via cache) | Read only |
| **RTC** | 8 KB | Deep-sleep persistent data | Read/write |

### Memory Placement Attributes

```cpp
// Place function in IRAM (fast, ISR-safe)
void IRAM_ATTR my_isr_handler() {
  // Critical timing code
}

// Place data in RTC memory (survives deep sleep)
RTC_DATA_ATTR int boot_count = 0;

// Place data in DRAM for DMA access
DMA_ATTR uint8_t dma_buffer[256];

// Ensure word alignment for DMA
WORD_ALIGNED_ATTR uint8_t aligned_buf[64];
```

### Stack Considerations

```cpp
// BAD: Large stack allocation
void process() {
  uint8_t buffer[8192];  // Risk of stack overflow!
}

// GOOD: Heap allocation for large buffers
void process() {
  auto buffer = std::make_unique<std::array<uint8_t, 8192>>();
}

// GOOD: Static allocation for fixed buffers
void process() {
  static uint8_t buffer[8192];  // One instance, not on stack
}
```

---

## Interrupt Handling (ISR)

### ISR Rules

1. **Keep ISRs short** — Set flags, defer work to tasks
2. **Use IRAM_ATTR** — ISR code must be in IRAM
3. **No blocking calls** — No `delay()`, mutexes, or I/O
4. **Use volatile for shared variables**
5. **Use atomic operations** for multi-byte values

```cpp
// ISR-safe flag pattern
volatile bool data_ready = false;

void IRAM_ATTR gpio_isr_handler(void* arg) {
  data_ready = true;  // Just set flag
}

void loop() {
  if (data_ready) {
    data_ready = false;
    process_data();  // Do actual work in main context
  }
}
```

### Atomic Operations for ISR Communication

```cpp
#include <atomic>

std::atomic<bool> flag{false};
std::atomic<uint32_t> counter{0};

void IRAM_ATTR isr() {
  flag.store(true, std::memory_order_release);
  counter.fetch_add(1, std::memory_order_relaxed);
}

void loop() {
  if (flag.exchange(false, std::memory_order_acquire)) {
    // Process
  }
}
```

---

## FreeRTOS Task Management

### Task Creation

```cpp
// Create task with adequate stack
TaskHandle_t task_handle;
xTaskCreate(
    task_function,      // Function
    "TaskName",         // Name (for debugging)
    4096,               // Stack size in bytes
    nullptr,            // Parameters
    5,                  // Priority (higher = more important)
    &task_handle        // Handle
);

// Pin to specific core (ESP32 is dual-core)
xTaskCreatePinnedToCore(
    task_function, "TaskName", 4096, nullptr, 5, &task_handle,
    1  // Core ID: 0 or 1
);
```

### Task Communication

```cpp
// Queue for ISR to task communication
QueueHandle_t event_queue;
event_queue = xQueueCreate(10, sizeof(Event));

// From ISR
void IRAM_ATTR isr() {
  Event e = {.type = EVENT_DATA};
  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(event_queue, &e, &woken);
  if (woken) portYIELD_FROM_ISR();
}

// In task
void task(void* param) {
  Event e;
  while (true) {
    if (xQueueReceive(event_queue, &e, portMAX_DELAY)) {
      handle_event(e);
    }
  }
}
```

### Synchronization

```cpp
// Mutex for shared resource protection
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

void access_shared() {
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) {
    // Access shared resource
    xSemaphoreGive(mutex);
  }
}

// Binary semaphore for signaling
SemaphoreHandle_t signal = xSemaphoreCreateBinary();

void IRAM_ATTR isr() {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(signal, &woken);
  if (woken) portYIELD_FROM_ISR();
}
```

---

## GPIO Best Practices

### Configuration

```cpp
// ESP-IDF style
gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << GPIO_NUM_4),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};
gpio_config(&io_conf);

// Arduino style
pinMode(4, OUTPUT);
pinMode(5, INPUT_PULLUP);
```

### Strapping Pins (Avoid or Use Carefully)

| GPIO | Function | Safe to Use? |
|------|----------|--------------|
| GPIO0 | Boot mode | Avoid (needs HIGH at boot) |
| GPIO2 | Boot mode | Use with care |
| GPIO5 | SDIO timing | Use with care |
| GPIO12 | Flash voltage | Avoid |
| GPIO15 | SDIO timing | Use with care |

---

## SPI Communication

### Best Practices

```cpp
// Use hardware CS when possible
spi_device_interface_config_t devcfg = {
    .mode = 0,
    .clock_speed_hz = 1000000,
    .spics_io_num = CS_PIN,  // Hardware CS
    .queue_size = 7,
};

// For software CS, use RAII pattern
class SpiTransaction {
 public:
  explicit SpiTransaction(int cs_pin) : cs_(cs_pin) {
    gpio_set_level((gpio_num_t)cs_, 0);
  }
  ~SpiTransaction() {
    gpio_set_level((gpio_num_t)cs_, 1);
  }
 private:
  int cs_;
};

// DMA for large transfers
spi_bus_config_t buscfg = {
    .mosi_io_num = MOSI_PIN,
    .miso_io_num = MISO_PIN,
    .sclk_io_num = SCLK_PIN,
    .max_transfer_sz = 4096,  // DMA buffer size
};
```

### Timing

```cpp
// Add delays after SPI operations if needed
void write_register(uint8_t addr, uint8_t data) {
  {
    SpiTransaction txn(cs_pin_);
    spi_->transfer(addr);
    spi_->transfer(data);
  }
  delayMicroseconds(15);  // Device settle time
}
```

---

## Power Management

### Sleep Modes

| Mode | Wake Sources | Current | Use Case |
|------|-------------|---------|----------|
| Modem sleep | WiFi beacon | ~20 mA | Connected idle |
| Light sleep | Timer, GPIO, UART | ~0.8 mA | Short idle periods |
| Deep sleep | Timer, GPIO, ULP | ~10 µA | Long idle periods |

### Deep Sleep Pattern

```cpp
#include "esp_sleep.h"

RTC_DATA_ATTR int boot_count = 0;

void setup() {
  boot_count++;

  // Configure wake sources
  esp_sleep_enable_timer_wakeup(60 * 1000000);  // 60 seconds
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0); // GPIO wake

  // Do work...

  // Enter deep sleep
  esp_deep_sleep_start();
}
```

---

## WiFi Best Practices

### Connection Management

```cpp
// Use event-driven connection handling
WiFi.onEvent([](WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STA_CONNECTED:
      ESP_LOGI(TAG, "Connected to AP");
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      ESP_LOGW(TAG, "Disconnected, reconnecting...");
      WiFi.reconnect();
      break;
    case IP_EVENT_STA_GOT_IP:
      ESP_LOGI(TAG, "Got IP: %s", WiFi.localIP().toString().c_str());
      break;
  }
});

// Non-blocking connection
WiFi.begin(ssid, password);
// Don't block with while(!WiFi.isConnected())
```

### Memory with WiFi

```cpp
// WiFi uses significant heap (~50KB)
// Check free heap before large allocations
size_t free_heap = esp_get_free_heap_size();
if (free_heap < 20000) {
  ESP_LOGW(TAG, "Low memory: %d bytes", free_heap);
}
```

---

## Logging Best Practices

### ESP-IDF Logging

```cpp
#include "esp_log.h"

static const char* TAG = "my_component";

ESP_LOGE(TAG, "Error: %s", error_msg);     // Error
ESP_LOGW(TAG, "Warning: value=%d", val);   // Warning
ESP_LOGI(TAG, "Info: started");            // Info
ESP_LOGD(TAG, "Debug: state=%d", state);   // Debug
ESP_LOGV(TAG, "Verbose: raw=%02x", byte);  // Verbose

// Conditional compilation based on log level
#if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
  dump_buffer(data, len);
#endif
```

### ESPHome Logging

```cpp
ESP_LOGE("tag", "Error message");
ESP_LOGW("tag", "Warning message");
ESP_LOGI("tag", "Info message");
ESP_LOGD("tag", "Debug message");
ESP_LOGV("tag", "Verbose message");
ESP_LOGVV("tag", "Very verbose message");
```

---

## Timing and Delays

### Non-Blocking Patterns

```cpp
// BAD: Blocking delay
void loop() {
  do_work();
  delay(1000);  // Blocks everything!
}

// GOOD: Non-blocking with millis()
uint32_t last_run = 0;
const uint32_t INTERVAL = 1000;

void loop() {
  uint32_t now = millis();
  if (now - last_run >= INTERVAL) {
    last_run = now;
    do_work();
  }
  // Other tasks can run
}
```

### Microsecond Timing

```cpp
// Use esp_timer for accurate timing
#include "esp_timer.h"

int64_t start = esp_timer_get_time();  // Microseconds
// ... operation ...
int64_t elapsed_us = esp_timer_get_time() - start;
```

### Safe Delays

```cpp
// Short delays (doesn't yield to RTOS)
delayMicroseconds(100);
ets_delay_us(100);

// Longer delays (yields to RTOS)
delay(10);
vTaskDelay(pdMS_TO_TICKS(10));
```

---

## Flash/NVS Storage

### NVS for Configuration

```cpp
#include "nvs_flash.h"
#include "nvs.h"

// Initialize once
nvs_flash_init();

// Write
nvs_handle_t handle;
nvs_open("storage", NVS_READWRITE, &handle);
nvs_set_i32(handle, "counter", 42);
nvs_commit(handle);
nvs_close(handle);

// Read
nvs_open("storage", NVS_READONLY, &handle);
int32_t value;
nvs_get_i32(handle, "counter", &value);
nvs_close(handle);
```

### Wear Leveling

```cpp
// Avoid frequent writes to same key
// BAD: Write every loop
nvs_set_i32(handle, "counter", counter++);

// GOOD: Write periodically or on change
if (counter - last_saved > 100) {
  nvs_set_i32(handle, "counter", counter);
  last_saved = counter;
}
```

---

## ESPHome Component Guidelines

### Component Lifecycle

```cpp
class MyComponent : public Component {
 public:
  // Called once at startup
  void setup() override {
    // Initialize hardware
  }

  // Called every loop iteration
  void loop() override {
    // Non-blocking operations only!
  }

  // Called during config dump
  void dump_config() override {
    ESP_LOGCONFIG(TAG, "MyComponent:");
    ESP_LOGCONFIG(TAG, "  Pin: %d", pin_);
  }

  // Priority (higher = earlier setup)
  float get_setup_priority() const override {
    return setup_priority::DATA;  // After hardware, before network
  }
};
```

### Setup Priorities

| Priority | Value | Use For |
|----------|-------|---------|
| `BUS` | 1000 | SPI, I2C buses |
| `IO` | 900 | GPIO expanders |
| `HARDWARE` | 800 | Sensors, displays |
| `DATA` | 600 | Data processing |
| `PROCESSOR` | 400 | After all hardware |
| `WIFI` | 250 | Network-dependent |
| `AFTER_WIFI` | 200 | Services requiring network |

---

## Common Pitfalls

### 1. Watchdog Timeout

```cpp
// BAD: Long blocking operation
while (waiting) {
  // Watchdog will reset!
}

// GOOD: Yield periodically
while (waiting) {
  yield();  // Or vTaskDelay(1)
  if (timeout_expired()) break;
}
```

### 2. Stack Overflow

```cpp
// BAD: Recursive with deep stack
void parse(Node* n) {
  if (n->child) parse(n->child);  // Can overflow
}

// GOOD: Iterative or tail-recursive
void parse(Node* n) {
  while (n) {
    process(n);
    n = n->child;
  }
}
```

### 3. Heap Fragmentation

```cpp
// BAD: Frequent small allocations
for (int i = 0; i < 1000; i++) {
  char* buf = (char*)malloc(10);
  // ...
  free(buf);
}

// GOOD: Reuse buffers
char buf[10];
for (int i = 0; i < 1000; i++) {
  // Use buf
}
```

### 4. Flash Cache Miss in ISR

```cpp
// BAD: Calling flash-resident code from ISR
void IRAM_ATTR isr() {
  Serial.println("ISR");  // May crash!
}

// GOOD: Only IRAM code in ISR
volatile bool flag = false;
void IRAM_ATTR isr() {
  flag = true;  // Just set flag
}
```

---

## Debugging Tips

1. **Monitor heap**: `ESP.getFreeHeap()`, `heap_caps_get_free_size()`
2. **Monitor stack**: `uxTaskGetStackHighWaterMark()`
3. **Use assertions**: `configASSERT()`, `ESP_ERROR_CHECK()`
4. **Core dumps**: Enable in menuconfig for crash analysis
5. **JTAG debugging**: For step-through debugging
