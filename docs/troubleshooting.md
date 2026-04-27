# Troubleshooting

## Nothing happens, no logs from the GEA component

- Confirm the `baud_rate` matches the protocol: **230400** for GEA3,
  **19200** for GEA2. A baud mismatch produces total silence — no CRC
  errors, no packets, nothing.
- Confirm the `protocol:` setting matches the appliance. GEA3 is the default
  and is wrong for older appliances. If `protocol: gea2` is set,
  `dest_address:` must also be set (the hub can't auto-detect on GEA2).
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
- Make sure `dest_address` actually matches your appliance. GEA3 can
  auto-detect, but GEA2 cannot — a wrong dest means every read times out
  silently. `0xC0` is the most common value (washer/dryer/dishwasher main
  board).
- If you have an ERD declared but no entity uses it, it won't be polled —
  the poll list is built from referenced ERDs only.

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

## See also

- [ERD Discovery](erd-discovery.md)
- [Diagnostics](diagnostics.md)
- [Protocol & internals](protocol.md)
- [Open an issue](https://github.com/mguaylam/esphome-gea/issues) — please
  include your YAML and a snippet of the ESPHome logs
  (`esphome logs your-device.yaml`).
