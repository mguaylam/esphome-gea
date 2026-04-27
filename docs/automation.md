# Automations

## `on_erd_change` Trigger

This automation fires when a specific bit or byte within an ERD's publication
transitions across a configured edge. It's defined on the `gea:` hub itself,
not on a platform.

The typical use case is **appliance event flags** — bits the firmware pulses
once to signal something happened (wash complete, detergent low, filter dirty).
A binary sensor would re-publish the state every time the appliance broadcasts
the ERD; an `on_erd_change` trigger fires only on the edge.

```yaml
# Example configuration entry
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

## Configuration variables

- **erd** (*Required*, hex 16-bit): The ERD to watch.
- **byte_offset** (*Optional*, integer): Byte index inside the ERD payload.
  Defaults to `0`.
- **bitmask** (*Optional*, hex byte): Mask applied to the selected byte.
  Defaults to `0xFF`.
- **edge** (*Optional*, string): When to fire — `rising`, `falling`, or `any`.
  Defaults to `rising`.
- **then** (*Required*, [Action](https://esphome.io/automations/actions)): The
  action(s) to run when the trigger fires.

## Edge semantics

Where `old` and `new` are the masked values from successive publications:

| Edge | Fires when |
|------|------------|
| `rising` | `old == 0 && new != 0` |
| `falling` | `old != 0 && new == 0` |
| `any` | `old != new` |

> **Silent baseline:** the first publication of an ERD after boot does not
> fire any trigger. This avoids spurious events when the ESP reboots while
> a flag is already set.

> **Multi-bit masks:** a `bitmask` covering multiple bits is treated as a single
> aggregated flag ("at least one bit set"). For per-bit detection, define one
> trigger per bit.

## See also

- [Binary Sensor](binary_sensor.md) — for the *current state* of a bit, not
  the transition.
- [Hub configuration](hub.md)
