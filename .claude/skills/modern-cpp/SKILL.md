---
name: modern-cpp
description: C++17 best practices for ESP32/embedded, aligned with Google C++ Style Guide. Use when writing or reviewing C++ code.
---

# Modern C++ for ESP32 (C++17)

Guidelines for safe, efficient embedded C++ code based on the Google C++ Style Guide, adapted for ESP32/embedded conventions.

**Source:** [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)

**C++ Version:** C++17 is fully supported on ESP-IDF 4.x+ and Arduino-ESP32 2.x+. See [Tooling](#tooling) section for build configuration.

---

## Naming Conventions

| Entity | Style | Example |
|--------|-------|---------|
| Types (classes, structs, enums) | PascalCase | `UrlTable`, `SensorState` |
| Variables | snake_case | `table_name`, `num_entries` |
| Class data members | trailing `_` | `table_name_`, `count_` |
| Struct data members | no trailing `_` | `x`, `y` |
| Constants | UPPER_SNAKE with prefix | `ELERO_MAX_RETRIES` |
| Functions | PascalCase | `ReadSensor()`, `ProcessData()` |
| Accessors/mutators | snake_case | `count()`, `set_count()` |
| Namespaces | snake_case | `esphome::elero` |
| Macros | UPPER_SNAKE with prefix | `ELERO_MAX_SIZE` |
| Enumerators | PascalCase or UPPER_SNAKE | `kIdle`, `STATE_IDLE` |

**Note on constants**: Google style uses `kPascalCase`, but ESP-IDF, ESPHome, and Arduino all use `UPPER_SNAKE_CASE` for constants. Follow ecosystem conventions for embedded code.

```cpp
class SensorReader {
 public:
  void UpdateReading();
  int reading() const { return reading_; }
 private:
  static constexpr int MAX_RETRIES = 3;
  int reading_{0};
};

enum class State : uint8_t { IDLE, RUNNING, ERROR };
```

---

## Integer Types

Use fixed-width types from `<cstdint>` when size matters. Use `int` for general purposes.

```cpp
uint8_t register_value;    // Hardware register
int32_t packet_count;      // Known size needed
for (int i = 0; i < n; ++i) // General loop counter
```

**Avoid:** `unsigned` just for non-negativity, `short`/`long`/`long long`.

---

## Error Handling (No Exceptions)

ESP32/embedded code doesn't use exceptions. Use return values instead.

```cpp
[[nodiscard]] bool WriteRegister(uint8_t addr, uint8_t val);
[[nodiscard]] std::optional<Data> Parse(std::string_view input);

enum class Status { OK, INVALID_ARG, TIMEOUT };
[[nodiscard]] Status Initialize();
```

Use `[[nodiscard]]` on functions where ignoring the return value is likely a bug.

---

## Constants

```cpp
// Good: Compile-time constants
constexpr int MAX_RETRIES = 3;
constexpr uint32_t TIMEOUT_MS = 5000;
inline constexpr uint8_t ENCODE_TABLE[] = {0x08, 0x02, 0x0d};  // Header-only

// Avoid
#define MAX_SIZE 100                // Use constexpr
const int MAX = ComputeMax();       // Runtime init - use constexpr if possible
static const uint8_t TABLE[] = {};  // Creates copy per TU - use inline
```

---

## RAII and Smart Pointers

```cpp
// Unique ownership
auto widget = std::make_unique<Widget>();

// RAII guard pattern
class SpiTransaction {
 public:
  explicit SpiTransaction(SpiDevice* dev) : dev_(dev) { dev_->Enable(); }
  ~SpiTransaction() { dev_->Disable(); }
  SpiTransaction(const SpiTransaction&) = delete;
  SpiTransaction& operator=(const SpiTransaction&) = delete;
 private:
  SpiDevice* dev_;
};
```

---

## Classes

### Structs vs Classes

```cpp
// Struct: passive data, no invariants
struct Point { float x; float y; };

// Class: has invariants, methods, or non-public members
class Circle {
 public:
  explicit Circle(float r) : radius_(r) {}
  float Area() const;
 private:
  float radius_;
};
```

### Declaration Order

```cpp
class MyClass {
 public:
  // Types, constants, constructors, methods, accessors
 protected:
  // Protected members
 private:
  // Private methods, then data members last
};
```

### Rule of Zero/Five

```cpp
// Rule of Zero: Let compiler generate special members
class Widget {
  std::string name_;
  std::vector<int> data_;
};

// Rule of Five: If you define one, define all
class RawResource {
 public:
  ~RawResource();
  RawResource(const RawResource&);
  RawResource& operator=(const RawResource&);
  RawResource(RawResource&&) noexcept;
  RawResource& operator=(RawResource&&) noexcept;
};
```

### Move-only and Non-copyable

```cpp
// Move-only
class UniqueHandle {
 public:
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  UniqueHandle(UniqueHandle&&) noexcept = default;
  UniqueHandle& operator=(UniqueHandle&&) noexcept = default;
};
```

---

## Inheritance

- Prefer composition over inheritance
- Use `override` and `final` explicitly
- Avoid multiple implementation inheritance (interface inheritance OK)

```cpp
class Base {
 public:
  virtual void Process() = 0;
  virtual ~Base() = default;
};

class Derived : public Base {
 public:
  void Process() override;
};
```

---

## Functions

### Parameter Passing

| Type | When |
|------|------|
| `T` (value) | Small types (≤16 bytes), will copy anyway |
| `const T&` | Large types, read-only |
| `T*` | Nullable output parameter |
| `std::string_view` | Read-only strings |
| `std::span<T>` | Read-only contiguous data |

### Guidelines

- Prefer return values over output parameters
- Write short, focused functions (~40 lines max)
- Use `explicit` on single-argument constructors
- Don't use default arguments on virtual functions

---

## Type Deduction

Use `auto` only when it improves clarity.

```cpp
// Good: Type obvious
auto widget = std::make_unique<Widget>();
auto it = map.find(key);
auto [key, value] = *it;

// Bad: Type unclear
auto result = Compute();  // What type?
```

---

## Other Guidelines

### Preincrement
```cpp
++i;  // Prefer over i++ when value not used
```

### sizeof
```cpp
memset(&data, 0, sizeof(data));  // Prefer sizeof(var) over sizeof(Type)
```

### const
```cpp
void Process(const std::string& input);  // const parameters
int value() const { return value_; }      // const methods
```

### Switch
```cpp
switch (state) {
  case State::IDLE:
    break;
  case State::RUNNING:
    [[fallthrough]];  // Explicit fallthrough
  default:
    break;
}
```

### Scoped Enums
```cpp
enum class State : uint8_t { IDLE, RUNNING };  // Not: enum State { ... }
```

### Casting
```cpp
static_cast<int>(x);  // Not: (int)x
```

---

## Headers

### Include Guards
```cpp
#pragma once  // Or traditional #ifndef guards
```

### Inline Variables (C++17)
```cpp
// Good: Single copy in flash
inline constexpr uint8_t ENCODE_TABLE[] = {0x08, 0x02};

// Bad: Copy per translation unit
static const uint8_t ENCODE_TABLE[] = {0x08, 0x02};
```

### Forward Declarations
Use when only pointers/references needed to reduce compile dependencies.

---

## Formatting

- 2-space indentation, no tabs
- 80-character line limit
- Opening brace on same line
- Space after `if`, `for`, `while`, `switch`

---

## Tooling

### Compiler Warnings

The compiler is your primary type checker. Use strict flags:

```bash
# Baseline (always use)
-Wall -Wextra -Wpedantic

# Recommended additions
-Wshadow                 # Variable shadowing
-Wconversion             # Implicit type conversions
-Wsign-conversion        # Signed/unsigned mismatches
-Wnull-dereference       # Null pointer dereference
-Wold-style-cast         # C-style casts
-Woverloaded-virtual     # Hiding virtual functions
-Wformat=2               # Format string issues
-Wdouble-promotion       # Float promoted to double

# For CI/strict builds
-Werror                  # Treat warnings as errors
```

### ESP32/ESPHome Configuration

Arduino framework defaults to C++11. Override for C++17:

```yaml
# ESPHome YAML
esphome:
  platformio_options:
    build_unflags: -std=gnu++11
    build_flags:
      - -std=gnu++17
      - -Wall
      - -Wextra
```

```ini
# platformio.ini
build_unflags = -std=gnu++11
build_flags =
  -std=gnu++17
  -Wall -Wextra -Wshadow -Wconversion
```

### Static Analysis

| Tool | Usage |
|------|-------|
| **clang-tidy** | Gold standard linter. Use `.clang-tidy` config file |
| **cppcheck** | Fast, good for CI: `cppcheck --enable=all src/` |

Example `.clang-tidy`:
```yaml
Checks: >
  -*,
  bugprone-*,
  cppcoreguidelines-*,
  modernize-*,
  performance-*,
  readability-*,
  -modernize-use-trailing-return-type,
  -readability-magic-numbers
WarningsAsErrors: ''
HeaderFilterRegex: '.*'
```

Run: `clang-tidy src/*.cpp -- -std=c++17 -I include/`

### Formatting

Use **clang-format** with a `.clang-format` file:

```yaml
# .clang-format (Google-based for ESP32)
BasedOnStyle: Google
IndentWidth: 2
ColumnLimit: 100
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: false
```

Run: `clang-format -i src/*.cpp include/*.h`

### IDE Integration

- **VS Code**: clangd extension (provides clang-tidy + format)
- **CLion**: Built-in clang-tidy and clang-format support
- **PlatformIO IDE**: Basic integration available

---

## ESP Platform Constraints

### Atomics: Platform Support

| Platform | `std::atomic` | Notes |
|----------|---------------|-------|
| **ESP32** | ✅ Supported | Lock-free for 8/16/32-bit types, use freely |
| **ESP8266** | ❌ Linker errors | `undefined reference to '__atomic_exchange_1'` |
| **RP2040** | ❌ Linker errors | No hardware atomic support |

**For ESP32-only projects:** Use `std::atomic` — it's the correct solution for ISR-to-loop communication and avoids race conditions in read-modify-write patterns.

```cpp
// ESP32-only: Preferred — no race condition
std::atomic<bool> received_{false};

void IRAM_ATTR isr() {
  received_.store(true, std::memory_order_release);
}

void loop() {
  if (received_.exchange(false)) {  // Atomic read-and-clear
    process();
  }
}
```

**For portable code (ESP8266/RP2040 support):** Use `volatile` instead. Single bool read/write is inherently atomic on ARM and Xtensa, but read-modify-write patterns have race conditions.

```cpp
// Portable but has race condition in read-clear pattern
volatile bool received_{false};

void IRAM_ATTR isr() {
  received_ = true;
}

void loop() {
  // Race: ISR can fire between read and clear
  if (received_) {
    received_ = false;
    process();
  }
}
```

**When you need atomics (ESP32):**
- ISR flag read-and-clear (avoids lost updates)
- Multi-core synchronization (ESP32 is dual-core)
- Any read-modify-write operation shared with ISR

### Memory Constraints

| Platform | RAM | Flash | Notes |
|----------|-----|-------|-------|
| ESP8266 | 80KB | 4MB | Very limited, avoid large buffers |
| ESP32 | 520KB | 4-16MB | Comfortable for most uses |
| RP2040 | 264KB | 2MB | Moderate constraints |

Prefer stack allocation for small objects, static allocation for fixed buffers.

---

## Anti-Patterns

| Avoid | Prefer |
|-------|--------|
| `new`/`delete` | Smart pointers, containers |
| `(int)x` | `static_cast<int>(x)` |
| `NULL` | `nullptr` |
| `#define CONST 5` | `constexpr int CONST = 5` |
| `typedef` | `using Alias = Type` |
| `int arr[N]` | `std::array<int, N>` |
| Output parameters | Return values |
| `i++` (unused value) | `++i` |
| `static const` in header | `inline constexpr` |
| Implicit conversions | `explicit` constructors |
| `volatile` for ISR read-clear (ESP32) | `std::atomic<bool>` with `.exchange()` |
