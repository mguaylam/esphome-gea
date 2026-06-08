# Diagnostics

The hub exposes per-bus counters and helper accessors callable from YAML
lambdas. Pair them with `platform: template` sensors and
`entity_category: diagnostic` to surface bus health in Home Assistant.

```yaml
# Example configuration entry
sensor:
  - platform: template
    name: "GEA RX Bytes"
    entity_category: diagnostic
    accuracy_decimals: 0
    lambda: 'return id(gea_hub).get_rx_bytes();'
    update_interval: 30s

  - platform: template
    name: "GEA CRC Errors"
    entity_category: diagnostic
    accuracy_decimals: 0
    lambda: 'return id(gea_hub).get_crc_errors();'
    update_interval: 30s

  - platform: template
    name: "GEA TX Retries"
    entity_category: diagnostic
    accuracy_decimals: 0
    lambda: 'return id(gea_hub).get_tx_retries();'
    update_interval: 30s

  - platform: template
    name: "GEA Dropped Requests"
    entity_category: diagnostic
    accuracy_decimals: 0
    lambda: 'return id(gea_hub).get_dropped_requests();'
    update_interval: 30s

binary_sensor:
  - platform: template
    name: "GEA Bus Connected"
    lambda: 'return id(gea_hub).is_bus_connected();'
```

## Lambda accessors

| Accessor | Returns | Increments / changes on |
|----------|---------|------------------------|
| `get_rx_bytes()` | `uint32_t` | every byte received on the UART |
| `get_crc_errors()` | `uint32_t` | a framed packet whose CRC does not match |
| `get_tx_retries()` | `uint32_t` | a pending request times out and is resent |
| `get_dropped_requests()` | `uint32_t` | a pending request exhausts all retries |
| `is_bus_connected()` | `bool` | `true` when a valid packet has been received in the last 30 s |

## Interpreting the counters

- A non-zero **CRC errors** counter on a stable bus usually points to a
  wiring/grounding issue.
- A growing **dropped requests** counter while the appliance is awake is more
  concerning and warrants protocol-level inspection (start with
  [Troubleshooting](troubleshooting.md)).
- **TX retries** is normal in small numbers under bus contention.

## Logging levels

The component logs at the standard ESPHome levels. The default level is
`DEBUG`; raise it to `VERBOSE` only when chasing a wiring or protocol problem,
since the per-frame output is high-volume.

| Level | What the GEA component emits | When to use it |
|-------|------------------------------|----------------|
| `ERROR` | *(none today)* | — |
| `WARN` | CRC mismatch, ERD read/write failed (`result != 0`), request dropped after exhausting retries, subscribe-all rejected, unexpected response shape, discovery cap reached | Always visible — anomalies worth attention |
| `INFO` | ERD values read, auto-detected appliance address, subscription state changes, discovery progress/results, poll list built | Useful default in normal operation |
| `CONFIG` | Configuration dump at boot (`dump_config`) | Startup only |
| `DEBUG` *(default)* | STX / valid-packet markers, "ignoring packet" from another node, 10 s RX stats, write OK, retry on timeout | Routine diagnostics |
| `VERBOSE` | Per-frame RX dump, unexpected bytes, unmatched responses, keep-alive, GEA2 TX self-echo (loopback) | Deep wiring / protocol debugging |

On the half-duplex GEA2 bus everything the MCU transmits is echoed back on RX
(see [Protocol & internals](protocol.md)). That self-echo is identified by its
source address matching our own and dropped at `VERBOSE` — it does not count
toward bus liveness and is not treated as foreign traffic.

## See also

- [Troubleshooting](troubleshooting.md)
- [Protocol & internals](protocol.md)
- [Hub configuration](hub.md)
