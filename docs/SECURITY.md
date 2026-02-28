# Security Model

This document describes the security considerations for the esphome-elero component.

## Overview

The esphome-elero component controls Elero wireless blinds via a CC1101 868 MHz RF transceiver. This involves wireless communication with motor controllers, a web interface for management, and integration with Home Assistant.

**Important:** The Elero RF protocol was designed for convenience, not security. It provides no meaningful protection against determined attackers with RF equipment.

---

## RF Protocol Security

### Threat Model

| Threat | Risk Level | Mitigation |
|--------|------------|------------|
| **Eavesdropping** | High | None - RF signals are broadcast openly |
| **Replay attacks** | Medium | Protocol includes counter, but no cryptographic verification |
| **Command injection** | Medium | Requires knowledge of blind address and channel |
| **Denial of service** | Medium | RF jamming can prevent communication |

### Protocol Limitations

The Elero protocol uses:

1. **Obfuscation, not encryption**: The encoding (nibble substitution, XOR, etc.) obscures data but provides no cryptographic security. Anyone with the algorithm can decode packets.

2. **Rolling counter**: Commands include a counter value, but:
   - The counter is not cryptographically signed
   - Counter sync can be broken by physical remote use
   - No replay protection beyond "recent counter" checks

3. **No authentication**: Any device that knows the blind address and channel can send commands. There is no shared secret or certificate.

4. **Broadcast transmission**: All RF commands are broadcast on the shared 868 MHz band. Any receiver within range can capture packets.

### What This Means

- **Physical access to original remote**: Allows extraction of all necessary parameters
- **Passive sniffing**: Reveals blind addresses, channels, and command patterns
- **Neighborhood attackers**: Can potentially control blinds if they capture RF traffic

### Recommendations

1. **Don't rely on blinds for security**: Blinds should not be your only barrier against intruders
2. **Physical security**: Keep original remotes secure
3. **Network isolation**: Put ESPHome devices on a separate VLAN if possible
4. **Monitor logs**: Watch for unexpected blind movements

---

## Web Interface Security

### CORS Policy

The web UI at `/elero` allows requests from **any origin** (`Access-Control-Allow-Origin: *`).

**Implications:**
- Any website loaded in a browser on the same network can make requests to the Elero web API
- This enables easy integration but allows potential cross-site attacks

**Mitigations:**
- The web UI is only accessible on the local network
- No sensitive credentials are exposed
- Commands only affect blinds (no system access)

### Authentication

The web interface has **no authentication** by default. Anyone on the local network can:
- View discovered and configured blinds
- Send commands to blinds via WebSocket
- View RF packets and logs in real-time

**Mitigations:**
- Use the `elero_web` switch to disable the web UI when not needed
- Restrict network access to trusted devices
- Consider using Home Assistant's authentication instead of direct web UI access

### WebSocket Endpoint

The WebSocket at `/elero/ws` accepts unauthenticated connections:

| Message Type | Risk | Notes |
|--------------|------|-------|
| `cmd` (send command) | Low | Controls blinds |
| `raw` (raw TX) | Medium | Can send arbitrary RF packets |
| `rf` events (receive) | Low | Reveals blind addresses |

### Rate Limiting

There is **no rate limiting** on WebSocket messages. A malicious client could:
- Flood the device with commands
- Cause excessive RF transmissions
- Potentially interfere with normal operation

---

## Buffer Safety

### Input Validation

The component validates all inputs:

| Input Source | Validation |
|--------------|------------|
| RF packet length | Checked against `ELERO_MAX_PACKET_SIZE` (57 bytes) |
| Destination count | Limited to 20 (`ELERO_MAX_DISCOVERED`) |
| Buffer indices | Bounds-checked before access |
| JSON strings | Escaped to prevent injection |
| HTTP parameters | Parsed with error checking |

### Memory Safety

- All `snprintf()` calls use proper size limits
- Ring buffers have fixed maximum sizes
- Command queues limited to `ELERO_MAX_COMMAND_QUEUE` (10)
- Log buffer limited to `ELERO_LOG_BUFFER_SIZE` (200 entries)

### Stack Usage

The web server uses moderately large stack buffers:
- JSON response buffers: 512-640 bytes
- Not a security issue, but may cause stack overflow on very constrained devices

---

## Home Assistant Integration

### API Security

When used with Home Assistant's native API:
- Communication is encrypted (if configured)
- Authentication is required
- Commands go through Home Assistant's access control

This is **more secure** than direct web UI access.

### Recommended Configuration

```yaml
# Use Home Assistant API instead of direct web access
api:
  encryption:
    key: "your-encryption-key"

# Disable web UI when not needed for discovery
switch:
  - platform: elero_web
    name: "Elero Web UI"
    restore_mode: RESTORE_DEFAULT_OFF  # Keep disabled by default
```

---

## Reporting Security Issues

If you discover a security vulnerability:

1. **Do not** open a public GitHub issue
2. Contact the maintainer directly via GitHub private message
3. Allow time for a fix before public disclosure

---

## Summary

| Component | Security Level | Notes |
|-----------|----------------|-------|
| RF Protocol | **Low** | Obfuscation only, no real encryption |
| Web UI | **Low** | No authentication, CORS allows all |
| Home Assistant API | **Medium-High** | Encrypted, authenticated |
| Buffer handling | **Good** | Proper bounds checking |
| Input validation | **Good** | All inputs validated |

**Bottom line:** This component is suitable for convenience automation of blinds, but should not be relied upon for physical security purposes.
