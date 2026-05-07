# Protocol & Internals

This page documents how the component talks to the appliance. You don't need
any of this to use the component — but it's useful when reverse-engineering a
new appliance or debugging unusual behaviour on the bus.

The component supports both **GEA3** (newer appliances, full-duplex,
230400 baud) and **GEA2** (older appliances, half-duplex, 19200 baud). The
two protocols share the same wire framing — only the command codes and the
ERD payload format differ.

## Frame format

Each frame on the wire looks like:

![GEA Frame](frame.svg)

- **STX / ETX:** Frame delimiters (`0xE2` / `0xE3`).
- **LEN:** Total logical length = `7 + len(payload)`.
- **CRC:** CRC-16/CCITT, polynomial `0x1021`, seed `0x1021`, computed over
  `DEST + LEN + SRC + PAYLOAD`, MSB-first on the wire.
- **Escaping:** any of `0xE0`–`0xE3` inside the inner bytes is prefixed with
  `0xE0` (the *escape* byte). The receiver state machine strips the escape on
  decode.

The framing is **identical** between GEA2 and GEA3 — same delimiters, same
escape rule, same CRC seed and polynomial. What changes is what goes inside
the payload.

## Command codes

### GEA3 (`0xA0`–`0xA8`)

| Command | Code | Direction | Purpose |
|---------|------|-----------|---------|
| Read Request | `0xA0` | → Appliance | Query ERD value |
| Read Response | `0xA1` | ← Appliance | Returns ERD data |
| Write Request | `0xA2` | → Appliance | Set ERD value |
| Write Response | `0xA3` | ← Appliance | Confirms write success |
| Subscribe-All | `0xA4` | → Appliance | Trigger full ERD publication |
| Subscribe-All Ack | `0xA5` | ← Appliance | Confirms subscription |
| Publication | `0xA6` | ← Appliance | Broadcasts ERD changes |
| Publication Ack | `0xA7` | → Appliance | Acknowledges publication |
| Subscription Host Startup | `0xA8` | ← Appliance | Appliance just came online |
| ACK | `0xE1` | ↔ Both | Single-byte acknowledgement |

GEA3 payloads carry an explicit `request_id` byte that the appliance echoes
back on the matching response.

### GEA2 (`0xF0` / `0xF1`)

| Command | Code | Direction | Purpose |
|---------|------|-----------|---------|
| Read Request / Response | `0xF0` | ↔ Both | Same code in both directions; direction is implied by `dest`. |
| Write Request / Response | `0xF1` | ↔ Both | Same as above. |
| ACK | `0xE1` | ↔ Both | Single-byte acknowledgement |

GEA2 has **no subscriptions, no publications, and no `request_id`**. There
is no equivalent of the GEA3 `0xA4`–`0xA8` family. The hub matches a
response to the pending request by ERD address — which works because only
one request is on the wire at a time.

### ERD payload layout

This is where GEA2 and GEA3 actually diverge:

| | GEA3 read | GEA2 read |
|---|---|---|
| Body | `[req_id][erd_h][erd_l]` | `[count=1][erd_h][erd_l]` |

| | GEA3 write | GEA2 write |
|---|---|---|
| Body | `[req_id][erd_h][erd_l][size][data…]` | `[count=1][erd_h][erd_l][size][data…]` |

GEA2 prefixes the body with a single-byte ERD count (always `1` here) and
omits the request ID. GEA3 includes the request ID and no count.

## Typical exchange (GEA3)

```mermaid
sequenceDiagram
    participant E as ESP32
    participant A as GE Appliance

    Note over E,A: Boot — initial subscription
    E->>A: Subscribe-All 0xA4 (type=0x00)
    A-->>E: ACK 0xE1
    A->>E: Subscribe-All Ack 0xA5
    E-->>A: ACK 0xE1
    A->>E: Publication 0xA6 (all ERDs broadcast)
    E-->>A: Publication Ack 0xA7

    Note over E,A: Read ERD (e.g. water temperature)
    E->>A: Read Request 0xA0 [ERD=0x3035]
    A-->>E: ACK 0xE1
    A->>E: Read Response 0xA1 [value=75]
    E-->>A: ACK 0xE1

    Note over E,A: Write ERD (from Home Assistant)
    E->>A: Write Request 0xA2 [ERD=0x321B, val=0x02]
    A-->>E: ACK 0xE1
    A->>E: Write Response 0xA3
    E-->>A: ACK 0xE1

    Note over E,A: Appliance-initiated change
    A->>E: Publication 0xA6 [ERD=0x2000 updated]
    E-->>A: Publication Ack 0xA7
```

