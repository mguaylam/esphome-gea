# Troubleshooting

## Nothing happens, no logs from the GEA component

- Confirm the `baud_rate` matches the protocol: **230400** for GEA3,
  **19200** for GEA2. A baud mismatch produces total silence — no CRC
  errors, no packets, nothing.
- Confirm the `protocol:` setting matches the appliance. GEA3 is the default
  and is wrong for older appliances. With `protocol: gea2` and no
  `dest_address:`, the hub polls nothing until it has found the appliance
  address (see
  [below](#gea2-address-discovery-does-not-settle-on-an-address)).
- Check TX/RX aren't swapped. The ESP's TX goes to the appliance's RX and
  vice versa.
- Verify GND is connected between the ESP and the appliance.
- Add a `is_bus_connected()` lambda binary_sensor (see
  [Diagnostics](diagnostics.md)) — `false` means no valid packet has arrived
  in the last 30 s.

## GEA2-specific: entities stay "unknown" even after a poll cycle

- The default `poll_interval` is 2 s. With many declared ERDs the full
  refresh cycle takes `N × poll_interval` — give it a full minute before
  assuming an ERD doesn't respond.
- Make sure `dest_address` actually matches your appliance — a wrong dest
  means every read times out silently. The address varies by appliance; if
  you don't know it, remove `dest_address` and let the hub probe the bus (see
  the next section).
- If you have an ERD declared but no entity uses it, it won't be polled —
  the poll list is built from referenced ERDs only.

## GEA2: address discovery does not settle on an address

With `protocol: gea2` and no `dest_address`, the hub broadcasts a read of the
appliance-type ERD (`0x0008`) and adopts an address only when exactly one node
returns the value. In every other case it keeps probing and logs what it has
heard at `WARN`, for example:

```
GEA2 address discovery: no node answered the probe (ERD 0x0008) after 1 attempt(s). Nodes heard:
    0x45 — replied to the probe, but has no ERD 0x0008
    0x41 — did not reply to the probe (last frame: cmd 0x01 to 0xFF)
  If 0x45 is the control board, set 'dest_address: 0x45' and reflash. Still probing.
```

The list is repeated in the configuration dump when a log client connects, and
by `log_address()`. How to read it:

- **answered the probe** — the node returned its appliance type. Two or more
  such nodes are listed when the hub could not choose between them.
- **replied to the probe, but has no ERD 0x0008** — the node serves ERDs but
  not this one. A control board can be in this case; try its address.
- **did not reply to the probe** — the node was only seen sending other frames,
  to the hub, to everyone (`0xFF`) or to another node. A control board polling
  the Wi-Fi module address (`0xBF`) shows up this way.
- **nothing heard on the bus** — no valid frame at all: check wiring, baud
  rate and pin inversion first.

Set `dest_address` to the node you picked and reflash. If its ERDs don't
respond, try the next one in the list.

## "Subscribe-All Ack" never arrives (GEA3 only)

- Some appliances need the bus to settle first. The hub already retries
  every 1 s in `SUBSCRIBING` state — wait a few seconds.
- If the appliance is actually a GEA2 device, `subscribe-all` will never be
  acknowledged because GEA2 has no such command. Switch to
  `protocol: gea2`.

## Entities created but always show "unknown"

- The ERD address is probably wrong for your model. Run a discovery pass
  (see [ERD Discovery](erd-discovery.md)) and grep the logs for activity when
  you trigger that state on the appliance.
- For binary sensors, double-check the `byte_offset` and `bitmask` — the
  door bit might live in byte 1 of a multi-byte ERD, not byte 0.

## Switch / select / number writes don't take effect

- Some ERDs are read-only at the appliance side; the write goes through but
  the appliance ignores it. There's no programmatic way to know in advance —
  try operating the appliance manually and note which ERD changes, then
  write to *that* ERD.
- Some appliances expect writes to a *different* ERD than the read ERD.
  Use `write_erd:` to specify it.

## Bus shows occasional CRC errors

- Small numbers (a few per hour) are normal on a long cable run.
- A growing count usually points to a wiring/grounding issue. See the
  `get_crc_errors()` counter under [Diagnostics](diagnostics.md).

## "Delayed start" wakes up the appliance unexpectedly

Don't expose the delayed-start ERD as a writable entity (e.g. washer ERD
`0x2038`). Writing to it brings the machine out of sleep mode whether you
wanted to or not. **Read-only is fine.**

## `log_address()` on `on_client_connected` doesn't print anything

`on_client_connected` fires at authentication, just before the client sends its
log-subscribe request, and the logger doesn't replay past lines — so a one-shot
line emitted exactly on connect can be missed by the client that just connected.
Add a short `delay: 1s` before the lambda so the subscription is in place first
(see [Diagnostics](diagnostics.md#lambda-accessors)). The discovered
address is always written to the boot/serial log regardless.

## See also

- [ERD Discovery](erd-discovery.md)
- [Diagnostics](diagnostics.md)
- [Protocol & internals](protocol.md)
- [Open an issue](https://github.com/mguaylam/esphome-gea/issues) — please
  include your YAML and a snippet of the ESPHome logs
  (`esphome logs your-device.yaml`).
