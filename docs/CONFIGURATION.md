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
    dst_address: 0xa831e5
    channel: 4
    src_address: 0xf0d008
    open_duration: 25s
    close_duration: 22s
    poll_interval: 5min
    supports_tilt: false
    payload_1: 0x00
    payload_2: 0x04
    type: 0x6a
    type2: 0x00
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
| `dst_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF-Adresse des Rollladens (Zieladresse) |
| `channel` | Integer (0-255) | Funkkanal des Rollladens |
| `src_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF-Adresse der zu simulierenden Fernbedienung (Quelladresse) |

### Optionale Parameter

| Parameter | Typ | Standard | Beschreibung |
|---|---|---|---|
| `open_duration` | Zeitdauer | `0s` | Fahrzeit zum vollständigen Öffnen. Wird für die zeitbasierte Positionssteuerung benötigt. Wenn gesetzt, muss auch `close_duration` gesetzt werden. |
| `close_duration` | Zeitdauer | `0s` | Fahrzeit zum vollständigen Schließen. Wird für die zeitbasierte Positionssteuerung benötigt. Wenn gesetzt, muss auch `open_duration` gesetzt werden. |
| `poll_interval` | Zeitdauer / `never` | `5min` | Intervall für Status-Abfragen. `never` deaktiviert das Polling. |
| `supports_tilt` | Boolean | `false` | Aktiviert Tilt/Kipp-Unterstützung (z.B. für Raffstore). |
| `auto_sensors` | Boolean | `true` | Erstellt automatisch RSSI- und Status-Sensoren für diesen Rollladen. Setzen Sie auf `false`, um diese manuell zu konfigurieren. |

### Protokoll-Parameter

Diese Werte werden aus dem Log der echten Fernbedienung ausgelesen. Bei Übereinstimmung mit den Standardwerten müssen sie nicht angegeben werden.

| Parameter | Typ | Standard | Beschreibung |
|---|---|---|---|
| `payload_1` | Hex (0x00-0xFF) | `0x00` | Erstes Payload-Byte |
| `payload_2` | Hex (0x00-0xFF) | `0x04` | Zweites Payload-Byte |
| `type` | Hex (0x00-0xFF) | `0x6a` | Nachrichtentyp (0x6a=Befehl, 0xca=Status) |
| `type2` | Hex (0x00-0xFF) | `0x00` | Sekundärer Typ-Byte |
| `hop` | Hex (0x00-0xFF) | `0x0a` | Hop-Zähler |

### Befehls-Parameter

| Parameter | Typ | Standard | Beschreibung |
|---|---|---|---|
| `command_up` | Hex (0x00-0xFF) | `0x20` | Befehlscode: Rollladen hoch |
| `command_down` | Hex (0x00-0xFF) | `0x40` | Befehlscode: Rollladen runter |
| `command_stop` | Hex (0x00-0xFF) | `0x10` | Befehlscode: Rollladen stopp |
| `command_check` | Hex (0x00-0xFF) | `0x00` | Befehlscode: Status abfragen |
| `command_tilt` | Hex (0x00-0xFF) | `0x24` | Befehlscode: Tilt/Kipp |

---

## Plattform: `light`

Jedes Elero-Licht (z.B. Hauslicht mit Elero-Empfänger) wird als eigener Light-Eintrag konfiguriert. Das Licht erscheint in Home Assistant als vollständige Licht-Entität — mit Ein/Aus und optionaler Helligkeitssteuerung.

```yaml
light:
  - platform: elero
    name: "Wohnzimmerlicht"
    dst_address: 0xc41a2b
    channel: 6
    src_address: 0xf0d008
    dim_duration: 5s        # Optional: 0s = nur Ein/Aus, >0 = Helligkeit steuerbar
    payload_1: 0x00
    payload_2: 0x04
    type: 0x6a
    type2: 0x00
    hop: 0x0a
    command_on: 0x20
    command_off: 0x40
    command_dim_up: 0x20
    command_dim_down: 0x40
    command_stop: 0x10
    command_check: 0x00
```

### Pflichtparameter

