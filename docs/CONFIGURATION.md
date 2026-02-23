# Konfigurationsreferenz: Elero ESPHome Component

Vollständige Referenz aller konfigurierbaren Parameter.

---

## Hub: `elero`

Der zentrale Hub steuert die SPI-Kommunikation mit dem CC1101-Funkmodul.

```yaml
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26
  freq0: 0x7a
  freq1: 0x71
  freq2: 0x21
```

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `cs_pin` | GPIO-Pin | Ja | - | SPI Chip-Select Pin für den CC1101 |
| `gdo0_pin` | GPIO-Pin (Input) | Ja | - | CC1101 GDO0 Interrupt-Pin |
| `freq0` | Hex (0x00-0xFF) | Nein | `0x7a` | CC1101 Frequenz-Register FREQ0 |
| `freq1` | Hex (0x00-0xFF) | Nein | `0x71` | CC1101 Frequenz-Register FREQ1 |
| `freq2` | Hex (0x00-0xFF) | Nein | `0x21` | CC1101 Frequenz-Register FREQ2 |

> Der Hub erweitert die ESPHome SPI-Konfiguration. `spi:` muss separat mit `clk_pin`, `mosi_pin` und `miso_pin` konfiguriert sein.

### Frequenz-Varianten

| Variante | freq0 | freq1 | freq2 | Hinweis |
|---|---|---|---|---|
| Standard 868 MHz | `0x7a` | `0x71` | `0x21` | Standard-Einstellung |
| Alternative 868 MHz | `0xc0` | `0x71` | `0x21` | Häufigste Alternative |

---

## Plattform: `cover`

Jeder Rollladen wird als eigener Cover-Eintrag konfiguriert.

```yaml
cover:
  - platform: elero
    name: "Schlafzimmer"
    blind_address: 0xa831e5
    channel: 4
    remote_address: 0xf0d008
    open_duration: 25s
    close_duration: 22s
    poll_interval: 5min
    supports_tilt: false
    payload_1: 0x00
    payload_2: 0x04
    pck_inf1: 0x6a
    pck_inf2: 0x00
    hop: 0x0a
    command_up: 0x20
    command_down: 0x40
    command_stop: 0x10
    command_check: 0x00
    command_tilt: 0x24
```

### Pflichtparameter

| Parameter | Typ | Beschreibung |
|---|---|---|
| `name` | String | Anzeigename in Home Assistant |
| `blind_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF-Adresse des Rollladens (= `dst` im Log) |
| `channel` | Integer (0-255) | Funkkanal des Rollladens (= `chl` im Log) |
| `remote_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF-Adresse der zu simulierenden Fernbedienung (= `src`/`bwd`/`fwd` im Log) |

### Optionale Parameter

| Parameter | Typ | Standard | Beschreibung |
|---|---|---|---|
| `open_duration` | Zeitdauer | `0s` | Fahrzeit zum vollständigen Öffnen. Wird für die zeitbasierte Positionssteuerung benötigt. |
| `close_duration` | Zeitdauer | `0s` | Fahrzeit zum vollständigen Schließen. Wird für die zeitbasierte Positionssteuerung benötigt. |
| `poll_interval` | Zeitdauer / `never` | `5min` | Intervall für Status-Abfragen. `never` deaktiviert das Polling. |
| `supports_tilt` | Boolean | `false` | Aktiviert Tilt/Kipp-Unterstützung (z.B. für Raffstore). |

### Protokoll-Parameter

Diese Werte werden aus dem Log der echten Fernbedienung ausgelesen. Bei Übereinstimmung mit den Standardwerten müssen sie nicht angegeben werden.

| Parameter | Typ | Standard | Log-Feld | Beschreibung |
|---|---|---|---|---|
| `payload_1` | Hex (0x00-0xFF) | `0x00` | `payload[0]` | Erstes Payload-Byte |
| `payload_2` | Hex (0x00-0xFF) | `0x04` | `payload[1]` | Zweites Payload-Byte |
| `pck_inf1` | Hex (0x00-0xFF) | `0x6a` | `typ` | Erstes Paket-Info-Byte |
| `pck_inf2` | Hex (0x00-0xFF) | `0x00` | `typ2` | Zweites Paket-Info-Byte |
| `hop` | Hex (0x00-0xFF) | `0x0a` | `hop` | Hop-Zähler |

### Befehls-Parameter

| Parameter | Typ | Standard | Beschreibung |
|---|---|---|---|
| `command_up` | Hex (0x00-0xFF) | `0x20` | Befehlscode: Rollladen hoch |
| `command_down` | Hex (0x00-0xFF) | `0x40` | Befehlscode: Rollladen runter |
| `command_stop` | Hex (0x00-0xFF) | `0x10` | Befehlscode: Rollladen stopp |
| `command_check` | Hex (0x00-0xFF) | `0x00` | Befehlscode: Status abfragen |
| `command_tilt` | Hex (0x00-0xFF) | `0x24` | Befehlscode: Tilt/Kipp |

---

## Plattform: `sensor` (RSSI)

Zeigt die Empfangsstärke (RSSI) des letzten empfangenen Pakets eines bestimmten Rollladens.

```yaml
sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer RSSI"
```

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `blind_address` | Hex (24-bit) | Ja | - | RF-Adresse des zu überwachenden Rollladens |
| `name` | String | Ja | - | Anzeigename in Home Assistant |

**Automatisch gesetzte Werte:**

| Eigenschaft | Wert |
|---|---|
| `unit_of_measurement` | dBm |
| `accuracy_decimals` | 1 |
| `device_class` | signal_strength |
| `state_class` | measurement |

### RSSI-Richtwerte

