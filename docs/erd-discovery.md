# ERD Discovery

An **ERD** (Entity Reference Designator) is a 16-bit identifier for a data
register on the appliance — a temperature, machine state, or cycle selection.
You don't have to guess them: on boot the component sends a *subscribe-all*
command and the appliance dumps every ERD it supports.

Anything not matched to a configured entity is logged at `INFO` level:

```text
[I][gea:042]: Discovered ERD 0x2007: 00 00
[I][gea:042]: Discovered ERD 0x200A: 00 00 00 00
```

## Workflow for a new appliance

1. **Flash a minimal config** with just the `gea:` hub and no entities:

   ```yaml
   uart:
     id: uart_gea
     tx_pin: GPIO21
     rx_pin: GPIO20
     baud_rate: 230400

   gea:
     id: gea_hub
     uart_id: uart_gea
   ```

2. **Watch the logs** with `esphome logs your-device.yaml`. ERDs flow in
   immediately after boot.
3. **Operate the appliance** — open the door, change cycles, start a wash —
   and note which ERD values change in response.
4. **Add raw text sensors** for each ERD you want to track:

   ```yaml
   text_sensor:
     - platform: gea
       gea_id: gea_hub
       name: "Unknown 0x3035"
       erd: 0x3035
       decode: raw
   ```

5. **Refine** — once you've decoded what each byte means, replace the raw text
   sensor with a typed [`sensor`](sensor.md), [`binary_sensor`](binary_sensor.md),
   or [`select`](select.md).

## Richer log output

To make discovery logs self-documenting, enable the public ERD lookup table:

```yaml
gea:
  id: gea_hub
  uart_id: uart_gea
  erd_lookup: true   # +75 KB flash
```

The hub will then print each ERD's documented name, type, and decoded value
when it appears.

The data comes from the [erd-definitions](https://github.com/GEAppliances/erd-definitions)
submodule and covers ~3000 public ERDs.

## See also

- [Text Sensor](text_sensor.md) — for `decode: raw` during reverse-engineering.
- [Diagnostics](diagnostics.md)
- [Troubleshooting](troubleshooting.md)
