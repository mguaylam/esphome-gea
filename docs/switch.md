# GEA Switch

The `gea` switch platform reads a boolean ERD and writes back to it on toggle.

```yaml
# Example configuration entry
switch:
  - platform: gea
    gea_id: gea_hub
    name: "Extra Dry"
    erd: 0x3230
    byte_offset: 2
    payload_on: 0x01
    payload_off: 0x00

  # Read and write addresses differ on some appliances
  - platform: gea
    gea_id: gea_hub
    name: "Sanitize"
    erd: 0x3231          # read this ERD
    write_erd: 0x3232    # but write to this one
```

## Configuration variables

- **gea_id** (*Required*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  The ID of the parent [`gea`](hub.md) hub.
- **erd** (*Required*, hex 16-bit): The ERD address to read.
- **write_erd** (*Optional*, hex 16-bit): A different ERD to write to. Defaults to
  the same value as `erd`.
- **byte_offset** (*Optional*, integer): Byte index inside the ERD payload to
  compare against `payload_on`/`payload_off`. Defaults to `0`.
- **payload_on** (*Optional*, hex byte): Byte value indicating the switch is on.
  Defaults to `0x01`.
- **payload_off** (*Optional*, hex byte): Byte value indicating the switch is off.
  Defaults to `0x00`.
- All other options from [Switch](https://esphome.io/components/switch/index.html#config-switch).

## See also

- [Hub configuration](hub.md)
