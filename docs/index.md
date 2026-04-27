# esphome-gea Documentation

The `gea` external component talks to GE appliances over the **GEA3 serial bus**
and exposes their state to Home Assistant as standard ESPHome entities.

If this is your first time, start with the [Quick start](../README.md#quick-start)
in the project README, then come back here for the full configuration reference.

## Hub

- [`gea`](hub.md) — the main hub component (UART, addressing, ERD discovery).

## Entities

| Platform | Direction | Use for |
|----------|-----------|---------|
| [`sensor`](sensor.md) | read | Numeric values (temperature, time elapsed, error codes) |
| [`binary_sensor`](binary_sensor.md) | read | Door open/closed, lock state, single bits |
| [`switch`](switch.md) | read + write | Toggleable settings (e.g. extra dry, sanitize) |
| [`select`](select.md) | read + write | Cycle selection, mode pickers |
| [`number`](number.md) | read + write | Numeric setpoints |
| [`text_sensor`](text_sensor.md) | read | ASCII strings, hex dumps, enum labels |
| [`button`](button.md) | write | Fire-and-forget commands (remote start, retrieve model) |

## Automations

- [`on_erd_change`](automation.md) — fire on rising/falling/any bitmask transitions.

## Operating the bus

- [ERD Discovery](erd-discovery.md) — how to find ERDs on a new appliance.
- [Diagnostics](diagnostics.md) — bus health counters and helper lambdas.
- [Troubleshooting](troubleshooting.md) — common pitfalls and how to fix them.

## Reference

- [Protocol & internals](protocol.md) — GEA3 framing, command codes, connection lifecycle, request reliability.
- [Testing](testing.md) — multi-platform compile-time integration tests.

## Hardware adapters

The component is hardware-agnostic — any TTL-level UART breakout to the
appliance's GEA3 jack works. Two known-good options:

- **[mulcmu/esphome-ge-laundry-uart](https://github.com/mulcmu/esphome-ge-laundry-uart)**
  — open hardware (KiCad sources + gerbers). Recommended if you're comfortable
  ordering PCBs and soldering. The original reverse-engineering of the GEA3
  protocol on ESPHome was done by [@mulcmu](https://github.com/mulcmu); this
  project builds on that foundation.
- **[FirstBuild GEA adapter](https://firstbuild.com/)** — commercial 8P8C
  breakout with status LEDs, ready to use out of the box.
