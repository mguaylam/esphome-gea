# GEA Number

The `gea` number platform exposes a numeric ERD as a read+write value with
min/max/step validation. `multiplier` and `offset` apply on read and are
inverted on write — so the wire value matches what the appliance expects.

```yaml
# Example configuration entry
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

  # Scaled value (e.g. temperature in tenths of a °C)
  - platform: gea
    gea_id: gea_hub
    name: "Setpoint"
    erd: 0xD005
    decode: uint16_be
    multiplier: 0.1
    min_value: 0
    max_value: 100
    step: 0.1
    unit_of_measurement: "°C"
```

## Configuration variables

- **gea_id** (*Required*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  The ID of the parent [`gea`](hub.md) hub.
- **erd** (*Required*, hex 16-bit): The ERD address to read.
- **write_erd** (*Optional*, hex 16-bit): A different ERD to write to. Defaults
  to the same value as `erd`.
- **decode** (*Optional*, string): Same options as [`sensor`](sensor.md).
  Defaults to `uint8`.
- **byte_offset** (*Optional*, integer): Byte index inside the payload.
  Defaults to `0`.
- **multiplier** (*Optional*, float): Multiplied with the decoded raw value on
  read. The inverse is applied on write. Must be non-zero. Defaults to `1.0`.
- **offset** (*Optional*, float): Added on read; subtracted on write.
  Defaults to `0.0`.
- **min_value** / **max_value** / **step**: Standard
  [Number](https://esphome.io/components/number/index.html#config-number) bounds.

## Behaviour

When the user changes the value in Home Assistant, the component:

1. Inverts the scaling: `raw = (value − offset) / multiplier`.
2. Clamps `raw` to `0` if the result is negative and `decode` is unsigned
   (a warning is logged).
3. Encodes `raw` to the configured number of bytes in the right endianness.
4. Sends a write request to `write_erd`.

## See also

- [Sensor](sensor.md) — for read-only numeric values.
- [Hub configuration](hub.md)
