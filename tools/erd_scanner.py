#!/usr/bin/env python3
"""
ERD Scanner -- probes GE appliance ERDs via ESPHome native API.

Connects to an ESPHome device, reads ERDs one at a time, and reports
which ones the appliance responds to.  Results are printed live and
summarised at the end.

Prerequisites:
    pip install aioesphomeapi

The ESPHome device YAML must include a read_erd service:

    api:
      services:
        - service: read_erd
          variables:
            address: int
          then:
            - lambda: 'id(gea_hub)->read_erd((uint16_t)address);'

Usage:
    # Scan all known GE ERDs (uses erd-definitions JSON from repo)
    python3 tools/erd_scanner.py --host 192.168.1.42 --key "YOUR_API_KEY"

    # Scan a specific range
    python3 tools/erd_scanner.py --host 192.168.1.42 --key "YOUR_API_KEY" --range 0x0000-0x00FF

    # Slower scan (longer wait per ERD, for unreliable connections)
    python3 tools/erd_scanner.py --host 192.168.1.42 --key "YOUR_API_KEY" --delay 6
"""

import argparse
import asyncio
import json
import re
import sys
import time
from pathlib import Path

try:
    from aioesphomeapi import APIClient
except ImportError:
    print("Install aioesphomeapi:  pip install aioesphomeapi")
    sys.exit(1)


def load_erd_table(json_path):
    with open(json_path) as f:
        data = json.load(f)
    erds = []
    for entry in data["erds"]:
        erd_id = int(entry["id"], 16)
        erds.append({"id": erd_id, "name": entry["name"]})
    return sorted(erds, key=lambda e: e["id"])


def find_erd_json():
    candidates = [
        Path(__file__).parent.parent / "erd-definitions" / "appliance_api_erd_definitions.json",
        Path("erd-definitions/appliance_api_erd_definitions.json"),
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    return None


async def scan(host, key, erds, delay, password):
    results = {}
    log_lines = []

    def on_log(msg):
        log_lines.append(msg.message)

    client = APIClient(host, 6053, password=password, noise_psk=key)
    try:
        await client.connect(login=True)
        print(f"Connected to {host}")

        await client.subscribe_logs(on_log)

        resp = await client.list_entities_services()
        services = resp.services if hasattr(resp, "services") else (resp[1] if isinstance(resp, tuple) else [])

        read_service = None
        for svc in services:
            if svc.name == "read_erd":
                read_service = svc
                break

        if read_service is None:
            print("\nERROR: 'read_erd' service not found on device.")
            print("Add this to your device YAML:\n")
            print("  api:")
            print("    services:")
            print("      - service: read_erd")
            print("        variables:")
            print("          address: int")
            print("        then:")
            print("          - lambda: 'id(gea_hub)->read_erd((uint16_t)address);'")
            return

        total = len(erds)
        found = 0
        failed = 0
        start_time = time.monotonic()

        print(f"Scanning {total} ERDs (delay={delay}s per ERD)\n")

        for i, erd in enumerate(erds):
            erd_id = erd["id"]
            erd_name = erd["name"]
            erd_hex = f"0x{erd_id:04X}"

            log_lines.clear()

            await client.execute_service(read_service, {"address": erd_id})

            # Wait for the ESP to process the request and respond (or timeout).
            # The request queue retries at 250ms intervals up to 10 times (~2.75s),
            # so we need to wait at least that long for non-existent ERDs.
            await asyncio.sleep(delay)

            hit = False
            for line in log_lines:
                if erd_hex not in line:
                    continue
                if "Read ERD" in line and "OK" in line:
                    found += 1
                    results[erd_id] = {"name": erd_name, "log": line.strip()}
                    print(f"  [{i+1:4d}/{total}]  FOUND  {erd_hex}  {erd_name}")
                    print(f"                    {line.strip()}")
                    hit = True
                    break
                if "failed" in line:
                    failed += 1
                    hit = True
                    break

            if not hit:
                # No response -- ERD not implemented on this appliance
                pass

            if not hit and (i + 1) % 100 == 0:
                elapsed = time.monotonic() - start_time
                rate = (i + 1) / elapsed if elapsed > 0 else 0
                eta = (total - i - 1) / rate if rate > 0 else 0
                print(f"  [{i+1:4d}/{total}]  ...scanning ({found} found, ~{eta/60:.0f} min remaining)")

        elapsed = time.monotonic() - start_time
        print(f"\n{'=' * 70}")
        print(f"Scan complete in {elapsed/60:.1f} minutes")
        print(f"  Probed:    {total}")
        print(f"  Found:     {found}")
        print(f"  Rejected:  {failed}")
        print(f"  No reply:  {total - found - failed}")
        print(f"{'=' * 70}")

        if results:
            print(f"\nResponsive ERDs:\n")
            for erd_id in sorted(results.keys()):
                r = results[erd_id]
                print(f"  0x{erd_id:04X}  {r['name']}")
                print(f"           {r['log']}")

            out_path = Path(f"erd_scan_{host.replace('.', '_')}.json")
            with open(out_path, "w") as f:
                json.dump(
                    {f"0x{k:04X}": v for k, v in sorted(results.items())},
                    f, indent=2,
                )
            print(f"\nResults saved to {out_path}")

    finally:
        await client.disconnect()


def main():
    parser = argparse.ArgumentParser(
        description="Scan GE appliance ERDs via ESPHome native API"
    )
    parser.add_argument("--host", required=True, help="ESP device IP or hostname")
    parser.add_argument("--key", required=True, help="API encryption key (base64)")
    parser.add_argument("--password", default="", help="Legacy API password (if any)")
    parser.add_argument(
        "--delay", type=float, default=4.0,
        help="Seconds to wait per ERD (default: 4, minimum ~3 for timeout coverage)",
    )
    parser.add_argument(
        "--range", default=None,
        help="ERD range to scan, e.g. 0x0000-0x00FF (default: all known GE ERDs)",
    )
    parser.add_argument(
        "--erds", default=None,
        help="Path to appliance_api_erd_definitions.json",
    )
    args = parser.parse_args()

    json_path = args.erds or find_erd_json()

    if args.range:
        match = re.match(r"(0x[0-9A-Fa-f]+)-(0x[0-9A-Fa-f]+)", args.range)
        if not match:
            print(f"Invalid range: {args.range}  (expected e.g. 0x0000-0x00FF)")
            sys.exit(1)
        start, end = int(match.group(1), 16), int(match.group(2), 16)
        if json_path:
            all_erds = load_erd_table(json_path)
            erds = [e for e in all_erds if start <= e["id"] <= end]
            if not erds:
                erds = [{"id": i, "name": f"Unknown"} for i in range(start, end + 1)]
        else:
            erds = [{"id": i, "name": "Unknown"} for i in range(start, end + 1)]
    elif json_path:
        erds = load_erd_table(json_path)
    else:
        print("ERROR: No ERD definitions found.")
        print("Use --erds <path> or --range 0xSTART-0xEND")
        sys.exit(1)

    if args.delay < 3:
        print(f"WARNING: delay={args.delay}s may be too short (request timeout is ~2.75s)")

    asyncio.run(scan(args.host, args.key, erds, args.delay, args.password))


if __name__ == "__main__":
    main()
