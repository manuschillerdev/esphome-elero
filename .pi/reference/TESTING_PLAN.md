# Testing Plan for esphome-elero

Best practices for testing ESP32/C++ codebases as of 2026.

---

## Testing Strategies for ESP32/ESPHome Projects

### 1. Layered Testing Approach

The recommended practice is a hybrid strategy:

| Layer | Where it runs | Purpose |
|-------|---------------|---------|
| **Unit tests** | Host machine (native) | Fast iteration, business logic, protocol parsing |
| **Component tests** | Host or simulator | Integration between modules |
| **On-target tests** | Real ESP32 | Hardware timing, RF behavior, SPI communication |

### 2. Frameworks

**For native/host testing:**
- **GoogleTest + GMock** — most mature, great mocking support, Parasoft-certified version coming Jan 2026 for safety-critical use
- **Catch2** — header-only, simpler setup, modern syntax

**For on-target testing:**
- **Unity** — ESP-IDF's built-in framework
- **pytest-embedded** — wraps Unity output for CI integration

### 3. Hardware Abstraction Layer (HAL)

The key to testability is abstracting hardware dependencies:

```cpp
// Instead of direct register access (untestable):
REG_WRITE(GPIO_OUT_REG, 0x01);

// Use a mockable interface:
class ISpiDevice {
public:
    virtual void write_byte(uint8_t data) = 0;
    virtual uint8_t read_byte() = 0;
};
```

For this codebase, abstract the CC1101 SPI communication so the protocol logic (encryption, packet encoding) can be tested without hardware.

### 4. ESPHome-Specific Requirements

ESPHome requires compile-time tests for all components:

```
components/elero/
├── test.esp32-ard.yaml
├── test.esp32-idf.yaml
├── test.esp32-c3-ard.yaml
├── test.esp32-c3-idf.yaml
├── test.esp8266-ard.yaml
└── test.rp2040-ard.yaml
```

These are **compile tests only** — they verify the code builds, not that it works correctly.

### 5. What to Test Natively for This Project

Good candidates for host-based unit tests:

| Module | What to test |
|--------|--------------|
| **Encryption/decryption** | The Elero protocol crypto can be tested with known test vectors |
| **Packet encoding/decoding** | Verify packet structure without RF hardware |
| **Position calculation** | `EleroCover` dead-reckoning logic |
| **State machine** | Blind state transitions |

### 6. Practical Setup with GoogleTest

```
tests/
├── CMakeLists.txt
├── test_encryption.cpp      # Test crypto with known vectors
├── test_packet_codec.cpp    # Test packet encode/decode
├── test_position_calc.cpp   # Test position dead-reckoning
└── mocks/
    └── mock_spi.h           # Mock SPI for protocol tests
```

Run with:
```bash
cmake -B build tests && cmake --build build && ./build/tests
```

### 7. Simulation Options

- **Wokwi** — browser-based ESP32 simulator, integrates with VS Code
- **ESPHome Host Platform** — build ESPHome as a native executable for non-hardware components
- **ESP-IDF Linux host tests** — mock hardware via CMock

---

## Implementation Roadmap

### Phase 1: ESPHome Compile Tests (Minimum Requirement)
- Add compile test YAML files for each architecture/framework combination
- Catches syntax/API errors in CI

### Phase 2: Extract Protocol Logic
- Move encryption and packet handling to pure functions
- Separate hardware-dependent code from business logic

### Phase 3: Add GoogleTest for Protocol Layer
- Test encryption/decryption with known RF captures
- Test packet encoding/decoding
- Test position calculation logic

### Phase 4: On-Target Validation
- Keep manual testing for RF timing and SPI behavior
- Consider pytest-embedded for automated on-device tests

---

## References

- [ESP-IDF Unit Testing](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html)
- [ESPHome Component Tests](https://developers.esphome.io/architecture/ci/component_tests/)
- [Embedded C/C++ Unit Testing Fundamentals](https://www.parasoft.com/blog/embedded-unit-testing/)
- [GoogleTest Framework](https://embeddedprep.com/unit-testing-frameworks/)
- [pytest-embedded with Unity](https://blog.haysc.tech/unity-pytest-embedded-button-component-example/)
