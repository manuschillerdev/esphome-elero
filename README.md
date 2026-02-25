# Elero Remote Control Component for ESPHome

> Steuere Elero Rollläden und Lichter bidirektional via ESP32 + CC1101 direkt aus Home Assistant.
> Inklusive WebGui für RF-Scan mit Geräteerkennung und YAML-Template für die weitere Nutzung.
> Zahlreiche Detailverbesserung (Motorquitierung, variabler Poll bei Handbedienung etc.)

[![ESPHome](https://img.shields.io/badge/ESPHome-Component-blue)](https://esphome.io/)
[![License](https://img.shields.io/badge/License-GPLv3-green)](LICENSE)

---

## Inhaltsverzeichnis

- [Funktionsumfang](#funktionsumfang)
- [Voraussetzungen](#voraussetzungen)
- [Hardware-Verkabelung](#hardware-verkabelung)
- [Schnellstart](#schnellstart)
- [Konfigurationsreferenz](#konfigurationsreferenz)
- [Blind-Adressen ermitteln](#blind-adressen-ermitteln)
- [Positionssteuerung](#positionssteuerung)
- [Tilt-Steuerung](#tilt-steuerung)
- [Lichtsteuerung](#lichtsteuerung)
- [Diagnose-Sensoren](#diagnose-sensoren)
- [RF-Discovery (Scan)](#rf-discovery-scan)
- [Home Assistant Integration](#home-assistant-integration)
- [Fehlerbehebung](#fehlerbehebung)
- [Getestete Konfigurationen](#getestete-konfigurationen)
- [Weiterführende Dokumentation](#weiterführende-dokumentation)
- [Credits](#credits)

---

## Funktionsumfang

| Feature | Status |
|---|---|
| Rollläden hoch/runter/stopp steuern | Stabil |
| Bidirektionale Kommunikation (Status empfangen) | Stabil |
| Positionssteuerung (zeitbasiert) | Experimentell |
| Tilt/Kipp-Steuerung | Experimentell |
| RSSI-Signalstärke als Sensor | Stabil |
| Blind-Status als Text-Sensor | Stabil |
| RF-Discovery (Blinds finden) | Stabil |
| Web-UI fuer Discovery und YAML-Export | Stabil |
| Mehrere Blinds gleichzeitig | Stabil |
| TempoTel 2 Kompatibilität | Getestet |
| Lichter schalten (Ein/Aus und Dimmen) | Stabil |

## Voraussetzungen

### Hardware

- **ESP32** (empfohlen: D1 Mini ESP32, WT32-ETH01, ESP32-DevKit)
- **CC1101 Funkmodul** (868 MHz für Europa, 433 MHz alternativ)
- 5 Kabelverbindungen (SPI + GDO0)
- Bestehende Elero-Fernbedienung (z.B. TempoTel 2) zum Auslesen der Protokollwerte

### Software

- [ESPHome](https://esphome.io/) 2023.2.0 oder neuer
- [Home Assistant](https://www.home-assistant.io/) (empfohlen, aber nicht zwingend)

## Hardware-Verkabelung

### Pinbelegung CC1101 zu ESP32

```
CC1101 Modul          ESP32
┌─────────────┐       ┌──────────┐
│ VCC  (3.3V) │──────>│ 3V3      │
│ GND         │──────>│ GND      │
│ SCK  (SCLK) │──────>│ GPIO18   │
│ MOSI (SI)   │──────>│ GPIO23   │
│ MISO (SO)   │──────>│ GPIO19   │
│ CSN  (CS)   │──────>│ GPIO5    │
│ GDO0        │──────>│ GPIO26   │
│ GDO2        │       │ (nicht   │
│             │       │ benötigt)│
└─────────────┘       └──────────┘
```

> **Hinweis:** Im Gegensatz zu anderen Projekten wird GDO2 **nicht** benötigt. Nur GDO0 ist erforderlich.

### Alternative Pinbelegung (WT32-ETH01)

```
CC1101 Modul          WT32-ETH01
┌─────────────┐       ┌──────────┐
│ VCC  (3.3V) │──────>│ 3V3      │
│ GND         │──────>│ GND      │
│ SCK  (SCLK) │──────>│ GPIO14   │
│ MOSI (SI)   │──────>│ GPIO15   │
│ MISO (SO)   │──────>│ GPIO35   │
│ CSN  (CS)   │──────>│ GPIO4    │
│ GDO0        │──────>│ GPIO2    │
└─────────────┘       └──────────┘
```

## Schnellstart

### 1. ESPHome-Projekt einrichten

Erstelle eine neue YAML-Datei (z.B. `elero-blinds.yaml`):

```yaml
esphome:
  name: elero-blinds
  friendly_name: "Elero Rollladen"

esp32:
  board: esp32dev

# WLAN-Konfiguration
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

# Fallback-Hotspot bei WLAN-Problemen
  ap:
    ssid: "Elero-Blinds Fallback"
    password: !secret ap_password

# Logging aktivieren (wichtig für Ersteinrichtung!)
logger:
  level: DEBUG

# Home Assistant API
api:
  encryption:
    key: !secret api_key

# OTA Updates
ota:
  - platform: esphome
    password: !secret ota_password

# Externe Elero-Komponente laden
external_components:
  - source: github://pfriedrich84/esphome-elero

# SPI-Bus konfigurieren
spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

# Elero CC1101 Hub
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26
  # Frequenz-Register anpassen falls nötig
  # Standard-Einstellung für 868.35 MHz: freq0: 0x7a, freq1: 0x71, freq2: 0x21
  # Alternative für 868.95 MHz (falls keine Pakete empfangen): freq0: 0xc0, freq1: 0x71, freq2: 0x21
  # freq0: 0x7a
  # freq1: 0x71
  # freq2: 0x21
```

### 2. Blind-Adressen ermitteln

Füge zunächst eine Dummy-Konfiguration hinzu und aktiviere den RF-Scan:

```yaml
# Dummy-Cover (wird später mit echten Werten ersetzt)
cover:
  - platform: elero
    blind_address: 0x000001
    channel: 1
    remote_address: 0x000001
    name: "Dummy"

# Scan-Buttons zum Entdecken
button:
  - platform: elero
    name: "Elero Start Scan"
    scan_start: true
  - platform: elero
    name: "Elero Stop Scan"
    scan_start: false
```

Flashe das Gerät, öffne den Log und drücke Tasten auf deiner echten Fernbedienung. Die Adressen erscheinen im Log. Details dazu im Abschnitt [Blind-Adressen ermitteln](#blind-adressen-ermitteln).

### 3. Konfiguration vervollständigen

Ersetze die Dummy-Werte mit den ermittelten Adressen:

```yaml
cover:
  - platform: elero
    blind_address: 0xa831e5    # Adresse deines Rollos
    channel: 4                  # Kanal
    remote_address: 0xf0d008   # Adresse der Fernbedienung
    name: "Schlafzimmer"
```

### 4. Flashen und testen

```bash
esphome run elero-blinds.yaml
```

---

## Konfigurationsreferenz

### Plattform `elero` (Hub)

Der zentrale Hub für die CC1101-Kommunikation.

```yaml
elero:
  cs_pin: GPIO5         # Pflicht: SPI Chip-Select
  gdo0_pin: GPIO26      # Pflicht: CC1101 GDO0 Interrupt-Pin
  freq0: 0x7a           # Optional: Frequenz-Register FREQ0 (Standard: 0x7a)
  freq1: 0x71           # Optional: Frequenz-Register FREQ1 (Standard: 0x71)
  freq2: 0x21           # Optional: Frequenz-Register FREQ2 (Standard: 0x21)
```

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `cs_pin` | GPIO-Pin | Ja | - | SPI Chip-Select Pin |
| `gdo0_pin` | GPIO-Pin | Ja | - | CC1101 GDO0 Interrupt Pin |
| `freq0` | Hex (0x00-0xFF) | Nein | `0x7a` | CC1101 FREQ0 Register |
| `freq1` | Hex (0x00-0xFF) | Nein | `0x71` | CC1101 FREQ1 Register |
| `freq2` | Hex (0x00-0xFF) | Nein | `0x21` | CC1101 FREQ2 Register |

### Plattform `cover` (Rollladen)

Jeder Rollladen wird als eigener Cover-Eintrag konfiguriert.

```yaml
cover:
  - platform: elero
    name: "Schlafzimmer"          # Pflicht
    blind_address: 0xa831e5       # Pflicht
    channel: 4                     # Pflicht
    remote_address: 0xf0d008      # Pflicht
    open_duration: 25s             # Optional: Für Positionssteuerung
    close_duration: 22s            # Optional: Für Positionssteuerung
    poll_interval: 5min            # Optional
    supports_tilt: false           # Optional
    payload_1: 0x00                # Optional
    payload_2: 0x04                # Optional
    pck_inf1: 0x6a                 # Optional
    pck_inf2: 0x00                 # Optional
    hop: 0x0a                      # Optional
    command_up: 0x20               # Optional
    command_down: 0x40             # Optional
    command_stop: 0x10             # Optional
    command_check: 0x00            # Optional
    command_tilt: 0x24             # Optional
```

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `name` | String | Ja | - | Name in Home Assistant |
| `blind_address` | Hex (24-bit) | Ja | - | Adresse des Rollladens |
| `channel` | Int (0-255) | Ja | - | Kanal des Rollladens |
| `remote_address` | Hex (24-bit) | Ja | - | Adresse der zu simulierenden Fernbedienung |
| `open_duration` | Zeitdauer | Nein | `0s` | Fahrzeit zum vollständigen Öffnen |
| `close_duration` | Zeitdauer | Nein | `0s` | Fahrzeit zum vollständigen Schließen |
| `poll_interval` | Zeitdauer / `never` | Nein | `5min` | Status-Abfrageintervall |
| `supports_tilt` | Boolean | Nein | `false` | Tilt/Kipp-Unterstützung aktivieren |
| `payload_1` | Hex (0x00-0xFF) | Nein | `0x00` | Erstes Payload-Byte |
| `payload_2` | Hex (0x00-0xFF) | Nein | `0x04` | Zweites Payload-Byte |
| `pck_inf1` | Hex (0x00-0xFF) | Nein | `0x6a` | Erstes Paket-Info-Byte |
| `pck_inf2` | Hex (0x00-0xFF) | Nein | `0x00` | Zweites Paket-Info-Byte |
| `hop` | Hex (0x00-0xFF) | Nein | `0x0a` | Hop-Byte |
| `command_up` | Hex (0x00-0xFF) | Nein | `0x20` | Befehlscode: Hoch |
| `command_down` | Hex (0x00-0xFF) | Nein | `0x40` | Befehlscode: Runter |
| `command_stop` | Hex (0x00-0xFF) | Nein | `0x10` | Befehlscode: Stopp |
| `command_check` | Hex (0x00-0xFF) | Nein | `0x00` | Befehlscode: Status abfragen |
| `command_tilt` | Hex (0x00-0xFF) | Nein | `0x24` | Befehlscode: Tilt/Kipp |

### Plattform `sensor` (RSSI Signalstärke)

Zeigt die Empfangsstärke (RSSI) des letzten empfangenen Pakets eines bestimmten Rollladens in dBm.

```yaml
sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer RSSI"
```

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `blind_address` | Hex (24-bit) | Ja | - | Adresse des Rollladens |
| `name` | String | Ja | - | Name in Home Assistant |

### Plattform `text_sensor` (Status-Text)

Zeigt den aktuellen Blind-Status als lesbaren Text.

```yaml
text_sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer Status"
```

Mögliche Werte: `top`, `bottom`, `intermediate`, `tilt`, `top_tilt`, `bottom_tilt`, `moving_up`, `moving_down`, `start_moving_up`, `start_moving_down`, `stopped`, `blocking`, `overheated`, `timeout`, `on`, `unknown`

| Parameter | Typ | Pflicht | Standard | Beschreibung |
|---|---|---|---|---|
| `blind_address` | Hex (24-bit) | Ja | - | Adresse des Rollladens |
| `name` | String | Ja | - | Name in Home Assistant |

### Plattform `button` (RF-Scan)

Startet/stoppt einen RF-Discovery-Scan zum Finden von Elero-Geräten.

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
| `scan_start` | Boolean | Nein | `true` | `true` = Scan starten, `false` = Scan stoppen |
| `name` | String | Ja | - | Name in Home Assistant |

---

## Blind-Adressen ermitteln

Es gibt zwei Methoden, um die notwendigen Protokollwerte zu ermitteln:

### Methode 1: RF-Scan (empfohlen)

1. Flashe die Konfiguration mit Scan-Buttons und Dummy-Cover
2. Öffne den ESPHome-Log (`esphome logs elero-blinds.yaml`)
3. Drücke den "Start Scan"-Button in Home Assistant
4. Betätige die physische Elero-Fernbedienung (Taste hoch/runter/stopp)
5. Drücke den "Stop Scan"-Button
6. Im Log erscheinen die entdeckten Geräte:
   ```
   [I][elero:xxx]: Discovered new device: addr=0xa831e5, remote=0xf0d008, ch=4, rssi=-52.0
   [I][elero.button:xxx]: Stopped Elero RF scan. Discovered 1 device(s).
   [I][elero.button:xxx]:   addr=0xa831e5 remote=0xf0d008 ch=4 rssi=-52.0 state=top seen=3
   ```

### Methode 2: Log-Analyse (manuell)

1. Flashe die Konfiguration mit einem Dummy-Cover
2. Aktiviere Log-Level `DEBUG`
3. Drücke eine Taste auf der Fernbedienung und beobachte das Log:

   ```
   rcv'd: len=29, cnt=45, typ=0x6a, typ2=0x00, hop=0x0a, syst=0x01, chl=09,
          src=0x908bef, bwd=0x908bef, fwd=0x908bef, #dst=01, dst=0xe039c9,
          rssi=-84.0, lqi=47, crc= 1,
          payload=[0x00 0x04 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00]
   ```

4. Suche die Zeile, in der `src`, `bwd` und `fwd` identisch sind. Daraus liest du ab:

   | Log-Feld | Konfiguration | Wert im Beispiel |
   |---|---|---|
   | `src` / `bwd` / `fwd` | `remote_address` | `0x908bef` |
   | `dst` | `blind_address` | `0xe039c9` |
   | `chl` | `channel` | `9` |
   | `typ` | `pck_inf1` | `0x6a` |
   | `typ2` | `pck_inf2` | `0x00` |
   | `hop` | `hop` | `0x0a` |
   | `payload[0]` | `payload_1` | `0x00` |
   | `payload[1]` | `payload_2` | `0x04` |

5. Drücke Hoch/Runter/Stopp und prüfe das 5. Payload-Byte (payload[4]):
   - **Hoch** (`0x20`): `payload=[... 0x20 ...]`
   - **Runter** (`0x40`): `payload=[... 0x40 ...]`
   - **Stopp** (`0x10`): `payload=[... 0x10 ...]`

> **Wichtig:** Nur Pakete mit `len=29` sind relevant. Pakete mit `len=27` sind interne Nachrichten.

---

## Positionssteuerung

Die Positionssteuerung basiert auf Zeitmessung: Der Rollladen wird nach einer berechneten Fahrzeit gestoppt. Dafür müssen `open_duration` und `close_duration` konfiguriert sein.

```yaml
cover:
  - platform: elero
    # ...
    open_duration: 25s    # Zeit für komplettes Öffnen
    close_duration: 22s   # Zeit für komplettes Schließen
```

**Kalibrierung:**
1. Stoppe den Rollladen in der vollständig geschlossenen Position
2. Messe die Zeit bis zur vollständigen Öffnung mit einer Stoppuhr
3. Messe die Zeit für das vollständige Schließen
4. Trage die Werte ein

> **Hinweis:** Die Positionssteuerung ist experimentell. Bei vollständig offen oder geschlossen wird kein Stopp-Befehl gesendet, da der Rollladen selbst an den Endpositionen stoppt.

---

## Tilt-Steuerung

Für Rollläden mit Kipp-/Tilt-Funktion (z.B. Raffstore):

```yaml
cover:
  - platform: elero
    # ...
    supports_tilt: true
```

- Jeder Tilt-Wert > 0 sendet den Tilt-Befehl
- Tilt auf 0 setzen sendet aktuell keinen Befehl

---

## Lichtsteuerung

Elero-Lichtempfänger werden als `light`-Plattform konfiguriert und erscheinen in Home Assistant als vollständige Licht-Entitäten.

### Einfaches Ein/Aus-Licht

Ohne `dim_duration` (oder `dim_duration: 0s`) wird nur Ein/Aus unterstützt:

```yaml
light:
  - platform: elero
    name: "Hauslicht"
    blind_address: 0xc41a2b
    channel: 6
    remote_address: 0xf0d008
```

### Dimmbar (Helligkeitssteuerung)

Mit `dim_duration` wird die Zeit angegeben, die der Empfänger benötigt, um von 0 % auf 100 % zu dimmen. Home Assistant zeigt dann einen Helligkeitsregler:

```yaml
light:
  - platform: elero
    name: "Wohnzimmerlicht"
    blind_address: 0xc41a2b
    channel: 6
    remote_address: 0xf0d008
    dim_duration: 5s
```

- Der richtige Wert für `dim_duration` hängt vom jeweiligen Empfänger ab — typisch sind 3–8 Sekunden.
- Alle Protokoll-Parameter (`blind_address`, `channel`, `remote_address`, `payload_*`, `pck_inf*`, `hop`) werden genauso ermittelt wie bei einem Rollladen (via RF-Log oder Web-UI-Discovery).
- Vollständige Parameterliste: [Konfigurationsreferenz](docs/CONFIGURATION.md#plattform-light)

---

## Diagnose-Sensoren

### RSSI-Sensor

Überwacht die Signalstärke der Kommunikation:

```yaml
sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer RSSI"
```

**Richtwerte:**
| RSSI (dBm) | Bewertung |
|---|---|
| > -50 | Ausgezeichnet |
| -50 bis -70 | Gut |
| -70 bis -85 | Akzeptabel |
| < -85 | Schwach / unzuverlässig |

### Status-Text-Sensor

Zeigt den letzten empfangenen Blind-Status als Text:

```yaml
text_sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer Status"
```

Kann für Home-Assistant-Automationen genutzt werden:

```yaml
# Home Assistant Automation Beispiel
automation:
  - alias: "Warnung bei Rollladen-Blockade"
    trigger:
      - platform: state
        entity_id: text_sensor.schlafzimmer_status
        to: "blocking"
    action:
      - service: notify.notify
        data:
          message: "Rollladen Schlafzimmer ist blockiert!"
```

---

## RF-Discovery (Scan)

Der RF-Scan empfängt alle Elero-Nachrichten im Funkbereich und protokolliert sie:

```yaml
button:
  - platform: elero
    name: "Elero Start Scan"
    scan_start: true
  - platform: elero
    name: "Elero Stop Scan"
    scan_start: false
```

**Ablauf:**
1. "Start Scan" drücken (löscht vorherige Ergebnisse)
2. Alle Fernbedienungen und Rollläden im Bereich betätigen
3. "Stop Scan" drücken
4. Ergebnisse im ESPHome-Log ablesen

Pro entdecktem Gerät werden protokolliert: Adresse, Remote-Adresse, Kanal, RSSI, Status, Häufigkeit.

### Web-UI (Alternative zum Log)

Fuer eine komfortablere Geräteerkennung steht eine optionale Web-Oberflaeche bereit:

```yaml
# HTTP-Server (wird von elero_web automatisch geladen)
# Nicht web_server: verwenden – das aktiviert die Standard-UI unter / wieder
web_server_base:
  port: 80

# Elero Web-UI aktivieren
elero_web:
```

Danach ist die Oberflaeche unter `http://<device-ip>/elero` erreichbar. Funktionen:

- **RF-Scan steuern** - Scan starten/stoppen direkt im Browser
- **Gefundene Geraete anzeigen** - Adresse, Kanal, Remote, RSSI, Status, Hop
- **Konfigurierte Covers anzeigen** - Name, Position, Betriebszustand
- **YAML exportieren** - Generiert Copy-Paste-fertige YAML-Konfiguration

Die Web-UI bietet zudem eine REST-API unter `/elero/api/*` mit CORS-Unterstuetzung fuer Cross-Origin-Zugriff.

#### Web-UI zur Laufzeit deaktivieren

Optional kann die Web-UI zur Laufzeit über einen Switch aktiviert/deaktiviert werden:

```yaml
switch:
  - platform: elero_web
    name: "Elero Web UI"
    restore_mode: RESTORE_DEFAULT_ON
```

Wenn der Switch ausgeschaltet ist, antworten alle `/elero`-Endpoints mit HTTP 503. Dies ist nützlich, um unerwünschten Zugriff zu blockieren, ohne die Komponente neu zu flashen.

---

## Home Assistant Integration

Nach dem Flashen erscheint das Gerät automatisch in Home Assistant (wenn `api:` konfiguriert ist). Die Entities:

| Entity-Typ | Beispiel | Beschreibung |
|---|---|---|
| `cover.schlafzimmer` | Cover | Hoch/Runter/Stopp, Position, Tilt |
| `sensor.schlafzimmer_rssi` | Sensor | Signalstärke in dBm |
| `text_sensor.schlafzimmer_status` | Text Sensor | Aktueller Status |
| `button.elero_start_scan` | Button | RF-Scan starten |
| `button.elero_stop_scan` | Button | RF-Scan stoppen |

### Dashboard-Karte

```yaml
type: entities
title: Elero Rollläden
entities:
  - entity: cover.schlafzimmer
  - entity: sensor.schlafzimmer_rssi
  - entity: text_sensor.schlafzimmer_status
  - type: divider
  - entity: button.elero_start_scan
  - entity: button.elero_stop_scan
```

---

## Fehlerbehebung

### Kein Log-Output beim Drücken der Fernbedienung

- **Verkabelung prüfen**: Alle SPI-Pins korrekt angeschlossen?
- **Frequenz testen**: Europäische Elero-Module verwenden meist 868 MHz, aber es gibt zwei gängige Varianten:
  - **Standard (868.35 MHz):** `freq0: 0x7a, freq1: 0x71, freq2: 0x21` ← **versuche zuerst diese**
  - **Alternative (868.95 MHz):** `freq0: 0xc0, freq1: 0x71, freq2: 0x21` ← falls obige nicht funktioniert

  Beispiel:
  ```yaml
  elero:
    cs_pin: GPIO5
    gdo0_pin: GPIO26
    freq0: 0xc0    # Alternative wenn Standard nicht funktioniert
    freq1: 0x71
    freq2: 0x21
  ```
- **3.3V prüfen**: CC1101 benötigt stabile 3.3V Versorgung

### Rollladen reagiert nicht auf Befehle

- Alle Werte sorgfältig mit der echten Fernbedienung vergleichen
- Alle Werte außer `cnt` müssen exakt übereinstimmen
- `blind_address` und `remote_address` nicht vertauscht?

### Schwaches Signal / unzuverlässige Steuerung

- RSSI-Sensor hinzufügen und beobachten
- Werte > -70 dBm sind typischerweise gut
- CC1101-Antenne repositionieren
- 868 MHz Module haben deutlich bessere Reichweite als 433 MHz

### Blind meldet BLOCKING oder OVERHEATED

- Das sind Fehlerzustände des Rollladen-Motors
- Den Rollladen physisch prüfen (Blockade, Überhitzung)
- Die Komponente loggt Warnungen für diese Zustände

### ESP32 startet nicht / Bootloop

- SPI-Pins nicht mit Boot-kritischen GPIOs belegen (z.B. GPIO0, GPIO2, GPIO12)
- Ausreichend Strom (mindestens 500mA) sicherstellen

---

## Getestete Konfigurationen

| Board | CC1101-Modul | Frequenz | Bewertung |
|---|---|---|---|
| D1 Mini ESP32 | Standard CC1101 | 433 MHz | Funktioniert, eingeschränkte Reichweite |
| WT32-ETH01 | RWE Smart Home CC1101 | 868 MHz | Sehr gute Reichweite und Empfang |
| ESP32-DevKit V1 | Standard CC1101 | 868 MHz | Gut |

---

## Weiterführende Dokumentation

- [docs/INSTALLATION.md](docs/INSTALLATION.md) - Detaillierte Schritt-für-Schritt Installationsanleitung
- [docs/CONFIGURATION.md](docs/CONFIGURATION.md) - Vollständige Konfigurationsreferenz mit Beispielen
- [example.yaml](example.yaml) - Minimales Beispiel
- [docs/examples/](docs/examples/) - Weitere Beispielkonfigurationen

---

## Credits

Dieses Projekt basiert auf der Arbeit von:

- [QuadCorei8085/elero_protocol](https://github.com/QuadCorei8085/elero_protocol) (MIT) - Verschlüsselungs-/Entschlüsselungsstrukturen
- [stanleypa/eleropy](https://github.com/stanleypa/eleropy) (GPLv3) - Fernbedienungs-Handling
- [andyboeh/esphome-elero](https://github.com/pfriedrich84/esphome-elero) - Grundlage für diese Steuerung, wobei deses Repo ein nahezu vollständiger Rebuild ist