## Typical exchange (GEA2)

GEA2 has no subscriptions, so the hub polls each declared ERD in turn. Since
the bus is half-duplex the hub also sees its own transmission echoed back —
those echoes are filtered out by checking the `dest` field.

```mermaid
sequenceDiagram
    participant E as ESP32
    participant A as GE Appliance

    Note over E,A: Round-robin polling — every poll_interval (default 2 s)
    E->>A: Read Request 0xF0 [count=1, ERD=0x2012]
    A-->>E: ACK 0xE1
    A->>E: Read Response 0xF0 [count=1, ERD=0x2012, size, data]
    E-->>A: ACK 0xE1

    Note over E,A: Write from Home Assistant — interleaves with the poll loop
    E->>A: Write Request 0xF1 [count=1, ERD=0x3230, size=1, val=0x01]
    A-->>E: ACK 0xE1
    A->>E: Write Response 0xF1 [count=1, ERD=0x3230]
    E-->>A: ACK 0xE1
    Note over E: Hub immediately re-reads 0x3230\nso entity state mirrors the appliance.
```

## Connection lifecycle (GEA3)

The component uses a two-state subscription machine:

```mermaid
stateDiagram-v2
    direction LR
    [*] --> SUBSCRIBING

    SUBSCRIBING --> SUBSCRIBED : 0xA5 ack (result == 0x00)
    SUBSCRIBED --> SUBSCRIBING : 0xA8 appliance reboot\nor bus reconnect

    state SUBSCRIBING {
        [*] --> idle
        idle --> tx : every 1 s
        tx --> idle : send subscribe-all\ntype=0x00
    }

    state SUBSCRIBED {
        [*] --> active
        active --> keepalive : every 30 s
        keepalive --> active : send subscribe-all\ntype=0x01 (retain)
    }
```

| State | Behaviour |
|-------|-----------|
| **SUBSCRIBING** | Sends subscribe-all `type=0x00` every **1 s** until the appliance acknowledges. |
| **SUBSCRIBED** | Sends subscribe-all `type=0x01` (retain) every **30 s** as a keep-alive. |

The keep-alive is required because the appliance silently drops subscriptions
after a few minutes even when the bus stays physically connected.

Transition back to **SUBSCRIBING** happens on either:

- **Primary:** a `0xA8` "subscription host startup" packet from the appliance
  (fires immediately when the appliance broadcasts its boot announcement).
- **Fallback:** a bus silent → active transition, detected when
  `is_bus_connected()` flips from `false` to `true`. Covers cases where the
  startup packet is missed.

## Connection lifecycle (GEA2)

There is no subscription state machine on GEA2. Instead, the hub:

1. Builds a deduplicated list of every ERD referenced by an entity or by an
   `on_erd_change` automation.
2. Round-robin polls one ERD per `poll_interval`, single-in-flight, sharing
   the same retry queue as user-initiated writes.
3. Skips a tick when the queue is busy — so a write doesn't get stuck behind
   the poll cycle.
4. After a successful write, immediately re-reads the same ERD so the
   matching entity reflects the appliance's new state without waiting for
   the next poll.

Bus health is still derived from `last_rx_ms_`, so `is_bus_connected()` works
identically on both protocols.

## Bus topology & hardware

GEA2 and GEA3 share the same logical framing but use very different
electrical layers — and on a multi-node bus, that has consequences.

### GEA3 — full-duplex, point-to-point

GEA3 separates TX and RX physically. Two simple tri-state buffers on the
carrier board (one per direction) bridge the appliance bus to the MCU UART.
There's no echo, no collision risk, no shared medium. From the firmware's
point of view it's just a fast UART link.

### GEA2 — half-duplex, multi-node, wired-OR

GEA2 is a **shared open-drain bus** that several appliance modules
(main board, control panel, ice maker, dispenser, …) drive simultaneously
in a wired-OR fashion. On a typical FirstBuild / GE Home Assistant Adapter
board:

- A high-side PNP transistor drives the bus dominant when the MCU TX is
  asserted; release lets the bus float to the recessive state.
- A NAND-gate inverter on the receive path feeds the **bus** signal (not
  the MCU TX line) back into the MCU RX. As a side-effect, **everything
  the MCU transmits is echoed back on RX** — this is intentional and is
  what enables byte-level collision detection in GE's reference firmware.
