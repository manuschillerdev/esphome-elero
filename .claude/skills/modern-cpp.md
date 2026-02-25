# Modern C++ Best Practices (C++17/C++20)

Use this skill when writing or reviewing C++ code. Apply these guidelines to produce safe, efficient, and maintainable code.

**Sources:**
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Modern C++ Features Cheatsheet](https://github.com/AnthonyCalandra/modern-cpp-features)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)

---

## Core Principles

1. **Express intent clearly** — Code should be readable and self-documenting
2. **Prefer compile-time over run-time** — Use `constexpr`, `static_assert`, concepts
3. **Don't leak resources** — Use RAII, smart pointers, containers
4. **Zero-overhead abstractions** — Abstractions shouldn't cost more than hand-written code

---

## C++17 Features to Use

### Language Features

| Feature | Example | When to Use |
|---------|---------|-------------|
| `constexpr` | `constexpr int MAX = 100;` | All compile-time constants |
| `[[nodiscard]]` | `[[nodiscard]] bool save();` | Functions where ignoring return is likely a bug |
| `[[maybe_unused]]` | `[[maybe_unused]] int debug;` | Variables used only in some configs |
| `[[fallthrough]]` | `case 1: x++; [[fallthrough]];` | Intentional switch fallthrough |
| Structured bindings | `auto [key, value] = pair;` | Decomposing pairs, tuples, structs |
| `if` with initializer | `if (auto it = m.find(k); it != m.end())` | Limiting scope of variables |
| `constexpr if` | `if constexpr (sizeof(T) > 8)` | Compile-time branching in templates |
| Nested namespaces | `namespace A::B::C { }` | Deep namespace hierarchies |
| Inline variables | `inline constexpr int X = 5;` | Header-only constants |

### Library Features

| Feature | Example | When to Use |
|---------|---------|-------------|
| `std::optional<T>` | `std::optional<int> find(x)` | Functions that may not return a value |
| `std::string_view` | `void log(std::string_view msg)` | Read-only string parameters |
| `std::variant<Ts...>` | `std::variant<int, string> v;` | Type-safe unions |
| `std::byte` | `std::byte buf[64];` | Raw memory/byte manipulation |
| `std::clamp` | `std::clamp(x, 0, 100)` | Constraining values to range |
| `std::size` | `std::size(array)` | Getting size of arrays/containers |

---

## C++20 Features to Use (When Available)

### Language Features

| Feature | Example | When to Use |
|---------|---------|-------------|
| Concepts | `template<std::integral T>` | Constraining template parameters |
| `<=>` (spaceship) | `auto operator<=>(const X&) = default;` | Generating comparison operators |
| Designated initializers | `Point{.x = 1, .y = 2}` | Struct initialization with named fields |
| `[[likely]]`/`[[unlikely]]` | `if (error) [[unlikely]]` | Branch prediction hints |
| `consteval` | `consteval int square(int n)` | Must-be-compile-time functions |
| `constinit` | `constinit int x = compute();` | Guaranteed static initialization |
| Range-based for init | `for (int i=0; auto& e : vec)` | Loop with counter |

### Library Features

| Feature | Example | When to Use |
|---------|---------|-------------|
| `std::span<T>` | `void process(std::span<int> data)` | Non-owning view of contiguous data |
| `std::format` | `std::format("{} = {}", name, val)` | Type-safe string formatting |
| `starts_with`/`ends_with` | `str.starts_with("prefix")` | String prefix/suffix checks |
| `.contains()` | `if (map.contains(key))` | Checking container membership |
| `std::bit_cast` | `std::bit_cast<uint32_t>(float_val)` | Safe type punning |
| `std::popcount` | `std::popcount(mask)` | Counting set bits |
| `std::jthread` | `std::jthread worker(fn);` | Auto-joining threads |

---

## Resource Management (RAII)

### Smart Pointers

```cpp
// Prefer
auto ptr = std::make_unique<Widget>();      // Unique ownership
auto shared = std::make_shared<Widget>();   // Shared ownership

// Avoid
Widget* raw = new Widget();  // Manual memory management
delete raw;
```

### RAII Guards

```cpp
// Pattern: Resource guard class
class SpiTransaction {
 public:
  explicit SpiTransaction(SpiDevice* dev) : dev_(dev) { dev_->enable(); }
  ~SpiTransaction() { dev_->disable(); }
  SpiTransaction(const SpiTransaction&) = delete;
  SpiTransaction& operator=(const SpiTransaction&) = delete;
 private:
  SpiDevice* dev_;
};

// Usage
void transfer() {
  SpiTransaction txn(&device);  // enable() called
  // ... do work ...
}  // disable() called automatically, even on exception
```

### Container Preference

```cpp
// Prefer standard containers
std::vector<int> data;           // Dynamic array
std::array<int, 10> fixed;       // Fixed-size array
std::string text;                // String management

// Avoid C-style arrays and manual allocation
int* arr = new int[n];  // Bad
int carr[100];          // Risky (stack overflow, no bounds checking)
```

---

## Error Handling

### Use Exceptions for Errors

```cpp
// Good: Exception for failure
void save_file(const std::string& path) {
  std::ofstream f(path);
  if (!f) throw std::runtime_error("Cannot open " + path);
  // ...
}

// Good: Optional for "not found"
std::optional<User> find_user(int id) {
  if (auto it = users_.find(id); it != users_.end())
    return it->second;
  return std::nullopt;
}
```

### Use `[[nodiscard]]` for Functions That Return Important Values

```cpp
[[nodiscard]] bool write_reg(uint8_t addr, uint8_t val);
[[nodiscard]] std::optional<Data> parse(std::string_view input);
```

---

## Constants and Constexpr

```cpp
// Good: Compile-time constants
constexpr int MAX_RETRIES = 3;
constexpr uint32_t TIMEOUT_MS = 5000;

// Good: Compile-time computation
constexpr uint16_t crc16(std::span<const uint8_t> data) {
  // ... compute at compile time if possible
}

// Avoid: Runtime initialization of constants
const int MAX = compute_max();  // Runs at startup
static const int X = 5;         // Prefer constexpr
#define MAX_SIZE 100            // Prefer constexpr
```

---

## Type Safety

### Use Strongly-Typed Enums

```cpp
// Good: Scoped enum with underlying type
enum class State : uint8_t {
  IDLE,
  RUNNING,
  ERROR
};

// Avoid: Unscoped enums pollute namespace
enum State { IDLE, RUNNING, ERROR };  // IDLE is global
```

### Use `std::byte` for Raw Data

```cpp
// Good: Explicit byte semantics
std::array<std::byte, 64> buffer;

// Avoid: char/uint8_t for non-character data
char buf[64];      // Ambiguous: text or bytes?
uint8_t data[64];  // Better, but std::byte is clearer
```

### Avoid Implicit Conversions

```cpp
// Good: Explicit constructors
class Distance {
 public:
  explicit Distance(double meters) : m_(meters) {}
 private:
  double m_;
};

// Avoid: Implicit conversion surprises
class Distance {
 public:
  Distance(double meters) : m_(meters) {}  // Allows Distance d = 5.0;
};
```

---

## Functions

### Parameter Passing

| Parameter Type | When to Use |
|----------------|-------------|
| `T` (by value) | Small types (≤16 bytes), types you'll copy anyway |
| `const T&` | Large types you only read |
| `T&` | Output/in-out parameters |
| `T&&` | Sink parameters (you'll move from) |
| `std::string_view` | Read-only string access |
| `std::span<T>` | Read-only contiguous range |

```cpp
void process(std::string_view name);              // Read-only string
void fill(std::span<std::byte> buffer);           // Read-only span
void compute(const Matrix& m);                     // Large read-only
void swap(int& a, int& b);                         // In-out
void take_ownership(std::unique_ptr<Widget> w);   // Sink
```

### Prefer Return Values Over Output Parameters

```cpp
// Good: Return value
[[nodiscard]] std::vector<int> compute();
[[nodiscard]] std::optional<Result> parse(Input in);
auto [success, data] = fetch();  // Structured binding

// Avoid: Output parameters
void compute(std::vector<int>& out);  // Harder to use
bool parse(Input in, Result* out);    // Error-prone
```

---

## Classes

### Rule of Zero/Five

```cpp
// Rule of Zero: Let compiler generate everything
class Widget {
  std::string name_;
  std::vector<int> data_;
  // No destructor, copy/move operations needed
};

// Rule of Five: If you define one, define all
class RawResource {
 public:
  RawResource();
  ~RawResource();
  RawResource(const RawResource&);
  RawResource& operator=(const RawResource&);
  RawResource(RawResource&&) noexcept;
  RawResource& operator=(RawResource&&) noexcept;
};
```

### Use `= default` and `= delete`

```cpp
class NonCopyable {
 public:
  NonCopyable() = default;
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;
  NonCopyable(NonCopyable&&) = default;
  NonCopyable& operator=(NonCopyable&&) = default;
};
```

### Member Initialization

```cpp
class Sensor {
 public:
  explicit Sensor(uint8_t addr) : address_(addr) {}

 private:
  uint8_t address_;
  uint32_t timeout_ms_{1000};     // Default member initializer
  bool enabled_{false};
  std::vector<Reading> history_;  // Default-constructed
};
```

---

## Performance Tips

1. **Prefer `emplace_back` over `push_back`** for in-place construction
2. **Reserve vector capacity** when size is known: `vec.reserve(n)`
3. **Use `std::move`** when transferring ownership
4. **Avoid unnecessary copies** — use references, `string_view`, `span`
5. **Mark move operations `noexcept`** — enables optimizations
6. **Use `constexpr` functions** — enables compile-time evaluation
7. **Prefer algorithms over loops** — `std::find`, `std::transform`, etc.

---

## Anti-Patterns to Avoid

| Avoid | Prefer |
|-------|--------|
| `new`/`delete` | Smart pointers, containers |
| C-style casts `(int)x` | `static_cast<int>(x)` |
| `NULL` | `nullptr` |
| `#define CONST 5` | `constexpr int CONST = 5;` |
| `typedef` | `using Alias = Type;` |
| Raw arrays `int arr[N]` | `std::array<int, N>` |
| Output parameters | Return values |
| `void*` | Templates, `std::variant`, `std::any` |
| Macros for code | Templates, `constexpr`, inline functions |