| RSSI (dBm) | Bewertung |
|---|---|
| > -50 | Ausgezeichnet |
| -50 bis -70 | Gut |
| -70 bis -85 | Akzeptabel |
| < -85 | Schwach / unzuverlässig |

---

## Plattform: `text_sensor` (Status)

Zeigt den aktuellen Blind-Status als lesbaren Text.

```yaml
text_sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer Status"
```

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `blind_address` | Hex (24-bit) | Ja | - | RF-Adresse des zu überwachenden Rollladens |
| `name` | String | Ja | - | Anzeigename in Home Assistant |

### Mögliche Status-Werte

| Wert | Beschreibung |
|---|---|
| `top` | Rollladen vollständig offen (obere Endposition) |
| `bottom` | Rollladen vollständig geschlossen (untere Endposition) |
| `intermediate` | Rollladen in Zwischenposition |
| `tilt` | Rollladen in Kipp-/Tilt-Position |
| `top_tilt` | Rollladen oben, gekippt |
| `bottom_tilt` | Rollladen unten, gekippt |
| `moving_up` | Rollladen fährt hoch |
| `moving_down` | Rollladen fährt runter |
| `start_moving_up` | Rollladen beginnt hochzufahren |
| `start_moving_down` | Rollladen beginnt runterzufahren |
| `stopped` | Rollladen gestoppt (Zwischenposition) |
| `blocking` | Rollladen blockiert (Fehler!) |
| `overheated` | Motor überhitzt (Fehler!) |
| `timeout` | Zeitüberschreitung (Fehler!) |
| `on` | Eingeschaltet |
| `unknown` | Unbekannter Zustand |

---

## Plattform: `button` (RF-Scan)

Stellt Buttons zum Starten und Stoppen eines RF-Discovery-Scans bereit.

```yaml
button:
  - platform: elero
    name: "Elero Start Scan"
    scan_start: true
  - platform: elero
    name: "Elero Stop Scan"
    scan_start: false
```

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `name` | String | Ja | - | Anzeigename in Home Assistant |
| `scan_start` | Boolean | Nein | `true` | `true` = Scan starten, `false` = Scan stoppen |

**Hinweis:** Für einen vollständigen Scan-Workflow werden zwei Buttons benötigt: einer zum Starten und einer zum Stoppen. Die Scan-Ergebnisse werden im ESPHome-Log ausgegeben.

---

## Web-UI: `elero_web`

Optionale Web-Oberfläche zur Geräteerkennung und YAML-Generierung. Erreichbar unter `http://<device-ip>/elero`.

```yaml
# web_server_base wird von elero_web automatisch geladen.
# Explizit angeben um den Port zu konfigurieren:
web_server_base:
  port: 80

elero_web:
```

**Voraussetzungen:**
- `web_server_base` wird automatisch von `elero_web` geladen. **Nicht** `web_server:` verwenden – das aktiviert die Standard-ESPHome-UI unter `/` wieder. Zugriff auf `/` leitet automatisch zu `/elero` weiter.

**Funktionen:**
- **RF-Scan steuern** – Scan starten/stoppen direkt im Browser
- **Gefundene Geräte anzeigen** – Adresse, Kanal, Remote, RSSI, Status, Hop
- **Konfigurierte Covers anzeigen** – Name, Position, Betriebszustand
- **YAML exportieren** – Generiert Copy-Paste-fertige YAML-Konfiguration für entdeckte Blinds

**REST-API Endpoints:**

| Endpoint | Methode | Beschreibung |
|---|---|---|
| `/elero` | GET | Web-UI (HTML) |
| `/elero/api/scan/start` | POST | RF-Scan starten |
| `/elero/api/scan/stop` | POST | RF-Scan stoppen |
| `/elero/api/discovered` | GET | Gefundene Geraete (JSON) |
| `/elero/api/configured` | GET | Konfigurierte Covers (JSON) |
| `/elero/api/yaml` | GET | YAML-Export fuer entdeckte Blinds |

**HTTP-Fehlercodes:**

| Code | Bedeutung | Wann |
|---|---|---|
| 200 | OK | Erfolgreiche Anfrage |
| 409 | Conflict | Scan starten wenn bereits laeuft, oder Scan stoppen wenn keiner laeuft |

Fehlerantworten werden als JSON zurueckgegeben: `{"error": "Beschreibung"}`

**CORS-Unterstuetzung:**

Alle API-Endpoints unterstuetzen Cross-Origin-Zugriff (CORS):
- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: GET, POST, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type`
- Preflight-Requests (OPTIONS) werden auf allen API-Endpoints unterstuetzt

---

## Vollständiges Beispiel

Eine vollständige Konfiguration mit allen Plattformen:

```yaml
external_components:
  - source: github://pfriedrich84/esphome-elero

spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26

cover:
  - platform: elero
    name: "Schlafzimmer"
    blind_address: 0xa831e5
    channel: 4
    remote_address: 0xf0d008
    open_duration: 25s
    close_duration: 22s

  - platform: elero
    name: "Wohnzimmer"
    blind_address: 0xb912f3
    channel: 5
    remote_address: 0xf0d008

sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer RSSI"
  - platform: elero
    blind_address: 0xb912f3
    name: "Wohnzimmer RSSI"

text_sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer Status"
  - platform: elero
    blind_address: 0xb912f3
    name: "Wohnzimmer Status"

button:
  - platform: elero
    name: "Elero Start Scan"
    scan_start: true
  - platform: elero
    name: "Elero Stop Scan"
    scan_start: false

# Web UI for discovery and YAML export
web_server:

elero_web:
```

Siehe auch: [Installationsanleitung](INSTALLATION.md) | [README](../README.md) | [Beispiel-YAML](../example.yaml)