- No UART inversion is required: idle bus → MCU RX HIGH → standard 8N1.

Schematic of the official board:
[geappliances/home-assistant-adapter/doc/schematic-v1.0.pdf](https://github.com/geappliances/home-assistant-adapter/blob/main/doc/schematic-v1.0.pdf).

## Collision handling (GEA2)

Because GEA2 is a multi-node bus, two nodes can start transmitting at the
same time and corrupt each other's frames. GE's reference firmware
(`tiny-gea-api`) uses **byte-level echo verification**: each transmitted
byte is compared against what comes back on RX, and a mismatch triggers an
immediate abort + pseudo-random backoff (≈43–106 ms, derived from the
node's address).

**What this component does today:**

- The bus self-echo is filtered out by checking the `dest` field against
  our own `src_address` — packets we sent ourselves are discarded before
  parsing.
- We do **not** verify the echo byte-by-byte and we do **not** implement
  the backoff FSM.
- If a collision corrupts our request, we recover via the standard
  request-reliability path (250 ms timeout × 10 retries — see below).

**Why it works in practice:**

The other nodes on the bus run `tiny-gea-api` (or equivalent) and **do**
detect collisions on their side, so they back off and retransmit within
~50 ms. We notice the missing response 250 ms later and retry. Both sides
eventually converge — at the cost of slightly higher latency on our end
during a collision.

**What we lose by not implementing byte-level CD:**

- Slower recovery after a collision (250 ms timeout vs ~50 ms backoff).
- We can briefly corrupt another node's in-flight frame before its CD
  catches it, forcing it to retransmit. No data loss, just bus noise.
- Less informative diagnostics on hardware faults: a missing echo today
  looks the same as a missing response (timeout + retry), whereas
  byte-level CD would flag wiring/power issues immediately.

This is a known and intentional trade-off — see
[Discussion #TBD](#) for field reports and the conditions under which a
byte-level CD implementation might be worth the added complexity.

## Request reliability

Every outgoing request (read, write, subscribe-all) goes through a
single-in-flight queue with deterministic retry:

| Parameter | Value |
|-----------|-------|
| Timeout per attempt | **250 ms** |
| Max retries | **10** |
| Total worst-case | **~2.75 s** before a request is dropped |

- **Serialization** — only one request is on the wire at a time, so on GEA3
  the `request_id` matches between request and response without ambiguity,
  and on GEA2 the ERD address alone is enough to identify the pending
  exchange.
- **Retry on timeout** — if no matching response arrives within 250 ms, the
  same request is resent. A late response from a prior attempt still
  matches.
- **Response matching** —
  - **GEA3**: incoming responses whose `request_id` doesn't match the
    pending request are ignored.
  - **GEA2**: incoming responses whose ERD doesn't match the pending request
    are ignored. Self-echoes on the half-duplex bus are filtered earlier by
    the `dest` check, so they never reach this stage.
- **Unsolicited frames bypass the queue** — ACKs, publications, publication
  ACKs, and subscription host startup packets are not request/response pairs
  and are processed independently. (GEA2 has none of these.)

Writes initiated from Home Assistant are **non-blocking**: the entity returns
immediately and the queue transmits in the background. A dropped write
(10 retries exhausted) is logged at `WARN` level and increments
`get_dropped_requests()` — see [Diagnostics](diagnostics.md).

## References

This component's protocol implementation was informed by GE Appliances'
publicly released reference projects (all BSD-3-Clause). No code is copied
— our C++ implementation is independent — but their work documents the
wire-level behaviour, timing constants, and design intent of the bus.

- **[geappliances/tiny-gea-api](https://github.com/geappliances/tiny-gea-api)**
  — minimal C library implementing both GEA2 and GEA3 framing, CRC, and
  the GEA2 byte-level collision-detection FSM. The canonical source of
  truth for "what should a node on this bus actually do".
- **[geappliances/home-assistant-adapter](https://github.com/geappliances/home-assistant-adapter)**
  — the official GE-published carrier board (schematic, firmware, BOM).
  Useful for understanding the GEA2 electrical layer (echo path, PNP
  driver, no UART inversion required).
- **[geappliances/arduino-gea2](https://github.com/geappliances/arduino-gea2)**
  — Arduino wrapper around `tiny-gea-api` for GEA2; doubles as a working
  example of the polling and timing constants in a real sketch.

## See also

- [Diagnostics](diagnostics.md)
- [Troubleshooting](troubleshooting.md)