| Parameter | Typ | Beschreibung |
|---|---|---|
| `name` | String | Anzeigename in Home Assistant |
| `dst_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF-Adresse des Lichts (Zieladresse) |
| `channel` | Integer (0-255) | Funkkanal des Lichts |
| `src_address` | Hex (24-bit, 0x0-0xFFFFFF) | RF-Adresse der zu simulierenden Fernbedienung (Quelladresse) |

### Optionale Parameter

| Parameter | Typ | Standard | Beschreibung |
|---|---|---|---|
| `dim_duration` | Zeitdauer | `0s` | Dimm-Fahrzeit von 0 % auf 100 %. `0s` = nur Ein/Aus (`ColorMode::ON_OFF`); Wert > 0 = Helligkeitssteuerung aktiv (`ColorMode::BRIGHTNESS`). |

### Protokoll-Parameter

Diese Werte werden aus dem Log der echten Fernbedienung ausgelesen (gleiche Bedeutung wie bei `cover`).

| Parameter | Typ | Standard | Beschreibung |
|---|---|---|---|
| `payload_1` | Hex (0x00-0xFF) | `0x00` | Erstes Payload-Byte |
| `payload_2` | Hex (0x00-0xFF) | `0x04` | Zweites Payload-Byte |
| `type` | Hex (0x00-0xFF) | `0x6a` | Nachrichtentyp (0x6a=Befehl, 0xca=Status) |
| `type2` | Hex (0x00-0xFF) | `0x00` | Sekundärer Typ-Byte |
| `hop` | Hex (0x00-0xFF) | `0x0a` | Hop-Zähler |

### Befehls-Parameter

| Parameter | Typ | Standard | Beschreibung |
|---|---|---|---|
| `command_on` | Hex (0x00-0xFF) | `0x20` | Befehlscode: Licht einschalten |
| `command_off` | Hex (0x00-0xFF) | `0x40` | Befehlscode: Licht ausschalten |
| `command_dim_up` | Hex (0x00-0xFF) | `0x20` | Befehlscode: Heller dimmen (nur wenn `dim_duration > 0`) |
| `command_dim_down` | Hex (0x00-0xFF) | `0x40` | Befehlscode: Dunkler dimmen (nur wenn `dim_duration > 0`) |
| `command_stop` | Hex (0x00-0xFF) | `0x10` | Befehlscode: Dimmen stoppen |
| `command_check` | Hex (0x00-0xFF) | `0x00` | Befehlscode: Status abfragen |

---

## Automatische Sensoren (`auto_sensors`)

Diagnose-Sensoren werden automatisch für jeden Cover- und Light-Block erstellt, wenn `auto_sensors: true` gesetzt ist (Standard). Separate `sensor:` / `text_sensor:` / `binary_sensor:` Plattform-Blöcke sind nicht mehr nötig.

Automatisch erstellte Sensoren pro Gerät:

| Sensor | Typ | Beschreibung |
|---|---|---|
| RSSI | `sensor` (dBm, device_class: signal_strength) | Empfangsstärke des letzten Pakets |
| Status | `text_sensor` | Letzter Blind-Status als Text |
| Problem | `binary_sensor` | `true` bei Blocking/Overheated/Timeout |
| Befehlsquelle | `text_sensor` | Letzte Befehlsquelle |
| Problemtyp | `text_sensor` | Art des Problems |

Um die automatische Sensor-Erstellung zu deaktivieren, setze `auto_sensors: false` im Cover-/Light-Block.

> **Migration:** Eigenständige Sensor-Plattformen (`sensor: platform: elero`, `text_sensor: platform: elero`) wurden entfernt. Sensoren werden jetzt automatisch von Cover-/Light-Entities über `auto_sensors: true` erstellt.

### RSSI-Richtwerte

| RSSI (dBm) | Bewertung |
|---|---|
| > -50 | Ausgezeichnet |
| -50 bis -70 | Gut |
| -70 bis -85 | Akzeptabel |
| < -85 | Schwach / unzuverlässig |

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
- **Geräteerkennung** – RF-Pakete von Elero-Geräten in Echtzeit anzeigen
- **Konfigurierte Geräte** – Status von Rollläden und Lichtern
- **Raw TX** – Test-Befehle senden für Debugging
- **Logs** – ESPHome-Logs in Echtzeit

**WebSocket-Kommunikation:**

Die Web-UI kommuniziert über WebSocket (`/elero/ws`) für Echtzeit-Updates. Siehe `docs/ARCHITECTURE.md` für das vollständige Protokoll.

| Endpoint | Beschreibung |
|---|---|
| `/` | Weiterleitung zu `/elero` |
| `/elero` | Web-UI (HTML) |
| `/elero/ws` | WebSocket für Echtzeit-Kommunikation |

**Server → Client Events:**

| Event | Beschreibung |
|---|---|
| `config` | Geräte-Konfiguration beim Verbindungsaufbau |
| `rf` | Dekodierte RF-Pakete in Echtzeit |
| `log` | ESPHome Log-Einträge mit `elero.*` Tags |
| `device_upserted` | NVS-Modi: Gerät wurde erstellt oder aktualisiert (Adresse, Typ) |
| `device_removed` | NVS-Modi: Gerät wurde entfernt (Adresse) |

**Client → Server Messages:**

| Typ | Beschreibung |
|---|---|
| `cmd` | Befehl an Rollladen/Licht: `{"type":"cmd", "address":"0xADDRESS", "action":"up"}` |
| `raw` | Raw RF-Paket für Tests: `{"type":"raw", "dst_address":"0x...", "src_address":"0x...", "channel":5, ...}` |
| `upsert_device` | NVS-Modi: Gerät erstellen oder aktualisieren (NvsDeviceConfig-Felder) |
| `remove_device` | NVS-Modi: Gerät entfernen nach `dst_address` + `device_type` |

**Warum Mongoose?**

Die Web-UI verwendet die Mongoose HTTP/WebSocket-Bibliothek statt ESPHome's `web_server_base`. Grund: ESPHome nutzt verschiedene Implementierungen je nach Framework (Arduino vs. ESP-IDF). Mongoose bietet eine einheitliche API für beide Frameworks

---

## Plattform: `switch` (Web-UI-Steuerung)

Optionale Runtime-Steuerung zum Aktivieren/Deaktivieren der Web-UI. Wenn deaktiviert, antworten alle `/elero`-Endpoints mit HTTP 503.

```yaml
switch:
  - platform: elero_web
    name: "Elero Web UI"
    id: elero_web_switch
    restore_mode: RESTORE_DEFAULT_ON
