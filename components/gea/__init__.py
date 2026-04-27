import json
import subprocess
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import uart
from esphome.const import CONF_ID, CONF_TRIGGER_ID


def _load_erd_table():
    """Parse erd-definitions/appliance_api_erd_definitions.json from the submodule."""
    repo_root = Path(__file__).parent.parent.parent
    json_path = repo_root / "erd-definitions" / "appliance_api_erd_definitions.json"
    if not json_path.exists():
        try:
            subprocess.run(
                ["git", "submodule", "update", "--init", "erd-definitions"],
                cwd=repo_root, check=True, capture_output=True,
            )
        except Exception:
            pass
    if not json_path.exists():
        return {}
    with open(json_path) as f:
        data = json.load(f)
    table = {}
    for erd in data.get("erds", []):
        try:
            erd_id = int(erd["id"], 16)
        except (KeyError, ValueError):
            continue
        if erd_id > 0xFFFF:
            continue
        name = erd.get("name", "").replace("\\", "\\\\").replace('"', '\\"')
        ops  = "|".join(erd.get("operations", []))
        types = "/".join(f.get("type", "") for f in erd.get("data", []))
        table[erd_id] = (name, types, ops)
    return table


def _write_erd_table_header(table):
    """Write components/gea/erd_table.h with a sorted static lookup table."""
    lines = [
        "// Auto-generated from erd-definitions/appliance_api_erd_definitions.json",
        "// Do not edit manually.",
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "namespace esphome {",
        "namespace gea {",
        "",
        "struct ErdTableEntry {",
        "  uint16_t id;",
        "  const char *name;",
        "  const char *type;",
        "  const char *ops;",
        "};",
        "",
        "static const ErdTableEntry ERD_TABLE[] = {",
    ]
    for erd_id, (name, types, ops) in sorted(table.items()):
        lines.append(f'  {{0x{erd_id:04X}, "{name}", "{types}", "{ops}"}},')
    lines += [
        "};",
        "",
        "static const size_t ERD_TABLE_SIZE = sizeof(ERD_TABLE) / sizeof(ERD_TABLE[0]);",
        "",
        "static const ErdTableEntry *erd_lookup(uint16_t id) {",
        "  size_t lo = 0, hi = ERD_TABLE_SIZE;",
        "  while (lo < hi) {",
        "    size_t mid = (lo + hi) / 2;",
        "    if (ERD_TABLE[mid].id == id) return &ERD_TABLE[mid];",
        "    if (ERD_TABLE[mid].id < id) lo = mid + 1;",
        "    else hi = mid;",
        "  }",
        "  return nullptr;",
        "}",
        "",
        "}  // namespace gea",
        "}  // namespace esphome",
    ]
    out = Path(__file__).parent / "erd_table.h"
    out.write_text("\n".join(lines) + "\n")

DEPENDENCIES = ["uart"]
AUTO_LOAD = []
CODEOWNERS = ["@michaelguaylambert"]
MULTI_CONF = False

gea_ns = cg.esphome_ns.namespace("gea")
GEAComponent = gea_ns.class_("GEAComponent", cg.Component, uart.UARTDevice)
ErdChangeTrigger = gea_ns.class_("ErdChangeTrigger", automation.Trigger.template())

# Shared constants used by child platforms
CONF_GEA_ID = "gea_id"
CONF_ERD = "erd"
CONF_DECODE = "decode"
CONF_BYTE_OFFSET = "byte_offset"
CONF_BITMASK = "bitmask"
CONF_WRITE_ERD = "write_erd"
CONF_DATA_SIZE = "data_size"
CONF_DEST_ADDRESS = "dest_address"
CONF_SRC_ADDRESS = "src_address"
CONF_ON_ERD_CHANGE = "on_erd_change"
CONF_EDGE = "edge"
CONF_MULTIPLIER = "multiplier"
CONF_OFFSET = "offset"

GeaDecodeType = gea_ns.enum("GeaDecodeType")

ErdChangeEdge = ErdChangeTrigger.enum("Edge")
EDGES = {
    "rising": ErdChangeEdge.EDGE_RISING,
    "falling": ErdChangeEdge.EDGE_FALLING,
    "any": ErdChangeEdge.EDGE_ANY,
}

