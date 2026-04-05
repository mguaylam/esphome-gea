import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = []
CODEOWNERS = []
MULTI_CONF = False

gea_ns = cg.esphome_ns.namespace("gea")
GEAComponent = gea_ns.class_("GEAComponent", cg.Component, uart.UARTDevice)

# Shared constants used by child platforms
CONF_GEA_ID = "gea_id"
CONF_ERD = "erd"
CONF_DECODE = "decode"
CONF_BYTE_OFFSET = "byte_offset"
CONF_BITMASK = "bitmask"
CONF_DEST_ADDRESS = "dest_address"
CONF_SRC_ADDRESS = "src_address"
CONF_RESUBSCRIBE_INTERVAL = "resubscribe_interval"

GeaDecodeType = gea_ns.enum("GeaDecodeType")

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
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GEAComponent),
        # dest_address is optional — omit to enable auto-detect (recommended for
        # single-appliance setups; the address is learned from the first valid packet).
        cv.Optional(CONF_DEST_ADDRESS): cv.hex_uint8_t,
        cv.Optional(CONF_SRC_ADDRESS, default=0xBB): cv.hex_uint8_t,
        # How often to re-send subscribe_all so state recovers from appliance power-cycles.
        cv.Optional(CONF_RESUBSCRIBE_INTERVAL, default="60s"): cv.update_interval,
    }
).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_DEST_ADDRESS in config:
        cg.add(var.set_dest_address(config[CONF_DEST_ADDRESS]))
    # else: auto_detect_ stays true, address is learned at runtime

    cg.add(var.set_src_address(config[CONF_SRC_ADDRESS]))
    cg.add(var.set_resubscribe_interval(config[CONF_RESUBSCRIBE_INTERVAL]))
