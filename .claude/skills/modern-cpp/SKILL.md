---
name: modern-cpp
description: C++17 best practices for ESP32/embedded, aligned with Google C++ Style Guide. Use when writing or reviewing C++ code.
---

# Modern C++ for ESP32 (C++17)

Guidelines for safe, efficient embedded C++ code based on the Google C++ Style Guide.

**Source:** [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)

---

## Naming Conventions

| Entity | Style | Example |
|--------|-------|---------|
| Types (classes, structs, enums) | PascalCase | `UrlTable`, `SensorState` |
| Variables | snake_case | `table_name`, `num_entries` |
| Class data members | trailing `_` | `table_name_`, `count_` |
| Struct data members | no trailing `_` | `x`, `y` |
| Constants | k + PascalCase | `kMaxRetries`, `kTimeoutMs` |
| Functions | PascalCase | `ReadSensor()`, `ProcessData()` |
| Accessors/mutators | snake_case | `count()`, `set_count()` |
| Namespaces | snake_case | `esphome::elero` |
| Macros | UPPER_SNAKE with prefix | `ELERO_MAX_SIZE` |
| Enumerators | k + PascalCase | `kIdle`, `kRunning` |

```cpp
class SensorReader {
 public:
  void UpdateReading();
  int reading() const { return reading_; }
 private:
  static constexpr int kMaxRetries = 3;
  int reading_{0};
};

enum class State : uint8_t { kIdle, kRunning, kError };
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

enum class Status { kOk, kInvalidArg, kTimeout };
[[nodiscard]] Status Initialize();
```

Use `[[nodiscard]]` on functions where ignoring the return value is likely a bug.

---

## Constants

```cpp
// Good: Compile-time constants
constexpr int kMaxRetries = 3;
constexpr uint32_t kTimeoutMs = 5000;
inline constexpr uint8_t kEncodeTable[] = {0x08, 0x02, 0x0d};  // Header-only

// Avoid
#define MAX_SIZE 100              // Use constexpr
const int kMax = ComputeMax();    // Runtime init - use constexpr if possible
static const uint8_t kTable[] = {...};  // Creates copy per TU - use inline
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
  case State::kIdle:
    break;
  case State::kRunning:
    [[fallthrough]];  // Explicit fallthrough
  default:
    break;
}
```

### Scoped Enums
```cpp
enum class State : uint8_t { kIdle, kRunning };  // Not: enum State { IDLE }
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
inline constexpr uint8_t kTable[] = {0x08, 0x02};

// Bad: Copy per translation unit
static const uint8_t kTable[] = {0x08, 0x02};
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

## Anti-Patterns

| Avoid | Prefer |
|-------|--------|
| `new`/`delete` | Smart pointers, containers |
| `(int)x` | `static_cast<int>(x)` |
| `NULL` | `nullptr` |
| `#define CONST 5` | `constexpr int kConst = 5` |
| `typedef` | `using Alias = Type` |
| `int arr[N]` | `std::array<int, N>` |
| Output parameters | Return values |
| `i++` (unused value) | `++i` |
| `static const` in header | `inline constexpr` |
| Implicit conversions | `explicit` constructors |
