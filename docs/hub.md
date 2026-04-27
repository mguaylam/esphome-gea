# GEA Hub

The `gea:` block defines the communication hub that handles UART framing,
addressing, ERD discovery, and request retries. Every entity platform you
configure references it via `gea_id`.

```yaml
# Example minimal configuration
uart:
  id: uart_gea
  tx_pin: GPIO21
  rx_pin: GPIO20
  baud_rate: 230400

gea:
  id: gea_hub
  uart_id: uart_gea
```

```yaml
# Example with explicit addressing and the public ERD lookup table
gea:
  id: gea_hub
  uart_id: uart_gea
  src_address: 0xE4
  dest_address: 0xC0
  erd_lookup: true
```

## Configuration variables

- **id** (*Optional*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  Manually specify the ID used for code generation. Required if you have multiple hubs.
- **uart_id** (*Required*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  ID of the `uart:` bus. Must be configured for `230400` baud, 8N1.
- **src_address** (*Optional*, hex byte): The address this device claims on the GEA bus.
  Defaults to `0xBB`.
- **dest_address** (*Optional*, hex byte): The address of the appliance.
  If omitted, the hub auto-detects it from the first valid packet seen on the bus.
- **erd_lookup** (*Optional*, boolean): Embed the public GE ERD definition set
  (~75 KB flash) so discovery logs include each ERD's documented name, type,
  and decoded value. Defaults to `false`.
- **on_erd_change** (*Optional*, [Automation](automation.md)):
  Fires when a specific bit/byte within an ERD's publication transitions.
  See [Automations](automation.md).

## UART configuration

The GEA3 protocol uses **230,400 baud, 8N1**. (19,200 baud is GEA2, an older
protocol, and is not supported.)

```yaml
uart:
  id: uart_gea
  tx_pin: GPIO21
  rx_pin: GPIO20
  baud_rate: 230400
  # data_bits: 8     # default
  # stop_bits: 1     # default
  # parity: NONE     # default
```

## See also

- [ERD Discovery](erd-discovery.md)
- [Diagnostics](diagnostics.md)
- [`uart` component (esphome.io)](https://esphome.io/components/uart)
