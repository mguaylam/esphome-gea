# esphome-gea

[![Compile](https://github.com/mguaylam/esphome-gea/actions/workflows/compile.yml/badge.svg)](https://github.com/mguaylam/esphome-gea/actions/workflows/compile.yml)
[![Test](https://github.com/mguaylam/esphome-gea/actions/workflows/test.yml/badge.svg)](https://github.com/mguaylam/esphome-gea/actions/workflows/test.yml)
[![ESPHome](https://img.shields.io/badge/ESPHome-external%20component-blue?logo=esphome)](https://esphome.io/components/external_components)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-compatible-41bdf5?logo=home-assistant)](https://www.home-assistant.io/)
[![ESP32](https://img.shields.io/badge/ESP32-supported-red?logo=espressif)](https://www.espressif.com/)

An [ESPHome](https://esphome.io/) external component for monitoring and controlling GE appliances via the **GEA3 serial bus**, with native [Home Assistant](https://www.home-assistant.io/) integration.

![Architecture](docs/architecture.svg)

---

## What is this?

Your GE dishwasher, washer, dryer, or oven has a small service port that speaks a protocol called **GEA3**. Through this port the appliance constantly publishes its state — door open/closed, cycle name, water temperature, time remaining, error codes — and accepts commands like "start the cycle" or "switch to Heavy Wash".

This project lets you wire a cheap ESP32 board into that port and expose everything to Home Assistant as regular entities (sensors, switches, buttons, etc.). No cloud account, no SmartHQ app, fully local.

Each piece of data on the bus is identified by a 16-bit number called an **ERD** (Entity Reference Designator). For example, `0x2012` might be the door state and `0x321B` the selected wash cycle. You'll see this term throughout the docs — just remember: **ERD = a register the appliance exposes**.

---

## Quick start

The shortest possible config — read the door sensor and the selected wash cycle from a dishwasher:

```yaml
# 1. Pull in the component
external_components:
  - source: github://mguaylam/esphome-gea
    components: [gea]

# 2. Set up the UART (pin numbers depend on your board)
uart:
  id: uart_gea
  tx_pin: GPIO21
  rx_pin: GPIO20
  baud_rate: 230400

# 3. Define the GEA hub
gea:
  id: gea_hub
  uart_id: uart_gea

# 4. Add some entities
binary_sensor:
  - platform: gea
    gea_id: gea_hub
    name: "Door"
    erd: 0x2012
    bitmask: 0x01
    device_class: door

select:
  - platform: gea
    gea_id: gea_hub
    name: "Wash Cycle"
    erd: 0x321B
    options:
      0x00: "AutoSense"
      0x01: "Heavy"
      0x02: "Normal"
```

Flash, plug into your appliance, and the entities appear in Home Assistant. **Don't know which ERDs your appliance uses?** Skip to [ERD Discovery](#erd-discovery) — the component automatically logs every ERD it sees on boot.

> Looking for a complete, working example? See the configs under [`devices/`](devices/).

---

## Table of Contents

- [What is this?](#what-is-this)
- [Quick start](#quick-start)
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
  - [Automation triggers](#automation-triggers)
- [Diagnostics](#diagnostics)
- [ERD Discovery](#erd-discovery)
- [Troubleshooting](#troubleshooting)
- [Testing](#testing)
- [Devices](#devices)
- [License](#license)

---

## Features

This component maps GE appliance ERDs to Home Assistant entities and handles the GEA3 protocol details for you.

- **Full bidirectional control** — read sensor values, toggle switches, change cycle settings, trigger remote start/stop
- **Auto-discovery** — subscribes to all ERDs on boot; unknown registers are logged for reverse-engineering
- **Plug-and-play addressing** — automatically detects appliance bus address from first valid packet
- **Resilient** — periodic re-subscription recovers state after appliance power cycles
- **Flexible decoding** — 13 numeric types (`uint8`, `uint16_be`, `int32_le`, …), raw hex, ASCII strings, enum option maps, plus optional `multiplier`/`offset` for scaled values
- **Multiple entity platforms** — sensor, binary sensor, switch, select, text sensor, button, number
- **Edge-triggered automations** — `on_erd_change` fires on rising/falling/any bitmask transitions
- **Bus health indicator** — `is_bus_connected()` lambda for status LEDs
- **Diagnostic counters** — `get_rx_bytes()`, `get_crc_errors()`, `get_tx_retries()`, `get_dropped_requests()` exposable as template sensors
- **Optional ERD lookup table** — embed the full GE ERD definition set for richer diagnostic logs (+75 KB flash)
- **Native HA integration** — device classes, state classes, diagnostic categories all supported

---

## Hardware

### Bill of Materials

| Part | Notes |
|------|-------|
| [Seeed XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) | Compact, 3.3 V, native USB |
| FirstBuild GEA adapter | 8P8C (RJ45-style) breakout, status LEDs, and supporting components included |

### Wiring

| XIAO ESP32-C3    |   | GE Appliance      |
|------------------|---|-------------------|
| D6 (TX / GPIO21) | → | GEA3 RX           |
| D7 (RX / GPIO20) | ← | GEA3 TX           |
| GND              | ↔ | GND               |
| 3V3              | → | 3.3V (optional)   |

The FirstBuild adapter also connects three status LEDs:

| Pin | Function |
|-----|----------|
| D0 | Heartbeat |
| D1 | Wi-Fi status |
| D2 | Bus status |

> The GEA3 connector on GE appliances is typically a small jack behind the service panel. The FirstBuild breakout board makes it simple to tap into without cutting wires. **No need to open the appliance's main electronics** — the service port is designed for diagnostic access.

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
```

| Option | Default | Description |
|--------|---------|-------------|
| `uart_id` | required | ID of the `uart:` block |
| `src_address` | `0xBB` | Source address on the GEA bus |
| `dest_address` | auto | Appliance address; detected from first packet if not set |
| `erd_lookup` | `false` | If `true`, embed the public GE ERD definition set (~75 KB flash) so discovery logs include each ERD's documented name, type, and decoded value |

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

  # Scaled value: many GE ERDs encode temperature × 10
  - platform: gea
    gea_id: gea_hub
    name: "Water Temperature"
    erd: 0x3035
    decode: uint16_be
    multiplier: 0.1
    offset: 0
    unit_of_measurement: "°C"
    accuracy_decimals: 1
```

**`decode` types:** `uint8`, `uint16_be`, `uint16_le`, `uint32_be`, `uint32_le`, `int8`, `int16_be`, `int16_le`, `int32_be`, `int32_le`, `bool`

| Option | Default | Description |
|--------|---------|-------------|
| `multiplier` | `1.0` | Multiplied with the decoded raw value |
| `offset` | `0.0` | Added after multiplication: `published = raw × multiplier + offset` |

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
    payload_on: 0x01
    payload_off: 0x00
```

| Option | Default | Description |
|--------|---------|-------------|
| `byte_offset` | `0` | Byte index inside the ERD payload to compare against `payload_on`/`payload_off` |
| `payload_on` | `0x01` | Byte value indicating the switch is on |
| `payload_off` | `0x00` | Byte value indicating the switch is off |
| `write_erd` | (same as `erd`) | Use a different ERD for writes when read and write addresses differ |

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
    payload: [0x01]
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

`multiplier` and `offset` are also accepted on numbers; the inverse transform is applied on write so the user-visible scaled value is converted back to the raw bytes the appliance expects.

---

### Automation triggers

#### `on_erd_change`

Fires when a specific bit or byte within an ERD's publication transitions across a configured edge. Defined on the `gea:` hub, not on a platform. Typical use case: appliance event flags (e.g. "wash complete", "detergent low") that the firmware pulses once and expects a consumer to act on.

```yaml
gea:
  id: gea_hub
  uart_id: uart_gea
  on_erd_change:
    - erd: 0x2154
      byte_offset: 1
      bitmask: 0x20
      edge: rising
      then:
        - homeassistant.event:
            event: esphome.washer_notification
            data:
              kind: wash_complete
```

| Option | Default | Description |
|--------|---------|-------------|
| `erd` | required | ERD address to watch |
| `byte_offset` | `0` | Byte index within the ERD payload |
| `bitmask` | `0xFF` | Mask applied to the selected byte |
| `edge` | `rising` | `rising`, `falling`, or `any` |

**Semantics** (where `old` / `new` are the masked values):

| Edge | Fires when |
|------|------------|
| `rising` | `old == 0 && new != 0` |
| `falling` | `old != 0 && new == 0` |
| `any` | `old != new` |

> The first publication of an ERD after boot is a silent baseline — no trigger fires. This prevents spurious events when the ESP reboots mid-cycle.
>
> With a multi-bit mask the masked region is treated as a single aggregated flag ("at least one bit set"). For per-bit detection, define one trigger per bit.

---

<details>
<summary><strong>Protocol Overview</strong></summary>

GEA3 is a full-duplex serial protocol. Each frame has the following structure:

![GEA3 Frame](docs/frame.svg)

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
| Subscription Host Startup | `0xA8` | ← Appliance | Announces appliance just came online |
| ACK | `0xE1` | ↔ Both | Single-byte acknowledgement |

### Typical Exchange

```mermaid
sequenceDiagram
    participant E as ESP32
    participant A as GE Appliance

    Note over E,A: Boot — initial subscription
    E->>A: Subscribe-All 0xA4 (type=0x00)
    A-->>E: ACK 0xE1
    A->>E: Subscribe-All Ack 0xA5
    E-->>A: ACK 0xE1
    A->>E: Publication 0xA6 (all ERDs broadcast)
    E-->>A: Publication Ack 0xA7

    Note over E,A: Read ERD (e.g. water temperature)
    E->>A: Read Request 0xA0 [ERD=0x3035]
    A-->>E: ACK 0xE1
    A->>E: Read Response 0xA1 [value=75]
    E-->>A: ACK 0xE1

    Note over E,A: Write ERD (from Home Assistant)
    E->>A: Write Request 0xA2 [ERD=0x321B, val=0x02]
    A-->>E: ACK 0xE1
    A->>E: Write Response 0xA3
    E-->>A: ACK 0xE1

    Note over E,A: Appliance-initiated change
    A->>E: Publication 0xA6 [ERD=0x2000 updated]
    E-->>A: Publication Ack 0xA7
```

</details>

---

<details>
<summary><strong>Connection lifecycle</strong></summary>

The component uses a two-state subscription machine:

```mermaid
stateDiagram-v2
    direction LR
    [*] --> SUBSCRIBING

    SUBSCRIBING --> SUBSCRIBED : 0xA5 ack (result == 0x00)
    SUBSCRIBED --> SUBSCRIBING : 0xA8 appliance reboot\nor bus reconnect

    state SUBSCRIBING {
        [*] --> idle
        idle --> tx : every 1 s
        tx --> idle : send subscribe-all\ntype=0x00
    }

    state SUBSCRIBED {
        [*] --> active
        active --> keepalive : every 30 s
        keepalive --> active : send subscribe-all\ntype=0x01 (retain)
    }
```

| State | Behaviour |
| ----- | --------- |
| **SUBSCRIBING** | Sends subscribe-all `type=0x00` every **1 s** until the appliance acknowledges |
| **SUBSCRIBED** | Sends subscribe-all `type=0x01` (retain) every **30 s** as a keep-alive |

The keep-alive is required because the appliance silently drops subscriptions after a few minutes even when the bus remains physically connected.

Transition back to SUBSCRIBING happens on two triggers:

- **Primary:** a `0xA8` "subscription host startup" packet received from the appliance — fires immediately when the appliance broadcasts its boot announcement.
- **Fallback:** a bus silent → active transition, detected when `is_bus_connected()` goes from `false` to `true`. Covers cases where the startup packet is missed.

</details>

---

<details>
<summary><strong>Request reliability</strong></summary>

Every outgoing request (read, write, subscribe-all) goes through a single-in-flight queue with deterministic retry:

| Parameter | Value |
|-----------|-------|
| Timeout per attempt | **250 ms** |
| Max retries | **10** |
| Total worst-case | **~2.75 s** before a request is dropped |

- **Serialization** — only one request is on the wire at a time, so `request_id` matches between request and response without ambiguity.
- **Retry on timeout** — if no matching response arrives within 250 ms, the same `request_id` is resent. A late response from a prior attempt still matches.
- **Request-ID matching** — incoming responses whose `request_id` does not match the pending request are ignored; the pending request stays armed until it either matches a response or exhausts its retries.
- **Unsolicited frames bypass the queue** — ACKs, publications, publication ACKs, and subscription host startup packets are not request/response pairs and are processed independently.

Writes initiated from Home Assistant are non-blocking: the entity returns immediately and the queue transmits in the background. A dropped write (10 retries exhausted) is logged at `WARN` level.

</details>

---

## Diagnostics

The hub exposes per-bus counters callable from YAML lambdas. Pair them with `platform: template` sensors and `entity_category: diagnostic` to surface bus health in Home Assistant:

```yaml
sensor:
  - platform: template
    name: "GEA RX Bytes"
    entity_category: diagnostic
    accuracy_decimals: 0
    lambda: 'return id(gea_hub).get_rx_bytes();'
    update_interval: 30s

  - platform: template
    name: "GEA CRC Errors"
    entity_category: diagnostic
    accuracy_decimals: 0
    lambda: 'return id(gea_hub).get_crc_errors();'
    update_interval: 30s

  - platform: template
    name: "GEA TX Retries"
    entity_category: diagnostic
    accuracy_decimals: 0
    lambda: 'return id(gea_hub).get_tx_retries();'
    update_interval: 30s

  - platform: template
    name: "GEA Dropped Requests"
    entity_category: diagnostic
    accuracy_decimals: 0
    lambda: 'return id(gea_hub).get_dropped_requests();'
    update_interval: 30s
```

| Accessor | Increments on |
|----------|---------------|
| `get_rx_bytes()` | every byte received on the UART |
| `get_crc_errors()` | a framed packet whose CRC does not match |
| `get_tx_retries()` | a pending request times out and is resent |
| `get_dropped_requests()` | a pending request exhausts all retries |

A non-zero **CRC errors** counter on a stable bus usually points to a wiring/grounding issue. A growing **dropped requests** counter while the appliance is awake is more concerning and warrants protocol-level inspection.

---

## Testing

Following the ESPHome convention, tests are **compile-time integration tests** that exercise every platform, decode type, and option (multiplier/offset, on_erd_change with each edge, distinct read/write ERDs, diagnostic counters via lambda, etc.). If the schema, codegen, or generated C++ regress, the build breaks.

Shared component config lives in [`tests/components/gea/common.yaml`](tests/components/gea/common.yaml); each target framework has a thin platform wrapper:

| Target | File |
|--------|------|
| ESP32 + ESP-IDF | [`tests/components/gea/test.esp32-idf.yaml`](tests/components/gea/test.esp32-idf.yaml) |
| ESP32 + Arduino | [`tests/components/gea/test.esp32-arduino.yaml`](tests/components/gea/test.esp32-arduino.yaml) |
| ESP8266 | [`tests/components/gea/test.esp8266.yaml`](tests/components/gea/test.esp8266.yaml) |
| RP2040 | [`tests/components/gea/test.rp2040.yaml`](tests/components/gea/test.rp2040.yaml) |

CI compiles all four in parallel on every PR. To reproduce locally:

```bash
esphome compile tests/components/gea/test.esp32-idf.yaml
```

> Note: only **ESP32-C3 (XIAO)** is verified on real hardware. The other platforms are exercised at build-time to catch portability regressions but have not been runtime-tested.

---

## ERD Discovery

Don't know which ERDs your appliance exposes? You don't have to. On boot the component sends a *subscribe-all* command and the appliance dumps every ERD it supports. Anything not matched to a configured entity gets logged at `INFO` level:

```text
[I][gea:042]: Discovered ERD 0x2007: 00 00
[I][gea:042]: Discovered ERD 0x200A: 00 00 00 00
```

**Workflow for a new appliance:**

1. Flash a minimal config with just the `gea:` hub (no entities).
2. Watch the logs (`esphome logs your-device.yaml`) — you'll see ERDs flow in.
3. Operate the appliance (open the door, change cycles, start a wash) and note which values change.
4. Add a `text_sensor` with `decode: raw` for each ERD you want to track:

   ```yaml
   text_sensor:
     - platform: gea
       gea_id: gea_hub
       name: "Unknown 0x3035"
       erd: 0x3035
       decode: raw
   ```

5. Once you've decoded what each byte means, swap the raw text sensor for a proper sensor/binary_sensor/select.

If you'd like richer log output (each ERD's documented name, type, and decoded value), enable the public ERD lookup table:

```yaml
gea:
  id: gea_hub
  uart_id: uart_gea
  erd_lookup: true   # +75 KB flash, but logs become self-documenting
```

> The [erd-definitions](https://github.com/GEAppliances/erd-definitions) submodule provides ~3000 documented public ERDs — useful for naming things you see on the bus.

---

## Troubleshooting

**Nothing happens, no logs from the GEA component**

- Confirm `baud_rate: 230400` (not 19,200 — that's the older GEA2 protocol).
- Check TX/RX aren't swapped. The ESP's TX goes to the appliance's RX and vice versa.
- Verify GND is connected between the ESP and the appliance.
- Check `is_bus_connected()` in a binary_sensor — `false` means no valid packet has arrived in the last 30 s.

**Entities created but always show "unknown"**

- The ERD address is probably wrong for your model. Run a discovery pass (see above) and grep the logs for activity when you trigger that state on the appliance.
- For binary sensors, double-check the `byte_offset` and `bitmask` — the door bit might live in byte 1 of a multi-byte ERD, not byte 0.

**Switch / select / number writes don't take effect**

- Some ERDs are read-only at the appliance side; the write goes through but the appliance ignores it. There's no programmatic way to know in advance — try operating the appliance manually and note which ERD changes, then write to that ERD.
- Some appliances expect writes to a *different* ERD than the read ERD. Use `write_erd:` to specify it.

**Bus shows occasional CRC errors**

- Small numbers (a few per hour) are normal on a long cable run.
- A growing count usually points to a wiring/grounding issue. See the `get_crc_errors()` counter under [Diagnostics](#diagnostics).

**"Delayed start" wakes up the appliance unexpectedly**

- Don't expose washer ERD `0x2038` (or equivalent on other appliances) as a writable entity — writing to it brings the machine out of sleep mode whether you wanted to or not. Read-only is fine.

Still stuck? [Open an issue](https://github.com/mguaylam/esphome-gea/issues) with your YAML and a snippet of the ESPHome logs (`esphome logs your-device.yaml`).

---

## Devices

Complete configurations for supported appliances:

| File | Appliance | Highlights |
|------|-----------|------------|
| [`PDP715SYV0FS.yaml`](devices/dishwasher/PDP715SYV0FS.yaml) | GE PDP715SYV0FS | Cycle selection, elapsed time, ASCII cycle name, model retrieval button |
| [`PFQ97HSPVDS.yaml`](devices/washer/PFQ97HSPVDS.yaml) | GE PFQ97HSPVDS (Ultrafast Combo) | Time remaining, door/lock/pump sensors, 200+ cycle options, remote start/stop, detergent/softener dosing |

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
