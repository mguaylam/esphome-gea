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
| `get_dest_address()` | `uint8_t` | the appliance address in use (resolved by GEA2 address discovery, or as configured) |
| `is_address_resolved()` | `bool` | `false` while GEA2 address discovery is still probing or halted; `true` once an address is known |

On GEA2 with no `dest_address`, the discovered address is logged once at boot —
which can scroll past before a network client connects. To print it again on
demand, wire `log_address()` to `api: on_client_connected`:

```yaml
api:
  on_client_connected:
    - lambda: 'id(gea_hub).log_address();'
```

If you'd rather have it always visible in Home Assistant, `get_dest_address()`
exposes it for a persistent diagnostic `text_sensor`:

```yaml
text_sensor:
  - platform: template
    name: "GEA appliance address"
    entity_category: diagnostic
    lambda: |-
      char buf[7];
      snprintf(buf, sizeof(buf), "0x%02X", id(gea_hub).get_dest_address());
      return std::string(buf);
```

## Interpreting the counters

- A non-zero **CRC errors** counter on a stable bus usually points to a
  wiring/grounding issue.
- A growing **dropped requests** counter while the appliance is awake is more
  concerning and warrants protocol-level inspection (start with
  [Troubleshooting](troubleshooting.md)).
- **TX retries** is normal in small numbers under bus contention.

## See also

- [Troubleshooting](troubleshooting.md)
- [Protocol & internals](protocol.md)
- [Hub configuration](hub.md)