```

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `name` | String | Ja | - | Anzeigename in Home Assistant |
| `id` | String | Nein | `elero_web_switch` | Eindeutige Komponenten-ID |
| `restore_mode` | Enum | Nein | `RESTORE_DEFAULT_ON` | `RESTORE_DEFAULT_ON` oder `RESTORE_DEFAULT_OFF` |

**Voraussetzungen:**
- `elero_web` muss in der Konfiguration vorhanden sein
- Dieser Switch ist optional; wenn nicht vorhanden, ist die Web-UI immer aktiv

---

## MQTT-Modus: `elero_mqtt`

Die `elero_mqtt`-Komponente ermöglicht die Geräteverwaltung zur Laufzeit über NVS-Persistierung und MQTT Home Assistant Discovery. Sie erfordert die ESPHome-Komponente `mqtt:`.

```yaml
mqtt:
  broker: 192.168.1.100

elero_mqtt:
  topic_prefix: elero
  discovery_prefix: homeassistant
  device_name: "Elero Gateway"
```

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `topic_prefix` | String | Nein | `elero` | MQTT-Topic-Präfix für alle Geräte-Topics |
| `discovery_prefix` | String | Nein | `homeassistant` | Home Assistant MQTT-Discovery-Präfix |
| `device_name` | String | Nein | `Elero Gateway` | Gerätename in Home Assistant |

**Wichtige Hinweise:**
- Wenn `elero_mqtt` vorhanden ist, sollten **keine** Cover oder Lights in der YAML definiert werden — Geräte werden zur Laufzeit über die Web-UI oder MQTT-API hinzugefügt.
- Geräte werden im NVS gespeichert (einheitlicher 48-Slot-Pool).
- Die Komponente `mqtt:` muss in der ESPHome-Konfiguration vorhanden sein.
- Fernbedienungen werden automatisch aus beobachteten RF-Befehlspaketen erkannt.

---

## Native+NVS-Modus: `elero_nvs`

Die `elero_nvs`-Komponente ermöglicht die Geräteverwaltung zur Laufzeit über NVS-Persistierung mit der nativen ESPHome-API (kein MQTT-Broker erforderlich).

```yaml
elero_nvs:
```

**Keine Konfigurationsparameter** — das bloße Einbinden der Komponente aktiviert die NVS-Persistierung.

**Wichtige Hinweise:**
- Geräte werden über die Web-UI-CRUD-API hinzugefügt und im NVS gespeichert.
- Beim Start werden aktive Geräte aus dem NVS wiederhergestellt und über die native ESPHome-API registriert.
- CRUD-Operationen nach dem Start schreiben in den NVS, neue Entitäten erscheinen jedoch erst nach einem Neustart (ESPHome-Einschränkung: Entitäten können nach der initialen Verbindung nicht nachträglich registriert werden).
- Kein MQTT-Broker erforderlich — verwendet die eingebaute native ESPHome-API.

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
    dst_address: 0xa831e5
    channel: 4
    src_address: 0xf0d008
    open_duration: 25s
    close_duration: 22s

  - platform: elero
    name: "Wohnzimmer"
    dst_address: 0xb912f3
    channel: 5
    src_address: 0xf0d008

light:
  - platform: elero
    name: "Wohnzimmerlicht"
    dst_address: 0xc41a2b
    channel: 6
    src_address: 0xf0d008
    # dim_duration: 5s  # Aktivieren für Helligkeitssteuerung

# Sensoren (RSSI, Status, Problem etc.) werden automatisch von den Cover-/Light-Blöcken
# erstellt, wenn auto_sensors: true (Standard). Keine separaten sensor:/text_sensor: Blöcke nötig.

# Web UI for discovery (do NOT use web_server: — use web_server_base: instead)
web_server_base:
  port: 80

elero_web:

# Optional: Runtime control to disable/enable the web UI
switch:
  - platform: elero_web
    name: "Elero Web UI"
    restore_mode: RESTORE_DEFAULT_ON
```

Siehe auch: [Installationsanleitung](INSTALLATION.md) | [README](../README.md) | [Beispiel-YAML](../example.yaml)
