# GEA Sensor

The `gea` sensor platform publishes a numeric ERD value as a floating-point
state, with optional scaling.

```yaml
# Example configuration entry
sensor:
  - platform: gea
    gea_id: gea_hub
    name: "Elapsed Time"
    erd: 0x0702
    decode: uint32_be
    unit_of_measurement: s
    state_class: total_increasing

  # Many GE ERDs encode temperature × 10 — apply a multiplier to recover the real value.
  - platform: gea
    gea_id: gea_hub
    name: "Water Temperature"
    erd: 0x3035
    decode: uint16_be
    multiplier: 0.1
    unit_of_measurement: "°C"
    accuracy_decimals: 1
```

## Configuration variables

- **gea_id** (*Required*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  The ID of the parent [`gea`](hub.md) hub.
- **erd** (*Required*, hex 16-bit): The ERD address to read.
- **decode** (*Optional*, string): How to interpret the raw bytes. Defaults to `uint16_be`.
  Accepted values:
  - Unsigned: `uint8`, `uint16_be`, `uint16_le`, `uint32_be`, `uint32_le`
  - Signed: `int8`, `int16_be`, `int16_le`, `int32_be`, `int32_le`
  - `bool` — masked bit (`bitmask` selects which bit), returns 0.0 or 1.0
- **byte_offset** (*Optional*, integer): Byte index inside the ERD payload to start
  decoding from. Defaults to `0`.
- **multiplier** (*Optional*, float): Multiplied with the decoded raw value.
  Must be non-zero. Defaults to `1.0`.
- **offset** (*Optional*, float): Added after multiplication. Defaults to `0.0`.
  Final value: `published = raw × multiplier + offset`.
- All other options from [Sensor](https://esphome.io/components/sensor/index.html#config-sensor).

## See also

- [Number](number.md) — for read+write numeric values.
- [Hub configuration](hub.md)
