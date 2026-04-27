# Troubleshooting

## Nothing happens, no logs from the GEA component

- Confirm `baud_rate: 230400` (not 19,200 — that's the older GEA2 protocol,
  not supported).
- Check TX/RX aren't swapped. The ESP's TX goes to the appliance's RX and
  vice versa.
- Verify GND is connected between the ESP and the appliance.
- Add a `is_bus_connected()` lambda binary_sensor (see
  [Diagnostics](diagnostics.md)) — `false` means no valid packet has arrived
  in the last 30 s.

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
