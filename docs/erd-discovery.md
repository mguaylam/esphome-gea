# ERD Discovery

An **ERD** (Entity Reference Designator) is a 16-bit identifier for a data
register on the appliance — a temperature, machine state, or cycle selection.

> **GEA3** appliances respond to a *subscribe-all* command on boot and dump
> every ERD they support automatically. **GEA2** appliances only answer when
> polled, but you can enable `gea2_discovery: true` to scan all 2 444 known
> ERDs on first boot and persist the responsive ones — see the
> [GEA2 discovery workflow](#gea2-discovery-workflow) below.

On GEA3, anything not matched to a configured entity is logged at `INFO`
level:

```text
[I][gea:042]: Discovered ERD 0x2007: 00 00
[I][gea:042]: Discovered ERD 0x200A: 00 00 00 00
```

## Workflow for a new GEA3 appliance

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

## GEA2 discovery workflow

Enable `gea2_discovery: true` on the hub to let the component discover which
ERDs your appliance supports automatically:

```yaml
uart:
  id: uart_gea
  tx_pin: GPIO9
  rx_pin: GPIO10
  baud_rate: 19200

gea:
  id: gea_hub
  uart_id: uart_gea
  protocol: gea2
  dest_address: 0xC0
  gea2_discovery: true
```

**What happens:**

1. **First boot** — the hub polls all 2 444 known GEA2 ERDs one by one
   (single attempt per ERD, 250 ms timeout). Takes roughly 20–30 minutes.
   Progress is saved to flash every 100 ERDs, so a reboot mid-scan resumes
   where it left off.
2. **Scan complete** — results are persisted to NVS. The log prints every
   ERD that responded (with its GE-documented name if available).
3. **Subsequent boots** — the saved list is loaded instantly; no scan is
   repeated.

> **Force a re-scan** by flashing with `esphome clean` to wipe NVS, or by
> temporarily disabling `gea2_discovery` and reflashing (which resets the
> stored state on the next boot with `gea2_discovery: true`).

The 2 444-address scan list is the union of:
- The [GE public ERD definitions](https://github.com/GEAppliances/erd-definitions) (~2 185 documented ERDs)
- 259 additional addresses discovered empirically by the community on real GEA2 hardware

**View results at any time** — call `log_erds()` from an automation to
reprint the list in the console, for example on every HA connection:

```yaml
api:
  on_client_connected:
    - lambda: 'id(gea_hub).log_erds();'
```

While scanning, `log_erds()` reports progress. Once done, it prints the full
list of responsive ERDs.

**Enrich with names** — pair `erd_lookup: true` on the hub to show the
GE-documented name next to each address:

```yaml
gea:
  id: gea_hub
  uart_id: uart_gea
  protocol: gea2
  dest_address: 0xC0
  gea2_discovery: true
  erd_lookup: true
```

## See also

- [Text Sensor](text_sensor.md) — for `decode: raw` during reverse-engineering.
- [Diagnostics](diagnostics.md)
- [Troubleshooting](troubleshooting.md)
