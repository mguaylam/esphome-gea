# GEA Binary Sensor

The `gea` binary sensor platform publishes a single bit (or whole byte) of an
ERD as a boolean.

```yaml
# Example configuration entry
binary_sensor:
  - platform: gea
    gea_id: gea_hub
    name: "Door"
    erd: 0x2012
    bitmask: 0x01
    device_class: door

  # Inverted bit on a different byte of the same ERD
  - platform: gea
    gea_id: gea_hub
    name: "Lock OK"
    erd: 0x2012
    byte_offset: 1
    bitmask: 0x02
    inverted: true
```

## Splitting a byte of flags into separate sensors

When an ERD packs several independent flags into one byte, expose each bit as
its own sensor by giving each a different `bitmask` of `1 << bit`. For example,
a door-alarm ERD where bit 0 is the fresh-food door, bit 1 the freezer door,
and so on:

```yaml
binary_sensor:
  - platform: gea
    gea_id: gea_hub
    name: "Fresh Food Door Alarm"
    erd: 0x1006
    bitmask: 0x01        # bit 0

  - platform: gea
    gea_id: gea_hub
    name: "Freezer Door Alarm"
    erd: 0x1006
    bitmask: 0x02        # bit 1

  - platform: gea
    gea_id: gea_hub
    name: "Convertible Drawer Alarm"
    erd: 0x1006
    bitmask: 0x04        # bit 2

  - platform: gea
    gea_id: gea_hub
    name: "Door-In-Door Alarm"
    erd: 0x1006
    bitmask: 0x08        # bit 3
```

Combine with `byte_offset` when the flags live in a later byte of a multi-byte
ERD.

## Configuration variables

- **gea_id** (*Required*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  The ID of the parent [`gea`](hub.md) hub.
- **erd** (*Required*, hex 16-bit): The ERD address to watch.
- **byte_offset** (*Optional*, integer): Byte index inside the ERD payload.
  Defaults to `0`.
- **bitmask** (*Optional*, hex byte): Bit mask applied after `byte_offset`
  extraction. The sensor publishes `true` if any masked bit is set.
  Defaults to `0xFF` (any non-zero byte).
- **inverted** (*Optional*, boolean): Invert the boolean result. Defaults to `false`.
- All other options from [Binary Sensor](https://esphome.io/components/binary_sensor/index.html#config-binary-sensor).

## See also

- [`on_erd_change` automation](automation.md) — for edge-triggered actions instead of state.
- [Hub configuration](hub.md)