DECODE_TYPES = {
    "uint8":     GeaDecodeType.UINT8,
    "uint16_be": GeaDecodeType.UINT16_BE,
    "uint16_le": GeaDecodeType.UINT16_LE,
    "uint32_be": GeaDecodeType.UINT32_BE,
    "uint32_le": GeaDecodeType.UINT32_LE,
    "int8":      GeaDecodeType.INT8,
    "int16_be":  GeaDecodeType.INT16_BE,
    "int16_le":  GeaDecodeType.INT16_LE,
    "int32_be":  GeaDecodeType.INT32_BE,
    "int32_le":  GeaDecodeType.INT32_LE,
    "bool":      GeaDecodeType.BOOL,
    "raw":       GeaDecodeType.RAW,
    "ascii":     GeaDecodeType.ASCII,
}


def validate_options(value):
    """Accept {int_or_hex_str: label_str} and normalise keys to int.

    Rejects duplicate keys after normalisation (e.g. 0x04 and "4" would collide).
    """
    if not isinstance(value, dict):
        raise cv.Invalid("options must be a mapping of integer keys to string labels")
    result = {}
    for k, v in value.items():
        if isinstance(k, str):
            try:
                key = int(k, 0)  # handles "0x04", "4", etc.
            except ValueError:
                raise cv.Invalid(f"option key {k!r} is not a valid integer")
        else:
            key = int(k)
        if key in result:
            raise cv.Invalid(f"duplicate option key {key} (0x{key:02X})")
        if not (0 <= key <= 0xFFFFFFFF):
            raise cv.Invalid(f"option key {key} out of range")
        result[key] = str(v)
    if not result:
        raise cv.Invalid("options must contain at least one entry")
    return result


def validate_nonzero_multiplier(value):
    """Reject multiplier=0 — would zero out all readings and break inverse on write."""
    f = cv.float_(value)
    if f == 0.0:
        raise cv.Invalid("multiplier must be non-zero")
    return f


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GEAComponent),
        # dest_address is optional — omit to enable auto-detect (recommended for
        # single-appliance setups; the address is learned from the first valid packet).
        cv.Optional(CONF_DEST_ADDRESS): cv.hex_uint8_t,
        cv.Optional(CONF_SRC_ADDRESS, default=0xBB): cv.hex_uint8_t,
        # Set to true to embed the GE public ERD lookup table (~75 KB flash).
        # Enables ERD name, type, and decoded value in dump_config output.
        cv.Optional("erd_lookup", default=False): cv.boolean,
        # Automation: fire on bitmask-edge transitions within an ERD publication.
        # First publication after boot establishes a silent baseline (no trigger).
        cv.Optional(CONF_ON_ERD_CHANGE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ErdChangeTrigger),
                cv.Required(CONF_ERD): cv.hex_uint16_t,
                cv.Optional(CONF_BYTE_OFFSET, default=0): cv.uint8_t,
                cv.Optional(CONF_BITMASK, default=0xFF): cv.hex_uint8_t,
                cv.Optional(CONF_EDGE, default="rising"): cv.one_of(
                    *EDGES, lower=True
                ),
            }
        ),
    }
).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    if config["erd_lookup"]:
        _write_erd_table_header(_load_erd_table())
        cg.add_build_flag("-DGEA_ERD_LOOKUP")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_DEST_ADDRESS in config:
        cg.add(var.set_dest_address(config[CONF_DEST_ADDRESS]))
    # else: auto_detect_ stays true, address is learned at runtime

    cg.add(var.set_src_address(config[CONF_SRC_ADDRESS]))

    for conf in config.get(CONF_ON_ERD_CHANGE, []):
        trigger = cg.new_Pvariable(
            conf[CONF_TRIGGER_ID],
            var,
            conf[CONF_ERD],
            conf[CONF_BYTE_OFFSET],
            conf[CONF_BITMASK],
            EDGES[conf[CONF_EDGE]],
        )
        await automation.build_automation(trigger, [], conf)
