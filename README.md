# esphome-gea

[![Compile](https://github.com/mguaylam/esphome-gea/actions/workflows/compile.yml/badge.svg)](https://github.com/mguaylam/esphome-gea/actions/workflows/compile.yml)
[![ESPHome](https://img.shields.io/badge/ESPHome-external%20component-blue?logo=esphome)](https://esphome.io/components/external_components)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-compatible-41bdf5?logo=home-assistant)](https://www.home-assistant.io/)
[![ESP32](https://img.shields.io/badge/ESP32-supported-red?logo=espressif)](https://www.espressif.com/)

An [ESPHome](https://esphome.io/) external component for monitoring and controlling GE appliances via the **GEA3 serial protocol**. Integrates natively with [Home Assistant](https://www.home-assistant.io/) — no cloud, no MQTT, no proprietary app required.

> Tested with a GE PDP715SYV0FS dishwasher and a GE PFQ97HSPVDS washer/dryer combo.

---

## Table of Contents

- [Features](#features)
- [Hardware](#hardware)
- [Installation](#installation)
- [Configuration](#configuration)
  - [Hub](#hub)
  - [Sensor](#sensor-read-only)
  - [Binary Sensor](#binary-sensor-read-only)
  - [Switch](#switch-read-write)
  - [Select](#select-read-write)
  - [Text Sensor](#text-sensor-read-only)
  - [Button](#button-write-only)
  - [Number](#number-read-write)
- [Protocol Overview](#protocol-overview)
- [ERD Discovery](#erd-discovery)
- [Examples](#examples)
- [License](#license)

---

## Features

- **Full bidirectional control** — read sensor values, toggle switches, change cycle settings, trigger remote start/stop
- **Auto-discovery** — subscribes to all ERDs on boot; unknown registers are logged for reverse-engineering
- **Plug-and-play addressing** — automatically detects appliance bus address from first valid packet
- **Resilient** — periodic re-subscription recovers state after appliance power cycles
- **Flexible decoding** — 13 numeric types (`uint8`, `uint16_be`, `int32_le`, …), raw hex, ASCII strings, and enum option maps
- **Multiple entity platforms** — sensor, binary sensor, switch, select, text sensor, button, number
- **Bus health indicator** — `is_bus_connected()` lambda for status LEDs
- **Native HA integration** — device classes, state classes, diagnostic categories all supported

---

## Hardware

### Bill of Materials

| Part | Notes |
|------|-------|
| [Seeed XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) | Compact, 3.3 V, native USB |
| FirstBuild RJ45 GEA breakout | Breaks out the GEA3 connector on GE appliances |
| 3× indicator LEDs + resistors | Optional: heartbeat, Wi-Fi, bus status |

### Wiring

```
   XIAO ESP32-C3                 GE Appliance
   ┌─────────────┐               ┌──────────────────┐
   │             │               │                  │
   │  D6 (TX) ──────────────────── GEA3 RX          │
   │  D7 (RX) ──────────────────── GEA3 TX          │
   │       GND ──────────────────── GND             │
   │       3V3 ──────────────────── 3.3V (optional) │
   │             │               │                  │
   │  D0 ─── [LED] ─── GND      └──────────────────┘
   │  D1 ─── [LED] ─── GND         (via RJ45 breakout)
   │  D2 ─── [LED] ─── GND
   └─────────────┘
     Heartbeat / Wi-Fi / Bus
```

> The GEA3 connector on GE appliances is typically accessible behind the appliance's service panel via an RJ45 jack. The FirstBuild breakout board makes this simple to tap into.

### UART Settings

| Parameter | Value |
|-----------|-------|
| Baud rate | **230,400** |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |

> **Note:** 19,200 baud is GEA2 (older protocol) and is not supported by this component.

---

## Installation

Add the external component to your ESPHome configuration:

```yaml
external_components:
  - source: github://mguaylam/esphome-gea
    components: [gea]
```

Configure the UART bus on the appropriate pins for your board:

```yaml
uart:
  id: uart_gea
  tx_pin: GPIO21   # D6 on XIAO ESP32-C3
  rx_pin: GPIO20   # D7 on XIAO ESP32-C3
  baud_rate: 230400
```

---

## Configuration

### Hub

The `gea:` block defines the communication hub. All entity platforms reference it via `gea_id`.

```yaml
gea:
  id: gea_hub
  uart_id: uart_gea
  src_address: 0xE4        # Our bus address (default: 0xBB)
  # dest_address: 0xC0     # Optional; auto-detected if omitted
  resubscribe_interval: 60s
```

| Option | Default | Description |
|--------|---------|-------------|
| `uart_id` | required | ID of the `uart:` block |
| `src_address` | `0xBB` | Source address on the GEA bus |
| `dest_address` | auto | Appliance address; detected from first packet if not set |
| `resubscribe_interval` | `60s` | How often to re-send subscribe-all to keep state fresh |

---

### Sensor (read-only)

Publishes an ERD value as a floating-point number.

```yaml
sensor:
  - platform: gea
    gea_id: gea_hub
    name: "Elapsed Time"
    erd: 0x0702
    decode: uint32_be
    byte_offset: 0
    unit_of_measurement: s
    state_class: total_increasing
```

**`decode` types:** `uint8`, `uint16_be`, `uint16_le`, `uint32_be`, `uint32_le`, `int8`, `int16_be`, `int16_le`, `int32_be`, `int32_le`, `bool`

---

### Binary Sensor (read-only)

Publishes a single bit or byte as a boolean.

```yaml
binary_sensor:
  - platform: gea
    gea_id: gea_hub
    name: "Door"
    erd: 0x2012
    bitmask: 0x01
    device_class: door
```

| Option | Description |
|--------|-------------|
| `bitmask` | Bit mask applied after `byte_offset` extraction |
| `inverted` | Invert the boolean result |

---

### Switch (read-write)

Reads a boolean ERD and writes it back on toggle.

```yaml
switch:
  - platform: gea
    gea_id: gea_hub
    name: "Extra Dry"
    erd: 0x3230
    byte_offset: 2
    bitmask: 0x01
```

A separate `write_erd` can be specified if the read and write addresses differ.

---

### Select (read-write)

Maps numeric ERD values to human-readable labels.

```yaml
select:
  - platform: gea
    gea_id: gea_hub
    name: "Wash Cycle"
    erd: 0x321B
    decode: uint8
    options:
      0x00: "AutoSense"
      0x01: "Heavy"
      0x02: "Normal"
      0x03: "1 Hour Wash"
      0x04: "Rinse Only"
```

---

### Text Sensor (read-only)

Decodes an ERD as a string. Supports three modes:

```yaml
text_sensor:
  # ASCII string (null-terminated)
  - platform: gea
    gea_id: gea_hub
    name: "Cycle Name"
    erd: 0x301C
    decode: ascii

  # Raw hex bytes
  - platform: gea
    gea_id: gea_hub
    name: "Raw State"
    erd: 0x3001
    decode: raw

  # Numeric value mapped to labels
  - platform: gea
    gea_id: gea_hub
    name: "Machine State"
    erd: 0x2000
    decode: uint8
    options:
      0x00: "Idle"
      0x01: "Running"
      0x02: "Paused"
```

---

### Button (write-only)

Sends a fixed payload when pressed. No state feedback.

```yaml
button:
  - platform: gea
    gea_id: gea_hub
    name: "Remote Start"
    erd: 0x2149
    data: [0x01]
```

---

### Number (read-write)

Bidirectional numeric ERD control with min/max/step validation.

```yaml
number:
  - platform: gea
    gea_id: gea_hub
    name: "Cycle Parameter"
    erd: 0xD004
    decode: uint16_be
    min_value: 0
    max_value: 65535
    step: 1
    entity_category: diagnostic
```

---

## Protocol Overview

GEA3 is a full-duplex serial protocol. Each frame has the following structure:

```
 ┌──────┬──────┬─────┬─────┬──────────────┬─────────┬──────┐
 │ STX  │ DEST │ LEN │ SRC │   PAYLOAD    │   CRC   │ ETX  │
 │ 0xE2 │  1B  │  1B │  1B │   n bytes    │ 2 bytes │ 0xE3 │
 └──────┴──────┴─────┴─────┴──────────────┴─────────┴──────┘
```

- **STX/ETX:** Frame delimiters (`0xE2` / `0xE3`)
- **LEN:** Total logical length = `7 + len(payload)`
- **CRC:** CRC-16/CCITT, polynomial `0x1021`, seed `0x1021`, computed over `DEST + LEN + SRC + PAYLOAD`
- **Escaping:** Control bytes `0xE0–0xE3` inside the payload are prefixed with `0xE0`

### Command Reference

| Command | Code | Direction | Purpose |
|---------|------|-----------|---------|
| Read Request | `0xA0` | → Appliance | Query ERD value |
| Read Response | `0xA1` | ← Appliance | Returns ERD data |
| Write Request | `0xA2` | → Appliance | Set ERD value |
| Write Response | `0xA3` | ← Appliance | Confirms write success |
| Subscribe-All | `0xA4` | → Appliance | Trigger full ERD publication |
| Subscribe-All Ack | `0xA5` | ← Appliance | Confirms subscription |
| Publication | `0xA6` | ← Appliance | Broadcasts ERD changes |
| Publication Ack | `0xA7` | → Appliance | Acknowledges publication |
| ACK | `0xE1` | ↔ Both | Single-byte acknowledgement |

---

## ERD Discovery

**ERD** (Entity Reference Designator) is a 16-bit identifier for a data register on the appliance — like a temperature reading, machine state, or cycle selection.

On boot the component sends a subscribe-all command. The appliance responds by publishing all supported ERDs. Any ERD not matched to a configured entity is logged at `INFO` level:

```
[I][gea:042]: Discovered ERD 0x2007: 00 00
[I][gea:042]: Discovered ERD 0x200A: 00 00 00 00
```

You can add these to your configuration as raw `text_sensor` entities to start reverse-engineering their meaning.

---

## Examples

Complete working configurations are included for two appliances:

| File | Appliance | Highlights |
|------|-----------|------------|
| [`dishwasher.yaml`](dishwasher.yaml) | GE PDP715SYV0FS | Cycle selection, elapsed time, ASCII cycle name, model retrieval button |
| [`washer.yaml`](washer.yaml) | GE PFQ97HSPVDS (Ultrafast Combo) | Time remaining, door/lock/pump sensors, 200+ cycle options, remote start/stop, detergent/softener dosing |

---

## License

MIT License

Copyright (c) 2024 mguaylam

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
