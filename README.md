# esphome-gea

[![Compile](https://github.com/mguaylam/esphome-gea/actions/workflows/compile.yml/badge.svg)](https://github.com/mguaylam/esphome-gea/actions/workflows/compile.yml)
[![Test](https://github.com/mguaylam/esphome-gea/actions/workflows/test.yml/badge.svg)](https://github.com/mguaylam/esphome-gea/actions/workflows/test.yml)
[![Lint](https://github.com/mguaylam/esphome-gea/actions/workflows/lint.yml/badge.svg)](https://github.com/mguaylam/esphome-gea/actions/workflows/lint.yml)
[![ESPHome](https://img.shields.io/badge/ESPHome-external%20component-blue?logo=esphome)](https://esphome.io/components/external_components)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-compatible-41bdf5?logo=home-assistant)](https://www.home-assistant.io/)
[![ESP32](https://img.shields.io/badge/ESP32-supported-red?logo=espressif)](https://www.espressif.com/)

An [ESPHome](https://esphome.io/) external component for monitoring and controlling GE appliances via the **GEA2 or GEA3 serial bus**, with native [Home Assistant](https://www.home-assistant.io/) integration.

---

## What is this?

Your GE dishwasher, washer, dryer, or oven has a small service port that speaks a protocol called **GEA3** (newer appliances) or **GEA2** (older ones). Through this port the appliance exposes its state — door open/closed, cycle name, water temperature, time remaining, error codes — and accepts commands like "start the cycle" or "switch to Heavy Wash".

This project lets you wire a cheap ESP32 board into that port and expose everything to Home Assistant as regular entities (sensors, switches, buttons, etc.). No cloud account, no SmartHQ app, fully local.

Each piece of data on the bus is identified by a 16-bit number called an **ERD** (Entity Reference Designator). For example, `0x2012` might be the door state and `0x321B` the selected wash cycle. You'll see this term throughout the docs — just remember: **ERD = a register the appliance exposes**.

---

## Quick start

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

Flash, plug into your appliance, and the entities appear in Home Assistant. **Don't know which ERDs your appliance uses?** Read the [ERD Discovery guide](docs/erd-discovery.md) — the component automatically logs every ERD it sees on boot.

> Looking for a complete, working example? See the configs under [`devices/`](devices/).

---

## Hardware

| Part | Notes |
|------|-------|
| [Seeed XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) | Compact, 3.3 V, native USB |
| GEA adapter board | See **Adapter options** below |

### Adapter options

You need a small adapter to break out the appliance's GEA3 jack to the ESP. Two
good options:

- **[mulcmu/esphome-ge-laundry-uart](https://github.com/mulcmu/esphome-ge-laundry-uart)** — *recommended for tinkerers.*
  Fully open hardware with KiCad sources and gerbers. Order the PCB from any
  fab house and solder it yourself. Big thanks to [@mulcmu](https://github.com/mulcmu)
  whose reverse-engineering work made this whole ecosystem possible.
- **[FirstBuild GEA adapter](https://firstbuild.com/)** — *recommended if you
  prefer not to solder.* Commercial 8P8C breakout with status LEDs and all
  supporting components.

Both expose the same TX/RX/GND pinout to the ESP32 — the wiring below applies
to either one.

### Wiring

| XIAO ESP32-C3    |   | GE Appliance      |
|------------------|---|-------------------|
| D6 (TX / GPIO21) | → | GEA3 RX           |
| D7 (RX / GPIO20) | ← | GEA3 TX           |
| GND              | ↔ | GND               |
| 3V3              | → | 3.3V (optional)   |

> The GEA3 connector is typically a small jack behind the appliance's service panel. The FirstBuild breakout makes tapping into it straightforward — **no need to open the main electronics**.

UART settings:
- **GEA3 (default)**: 230,400 baud, 8N1, full-duplex. Use the XIAO's hardware UART pins: TX = GPIO21, RX = GPIO20.
- **GEA2 (older appliances)**: 19,200 baud, 8N1, half-duplex. Pin numbers depend on your adapter — see [docs/hub.md](docs/hub.md) for the adapter-specific wiring table. Set `protocol: gea2` and `dest_address` on the hub.

---

## Features

- **GEA2 and GEA3 protocols** — same component, configurable per appliance.
- **Read & write** — sensors, switches, selects, numbers, buttons, text sensors, binary sensors.
- **Auto-discovery** — every ERD on the bus is logged on boot for easy reverse-engineering (GEA3; GEA2 polls only declared ERDs).
- **Plug-and-play addressing** — GEA3 appliance bus address auto-detected (GEA2 requires `dest_address`).
- **Resilient** — periodic re-subscription (GEA3) or round-robin polling (GEA2) recovers state after appliance power cycles.
- **Flexible decoding** — 13 numeric types, raw hex, ASCII, enum option maps, scaling via `multiplier`/`offset`.
- **Edge-triggered automations** — `on_erd_change` fires on rising/falling/any bitmask transitions.
- **Bus health** — diagnostic counters and `is_bus_connected()` lambda for status LEDs.
- **Optional ERD lookup table** — embed the public GE ERD definition set for self-documenting logs.

---

## Documentation

| Topic | |
|-------|---|
| Hub configuration | [docs/hub.md](docs/hub.md) |
| Sensor (read) | [docs/sensor.md](docs/sensor.md) |
| Binary Sensor (read) | [docs/binary_sensor.md](docs/binary_sensor.md) |
| Switch (read+write) | [docs/switch.md](docs/switch.md) |
| Select (read+write) | [docs/select.md](docs/select.md) |
| Number (read+write) | [docs/number.md](docs/number.md) |
| Text Sensor (read) | [docs/text_sensor.md](docs/text_sensor.md) |
| Button (write) | [docs/button.md](docs/button.md) |
| `on_erd_change` automation | [docs/automation.md](docs/automation.md) |
| ERD discovery workflow | [docs/erd-discovery.md](docs/erd-discovery.md) |
| Bus diagnostics | [docs/diagnostics.md](docs/diagnostics.md) |
| Troubleshooting | [docs/troubleshooting.md](docs/troubleshooting.md) |
| GEA2 / GEA3 protocols & internals | [docs/protocol.md](docs/protocol.md) |
| Testing | [docs/testing.md](docs/testing.md) |

Or jump in via the [documentation index](docs/index.md).

---

## Supported devices

Complete configurations for known appliances:

| File | Appliance | Highlights |
|------|-----------|------------|
| [`PDP715SYV0FS.yaml`](devices/dishwasher/PDP715SYV0FS.yaml) | GE PDP715SYV0FS dishwasher | Cycle selection, elapsed time, ASCII cycle name, model retrieval |
| [`PFQ97HSPVDS.yaml`](devices/washer/PFQ97HSPVDS.yaml) | GE PFQ97HSPVDS Ultrafast Combo | Time remaining, door/lock/pump sensors, 200+ cycle options, remote start/stop, dosing |

Got a working config for another appliance? PRs welcome.

---

## License

MIT — see [LICENSE](LICENSE).
