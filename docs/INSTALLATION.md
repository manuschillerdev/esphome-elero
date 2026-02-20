# Installationsanleitung: Elero ESPHome Component

Diese Anleitung führt dich Schritt für Schritt durch die komplette Einrichtung der Elero-Komponente, vom Zusammenbau der Hardware bis zur fertigen Home-Assistant-Integration.

---

## Inhaltsverzeichnis

1. [Benötigte Teile](#1-benötigte-teile)
2. [Hardware zusammenbauen](#2-hardware-zusammenbauen)
3. [ESPHome installieren](#3-esphome-installieren)
4. [Ersteinrichtung: Frequenz testen](#4-ersteinrichtung-frequenz-testen)
5. [Blind-Adressen herausfinden](#5-blind-adressen-herausfinden)
6. [Endgültige Konfiguration](#6-endgültige-konfiguration)
7. [In Home Assistant einbinden](#7-in-home-assistant-einbinden)
8. [Mehrere Rollläden hinzufügen](#8-mehrere-rollläden-hinzufügen)

---

## 1. Benötigte Teile

### Einkaufsliste

| Teil | Beschreibung | Ungefährer Preis |
|---|---|---|
| ESP32 Board | z.B. D1 Mini ESP32, ESP32-DevKit | ~5-10 EUR |
| CC1101 Modul | 868 MHz empfohlen für Europa | ~3-5 EUR |
| Dupont-Kabel | 7 Stück Female-Female | ~2 EUR |
| USB-Kabel | Micro-USB oder USB-C (je nach Board) | vorhanden |
| Elero Fernbedienung | z.B. TempoTel 2 (bereits vorhanden) | - |

> **Tipp:** 868 MHz CC1101-Module haben deutlich bessere Reichweite als 433 MHz. Falls möglich, verwende 868 MHz.

### Werkzeug

- Computer mit USB-Anschluss
- Python 3.9+ installiert
- Internetzugang

---

## 2. Hardware zusammenbauen

### Schritt 2.1: CC1101 an ESP32 anschließen

Verbinde die Pins wie folgt mit Dupont-Kabeln:

```
CC1101          ESP32 (Standard-Pinout)
──────          ──────────────────────
VCC  ──────>    3V3  (3.3 Volt!)
GND  ──────>    GND
SCK  ──────>    GPIO18
MOSI ──────>    GPIO23
MISO ──────>    GPIO19
CSN  ──────>    GPIO5
GDO0 ──────>    GPIO26
```

> **WARNUNG:** CC1101 arbeitet mit **3.3V**. Niemals an 5V anschließen!

### Schritt 2.2: Verkabelung prüfen

Prüfe alle Verbindungen nochmals:
- Keine losen Kabel
- Keine Kurzschlüsse zwischen benachbarten Pins
- VCC an 3.3V (nicht 5V!)

### Schritt 2.3: ESP32 an PC anschließen

Verbinde den ESP32 per USB mit deinem Computer. Der Treiber sollte automatisch installiert werden.

---

## 3. ESPHome installieren

### Option A: ESPHome Dashboard (Home Assistant Add-on)

1. In Home Assistant: **Einstellungen > Add-ons > Add-on Store**
2. Suche nach "ESPHome"
3. Installieren und starten
4. Öffne das ESPHome Dashboard

### Option B: ESPHome CLI (Kommandozeile)

```bash
# Python und pip müssen installiert sein
pip install esphome

# Prüfen ob es funktioniert
esphome version
```

---

## 4. Ersteinrichtung: Frequenz testen

Bevor du die Rollladen-Steuerung konfigurierst, musst du sicherstellen, dass die CC1101-Kommunikation funktioniert.

### Schritt 4.1: Basis-Konfiguration erstellen

Erstelle die Datei `elero-blinds.yaml`:

```yaml
esphome:
  name: elero-blinds
  friendly_name: "Elero Rollladen"

esp32:
  board: esp32dev

wifi:
  ssid: "DEIN_WLAN_NAME"
  password: "DEIN_WLAN_PASSWORT"

  ap:
    ssid: "Elero-Fallback"
    password: "fallback123"

logger:
  level: DEBUG

api:
  encryption:
    key: "GeneriereDiesenSchlüsselPerESPHomeDashboard"

ota:
  - platform: esphome
    password: "DeinOTAPasswort"

# Externe Elero-Komponente
external_components:
  - source: github://andyboeh/esphome-elero

# SPI-Bus
spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

# Elero Hub - Standardfrequenz
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26

# Dummy Cover (benötigt für Kompilierung)
cover:
  - platform: elero
    blind_address: 0x000001
    channel: 1
    remote_address: 0x000001
    name: "Test"

# Scan-Buttons
button:
  - platform: elero
    name: "Elero Start Scan"
    scan_start: true
  - platform: elero
    name: "Elero Stop Scan"
    scan_start: false
```

### Schritt 4.2: Erstmalig flashen

**Per CLI:**
```bash
# Erstmaliges Flashen über USB
esphome run elero-blinds.yaml
```

**Per Dashboard:**
1. Neue Konfiguration anlegen
2. YAML einfügen
3. "Install" klicken > "Plug into this computer"

### Schritt 4.3: Frequenz testen

1. Öffne den ESPHome-Log:
   ```bash
   esphome logs elero-blinds.yaml
   ```
2. Drücke eine Taste auf deiner Elero-Fernbedienung
3. **Wenn im Log Pakete erscheinen** (Zeilen mit `rcv'd: len=...`): Die Frequenz stimmt!
4. **Wenn NICHTS passiert**: Die Frequenz muss angepasst werden.

### Schritt 4.4: Frequenz anpassen (falls nötig)

Ändere die Frequenz-Register in der Konfiguration:

```yaml
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26
  freq0: 0xc0    # Häufige Alternative statt 0x7a
  freq1: 0x71
  freq2: 0x21
```

Flashe erneut und teste. Gängige Kombinationen:

| Konfiguration | freq0 | freq1 | freq2 |
|---|---|---|---|
| Standard 868 MHz | `0x7a` | `0x71` | `0x21` |
| Alternative 868 MHz | `0xc0` | `0x71` | `0x21` |

> **Tipp:** Probiere `0xc0` zuerst, das ist die häufigste Alternative.

---

## 5. Blind-Adressen herausfinden

Wenn die Kommunikation funktioniert, kannst du die Adressen deiner Rollläden ermitteln.

### Schritt 5.1: Scan starten

1. Öffne den ESPHome-Log
2. Drücke den "Start Scan"-Button in Home Assistant (oder per Log beobachten)
3. Betätige die Fernbedienung: Wähle einen Rollladen aus und drücke Hoch/Runter

### Schritt 5.2: Log-Ausgabe lesen

Im Log siehst du Zeilen wie:

```
[I][elero:xxx]: Discovered new device: addr=0xa831e5, remote=0xf0d008, ch=4, rssi=-52.0
```

Und die detaillierten Pakete:

```
[D][elero:xxx]: rcv'd: len=29, cnt=45, typ=0x6a, typ2=0x00, hop=0x0a, syst=0x01,
                chl=04, src=0xf0d008, bwd=0xf0d008, fwd=0xf0d008, #dst=01,
                dst=0xa831e5, rssi=-52.0, lqi=47, crc=1,
                payload=[0x00 0x04 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00]
```

### Schritt 5.3: Werte notieren

Aus der Log-Zeile oben:

```
blind_address:  0xa831e5   (= dst, die Blind-Adresse)
remote_address: 0xf0d008   (= src/bwd/fwd, die Fernbedienungs-Adresse)
channel:        4           (= chl)
pck_inf1:       0x6a        (= typ)
pck_inf2:       0x00        (= typ2)
hop:            0x0a        (= hop)
payload_1:      0x00        (= payload[0])
payload_2:      0x04        (= payload[1])
```

### Schritt 5.4: Befehle identifizieren

Drücke die einzelnen Tasten auf der Fernbedienung und beobachte das 5. Payload-Byte (Position [4]):

```
Taste HOCH:   payload=[0x00 0x04 0x00 0x00 0x20 ...]  → command_up:   0x20
Taste RUNTER: payload=[0x00 0x04 0x00 0x00 0x40 ...]  → command_down: 0x40
Taste STOPP:  payload=[0x00 0x04 0x00 0x00 0x10 ...]  → command_stop: 0x10
```

> Falls die Werte mit den Standardwerten übereinstimmen, müssen sie nicht extra konfiguriert werden.

### Schritt 5.5: Scan stoppen

Drücke den "Stop Scan"-Button. Im Log erscheint eine Zusammenfassung:

```
[I][elero.button:xxx]: Stopped Elero RF scan. Discovered 2 device(s).
[I][elero.button:xxx]:   addr=0xa831e5 remote=0xf0d008 ch=4 rssi=-52.0 state=top seen=5
[I][elero.button:xxx]:   addr=0xb912f3 remote=0xf0d008 ch=5 rssi=-61.0 state=bottom seen=3
```

---

## 6. Endgültige Konfiguration

Ersetze den Dummy-Cover mit den ermittelten Werten:

```yaml
esphome:
  name: elero-blinds
  friendly_name: "Elero Rollladen"

esp32:
  board: esp32dev

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

  ap:
    ssid: "Elero-Fallback"
    password: !secret ap_password

logger:
  level: INFO    # DEBUG nur bei Problemen

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

external_components:
  - source: github://andyboeh/esphome-elero

spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26
  freq0: 0xc0     # Anpassen falls nötig
  freq1: 0x71
  freq2: 0x21

# ── Rollläden ──
cover:
  - platform: elero
    name: "Schlafzimmer"
    blind_address: 0xa831e5
    channel: 4
    remote_address: 0xf0d008
    open_duration: 25s        # Für Positionssteuerung (optional)
    close_duration: 22s       # Für Positionssteuerung (optional)
    poll_interval: 5min

# ── Diagnose-Sensoren (optional) ──
sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer RSSI"

text_sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer Status"

# ── Scan-Buttons (optional, kann nach Einrichtung entfernt werden) ──
button:
  - platform: elero
    name: "Elero Start Scan"
    scan_start: true
  - platform: elero
    name: "Elero Stop Scan"
    scan_start: false
```

Flashe die endgültige Konfiguration:

```bash
esphome run elero-blinds.yaml
```

---

## 7. In Home Assistant einbinden

### Automatische Erkennung

Falls `api:` konfiguriert ist, wird das Gerät automatisch in Home Assistant entdeckt:

1. **Einstellungen > Geräte & Dienste**
2. Das ESPHome-Gerät sollte unter "Entdeckt" erscheinen
3. "Konfigurieren" klicken

### Manuelle Einbindung

Falls das Gerät nicht automatisch gefunden wird:

1. **Einstellungen > Geräte & Dienste > Integration hinzufügen**
2. "ESPHome" suchen
3. IP-Adresse des ESP32 eingeben (steht im Log)
4. API-Schlüssel eingeben

### Entities prüfen

Nach der Einbindung stehen folgende Entities zur Verfügung:

- `cover.schlafzimmer` - Rollladen-Steuerung
- `sensor.schlafzimmer_rssi` - Signalstärke
- `text_sensor.schlafzimmer_status` - Status-Text
- `button.elero_start_scan` - Scan starten
- `button.elero_stop_scan` - Scan stoppen

---

## 8. Mehrere Rollläden hinzufügen

Für jeden zusätzlichen Rollladen einen weiteren Cover-Eintrag hinzufügen:

```yaml
cover:
  - platform: elero
    name: "Schlafzimmer"
    blind_address: 0xa831e5
    channel: 4
    remote_address: 0xf0d008
    open_duration: 25s
    close_duration: 22s

  - platform: elero
    name: "Wohnzimmer Links"
    blind_address: 0xb912f3
    channel: 5
    remote_address: 0xf0d008
    open_duration: 30s
    close_duration: 27s

  - platform: elero
    name: "Wohnzimmer Rechts"
    blind_address: 0xc4a7d2
    channel: 6
    remote_address: 0xf0d008
    open_duration: 30s
    close_duration: 27s

# RSSI und Status für alle Rollläden
sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer RSSI"
  - platform: elero
    blind_address: 0xb912f3
    name: "Wohnzimmer Links RSSI"
  - platform: elero
    blind_address: 0xc4a7d2
    name: "Wohnzimmer Rechts RSSI"

text_sensor:
  - platform: elero
    blind_address: 0xa831e5
    name: "Schlafzimmer Status"
  - platform: elero
    blind_address: 0xb912f3
    name: "Wohnzimmer Links Status"
  - platform: elero
    blind_address: 0xc4a7d2
    name: "Wohnzimmer Rechts Status"
```

> **Hinweis:** Die Komponente staggert die Polling-Abfragen automatisch (5 Sekunden Versatz pro Rollladen), um Kollisionen auf dem Funkkanal zu vermeiden.

---

## Nächste Schritte

- [Konfigurationsreferenz](CONFIGURATION.md) - Alle Parameter im Detail
- [Beispielkonfigurationen](examples/) - Vorlagen für verschiedene Szenarien
- [README](../README.md) - Übersicht und Fehlerbehebung
