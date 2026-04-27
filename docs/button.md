# GEA Button

The `gea` button platform sends a fixed payload to a write-only ERD when
pressed. There's no state feedback — it's fire-and-forget.

```yaml
# Example configuration entry
button:
  - platform: gea
    gea_id: gea_hub
    name: "Remote Start"
    erd: 0x2149
    payload: [0x01]

  - platform: gea
    gea_id: gea_hub
    name: "Retrieve Model"
    erd: 0x0008
    payload: [0x00, 0x00]
```

## Configuration variables

- **gea_id** (*Required*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  The ID of the parent [`gea`](hub.md) hub.
- **erd** (*Required*, hex 16-bit): The ERD address to write to.
- **payload** (*Required*, list of bytes): Bytes to send on each press.
- All other options from [Button](https://esphome.io/components/button/index.html#config-button).

## See also

- [Hub configuration](hub.md)
