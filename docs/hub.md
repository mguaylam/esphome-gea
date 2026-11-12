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
# Pin numbers depend on your adapter board — see the UART section below.
uart:
  id: uart_gea
  tx_pin: GPIO9   # FirstBuild adapter
  rx_pin: GPIO10
  baud_rate: 19200

gea:
  id: gea_hub
  uart_id: uart_gea
  protocol: gea2
  dest_address: 0xC0     # optional — omit to let the hub discover it at boot
  poll_interval: 2s      # default refresh interval for each polled ERD
  poll_overrides:        # optional — give specific ERDs their own cadence
    - erd: 0x2038
      interval: 200ms    # fast-changing — refresh often
    - erd: 0x3001
      interval: 30s      # slow setpoint — refresh rarely
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
- **dest_address** (*Optional*, hex byte): The address of the appliance.
  - On **GEA3** the hub auto-detects it from the first valid packet.
  - On **GEA2** there is no spontaneous traffic, so if you omit `dest_address`
    the hub *actively discovers* it at boot: it broadcasts a read of a universal
    identity ERD (`0x0001`, model number) and adopts the address of whichever
    node replies, logging it at `INFO`. If **exactly one** appliance answers it
    is adopted automatically; if **several** answer (a multi-node bus) the hub
    halts and asks you to pick one. Pinning `dest_address: 0xC0` explicitly skips
    this probe on every boot — recommended once you know the value. Typical
    value: `0xC0`.
- **poll_interval** (*Optional*, time — GEA2 only): The default refresh interval
  applied to each polled ERD. Defaults to `2s`. Polling is per ERD — one read
  serves every entity bound to that ERD — and each ERD is refreshed on its own
  schedule. Ignored on GEA3.

  > **This is a per-ERD interval, not an inter-read delay.** Each polled ERD
  > targets `poll_interval` on its own, so the bus work scales with the number
  > of ERDs. A low global `poll_interval` with many ERDs oversubscribes the bus
  > (see [Bus saturation](#bus-saturation--sizing-your-intervals)); prefer a slow
  > default and accelerate only the ERDs you need with `poll_overrides`.
- **poll_overrides** (*Optional*, list — GEA2 only): Per-ERD interval overrides,
  keyed by ERD. ERDs without an override use `poll_interval`. Because polling is
  per ERD, the cadence belongs to the ERD, not the entity: setting it per entity
  would be ambiguous when two entities share an ERD. Entities keep declaring
  their ERD as usual and inherit that ERD's cadence.

  ```yaml
  poll_overrides:
    - erd: 0x2038
      interval: 200ms   # fast-changing — refresh often
    - erd: 0x3001
      interval: 30s     # slow setpoint — refresh rarely
  ```

  The scheduler serves the **most overdue ERD first**, which is
  *starvation-free*: an ERD's lateness grows every millisecond it waits, so a
  slow ERD always eventually outranks a fast one (whose lateness resets the
  moment it is served). A very fast ERD therefore cannot starve the others — it
  only adds latency, and only up to what bus throughput allows.

### Bus saturation — sizing your intervals

A single GEA2 read-and-response takes about **30–40 ms** on the 19 200-baud bus,
and only one read is in flight at a time. So an ERD polled every `interval` keeps
the bus busy roughly `35 ms / interval` of the time, and the whole schedule
demands:

```
bus occupancy ≈ Σ ( 35 ms / interval_of_each_ERD )
```

Keep this comfortably below 100 %. As a rule of thumb, staying under ~60 % leaves
room for the appliance's own internal traffic and the collision-avoidance gate:

| Schedule                              | Occupancy                          |
|---------------------------------------|------------------------------------|
| 15 ERDs @ `2s`                        | 15 × 35/2000 ≈ **26 %**            |
| 15 ERDs @ `2s` + one ERD @ `200ms`    | 26 % + 35/200 ≈ **44 %**           |
| 15 ERDs @ `100ms`                     | 15 × 35/100 ≈ **525 %** — oversubscribed |

The hub estimates this when it builds the poll schedule and logs it
(`~NN% estimated bus occupancy`), warns at ≥60 %, and warns loudly when the
schedule is **oversubscribed** (≥90 %). It also reports the **measured**
occupancy every 10 s (`GEA2 bus occupancy: ~NN% busy, ~NN% idle`), so you can
confirm the real headroom on your bus.

> **Why this matters for the appliance.** The GEA2 bus is shared with the
> appliance's own boards (control ↔ UI ↔ Wi-Fi module). If your polling
> saturates it you don't just get stale reads and rising retries on the ESPHome
> side — you also crowd out the appliance's internal traffic and the collision
> gate. Size your intervals so the bus keeps real idle time. A per-ERD interval
> is a *target*, not a guarantee: under contention the scheduler serves the most
> overdue ERD first but cannot exceed bus throughput.
- **erd_lookup** (*Optional*, boolean): Embed the public GE ERD definition set
  (~75 KB flash) so discovery logs include each ERD's documented name, type,
  and decoded value. Defaults to `false`.
- **gea2_discovery** (*Optional*, boolean — GEA2 only): Scan all 2 444 known
  GEA2 ERDs on first boot, persist the responsive ones to flash (NVS), then
  poll only those going forward. Takes ~20–30 minutes on first run; subsequent
  boots load the saved list instantly and skip the scan. Defaults to `false`.
  See [ERD Discovery](erd-discovery.md) for the full workflow.
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
# GEA3 — XIAO ESP32-C3 hardware UART pins
uart:
  id: uart_gea
  tx_pin: GPIO21
  rx_pin: GPIO20
  baud_rate: 230400
```

For GEA2, the correct pins depend on your adapter board:

| Adapter | TX pin | RX pin |
|---------|--------|--------|
| [FirstBuild GEA adapter](https://firstbuild.com/) | GPIO9 | GPIO10 |
| [mulcmu adapter](https://github.com/mulcmu/esphome-ge-laundry-uart) | GPIO5 (or GPIO9 via solder bridge) | GPIO10 |

```yaml
# GEA2 — FirstBuild adapter
uart:
  id: uart_gea
  tx_pin: GPIO9
  rx_pin: GPIO10
  baud_rate: 19200

# GEA2 — mulcmu adapter (default Tx; see schematic for solder bridge option)
uart:
  id: uart_gea
  tx_pin: GPIO5
  rx_pin: GPIO10
  baud_rate: 19200
```

## GEA2 vs GEA3 — what changes for you?

| Behaviour              | GEA3                                | GEA2                                  |
|------------------------|-------------------------------------|---------------------------------------|
| Discovery              | Subscribes to all ERDs at boot      | Reads only ERDs you declare           |
| Refresh model          | Appliance pushes on change          | Hub polls each ERD on its own interval |
| `dest_address`         | Optional (auto-detected)            | Required                              |
| `poll_interval`        | Ignored                             | Per-ERD default (2 s), tuneable; override per ERD with `poll_overrides` |
| Bus speed              | 230400 baud                         | 19200 baud                            |

Everything else — entities, automations, diagnostics, `erd_lookup` — works the
same on both protocols.

## See also

- [Protocol internals](protocol.md)
- [ERD Discovery](erd-discovery.md)
- [Diagnostics](diagnostics.md)
- [`uart` component (esphome.io)](https://esphome.io/components/uart)
