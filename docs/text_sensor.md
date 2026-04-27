# GEA Text Sensor

The `gea` text sensor platform decodes an ERD as a string. It supports three
modes depending on the `decode` option you pick.

```yaml
# Example configuration entry
text_sensor:
  # ASCII string (null-terminated)
  - platform: gea
    gea_id: gea_hub
    name: "Cycle Name"
    erd: 0x301C
    decode: ascii

  # Raw hex bytes — handy during ERD reverse-engineering
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

## Configuration variables

- **gea_id** (*Required*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  The ID of the parent [`gea`](hub.md) hub.
- **erd** (*Required*, hex 16-bit): The ERD address to read.
- **decode** (*Optional*, string): How to interpret the bytes:
  - `ascii` — null-trimmed ASCII string.
  - `raw` — hex string like `0x0100AABB` (good for discovery).
  - One of the numeric decode types (e.g. `uint8`) when paired with `options`.
  Defaults to `raw`.
- **options** (*Optional*, mapping): Numeric-to-label map (same syntax as
  [`select`](select.md)). When set, the decoded number is looked up in this
  map. Unknown values are published as `0xNN`.
- **byte_offset** (*Optional*, integer): Byte index inside the payload.
  Defaults to `0`.
- All other options from [Text Sensor](https://esphome.io/components/text_sensor/index.html#config-text-sensor).

## See also

- [Select](select.md) — for read+write enum mapping.
- [ERD Discovery](erd-discovery.md) — `decode: raw` is the right tool while reverse-engineering.
- [Hub configuration](hub.md)
