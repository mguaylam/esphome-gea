# GEA Select

The `gea` select platform maps a numeric ERD value to a list of human-readable
labels. Reads decode the bytes and look them up in your `options` map; writes
do the reverse.

```yaml
# Example configuration entry
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

## Configuration variables

- **gea_id** (*Required*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  The ID of the parent [`gea`](hub.md) hub.
- **erd** (*Required*, hex 16-bit): The ERD address to read.
- **options** (*Required*, mapping): Map of numeric values to label strings.
  Keys may be specified as decimal or hex (e.g. `0x04` or `4`). Duplicate keys
  are rejected at compile time.
- **write_erd** (*Optional*, hex 16-bit): A different ERD to write to. Defaults
  to the same value as `erd`.
- **decode** (*Optional*, string): How to decode the option value bytes.
  Defaults to `uint8`. See [Sensor decode types](sensor.md#configuration-variables).
- **byte_offset** (*Optional*, integer): Byte index inside the payload.
  Defaults to `0`.
- **data_size** (*Optional*, integer): Override the number of bytes to write.
  By default, derived from `decode` (1 for `uint8`, 2 for `uint16_*`, 4 for `uint32_*`).
- All other options from [Select](https://esphome.io/components/select/index.html#config-select).

## See also

- [Text Sensor](text_sensor.md) — for read-only enum mappings.
- [Hub configuration](hub.md)
