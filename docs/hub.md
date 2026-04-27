# GEA Hub

The `gea:` block defines the communication hub that handles UART framing,
addressing, ERD discovery, and request retries. Every entity platform you
configure references it via `gea_id`.

```yaml
# GEA3 — minimal configuration (newer appliances, 230400 baud, full-duplex)
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
# GEA3 — explicit addressing and public ERD lookup table embedded
gea:
  id: gea_hub
  uart_id: uart_gea
  src_address: 0xE4
  dest_address: 0xC0
  erd_lookup: true
```

```yaml
# GEA2 — older appliances (19200 baud, half-duplex, polled)
uart:
  id: uart_gea
  tx_pin: GPIO21
  rx_pin: GPIO20
  baud_rate: 19200

gea:
  id: gea_hub
  uart_id: uart_gea
  protocol: gea2
  dest_address: 0xC0     # required — auto-detect not possible on GEA2
  poll_interval: 2s      # how often to refresh each declared ERD
```

## Configuration variables

- **id** (*Optional*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  Manually specify the ID used for code generation. Required if you have multiple hubs.
- **uart_id** (*Required*, [ID](https://esphome.io/guides/configuration-types#config-id)):
  ID of the `uart:` bus. Baud rate must match the protocol — 230400 for GEA3,
  19200 for GEA2.
- **protocol** (*Optional*, string): Either `gea3` (default) or `gea2`. Pick the
  one your appliance speaks. See [protocol.md](protocol.md) for the wire-level
  differences.
- **src_address** (*Optional*, hex byte): The address this device claims on the
  GEA bus. Defaults to `0xBB`.
- **dest_address** (*Optional* on GEA3 / *Required* on GEA2, hex byte):
  The address of the appliance. On GEA3 the hub can auto-detect it from the
  first valid packet; GEA2 has no spontaneous traffic so this must be set
  explicitly. Typical value: `0xC0`.
- **poll_interval** (*Optional*, time — GEA2 only): How long the round-robin
  poller waits between successive ERD reads. Defaults to `2s`. Smaller values
  refresh state faster but increase bus load. Ignored on GEA3.
- **erd_lookup** (*Optional*, boolean): Embed the public GE ERD definition set
  (~75 KB flash) so discovery logs include each ERD's documented name, type,
  and decoded value. Defaults to `false`.
- **on_erd_change** (*Optional*, [Automation](automation.md)):
  Fires when a specific bit/byte within an ERD's value transitions.
  See [Automations](automation.md). On GEA2, the watched ERD is automatically
  added to the poll list.

## UART configuration

| Protocol | Baud   | Framing | Notes |
|----------|--------|---------|-------|
| `gea3`   | 230400 | 8N1     | Full-duplex. Default. |
| `gea2`   | 19200  | 8N1     | Half-duplex shared bus — the carrier board (mulcmu/Firstbuild) handles direction switching. |

```yaml
# GEA3
uart:
  id: uart_gea
  tx_pin: GPIO21
  rx_pin: GPIO20
  baud_rate: 230400

# GEA2
uart:
  id: uart_gea
  tx_pin: GPIO21
  rx_pin: GPIO20
  baud_rate: 19200
```

## GEA2 vs GEA3 — what changes for you?

| Behaviour              | GEA3                                | GEA2                                  |
|------------------------|-------------------------------------|---------------------------------------|
| Discovery              | Subscribes to all ERDs at boot      | Reads only ERDs you declare           |
| Refresh model          | Appliance pushes on change          | Hub polls round-robin                 |
| `dest_address`         | Optional (auto-detected)            | Required                              |
| `poll_interval`        | Ignored                             | Default 2 s, tuneable                 |
| Bus speed              | 230400 baud                         | 19200 baud                            |

Everything else — entities, automations, diagnostics, `erd_lookup` — works the
same on both protocols.

## See also

- [Protocol internals](protocol.md)
- [ERD Discovery](erd-discovery.md)
- [Diagnostics](diagnostics.md)
- [`uart` component (esphome.io)](https://esphome.io/components/uart)
