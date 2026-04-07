import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID
from .. import (
    gea_ns,
    GEAComponent,
    CONF_GEA_ID,
    CONF_ERD,
    CONF_DECODE,
    CONF_BYTE_OFFSET,
    DECODE_TYPES,
    validate_options,
)

DEPENDENCIES = ["gea"]

CONF_OPTIONS = "options"

GEATextSensor = gea_ns.class_("GEATextSensor", text_sensor.TextSensor, cg.Component)

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(GEATextSensor)
    .extend(
        {
            cv.GenerateID(CONF_GEA_ID): cv.use_id(GEAComponent),
            cv.Required(CONF_ERD): cv.hex_uint16_t,
            # "raw" publishes a hex string; numeric decode types publish decimal string.
            cv.Optional(CONF_DECODE, default="raw"): cv.enum(
                DECODE_TYPES, lower=True
            ),
            cv.Optional(CONF_BYTE_OFFSET, default=0): cv.uint8_t,
            # Optional value-to-label mapping (like select, but read-only).
            cv.Optional(CONF_OPTIONS): validate_options,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await text_sensor.register_text_sensor(var, config)

    hub = await cg.get_variable(config[CONF_GEA_ID])
    cg.add(var.set_erd(config[CONF_ERD]))
    cg.add(var.set_decode(config[CONF_DECODE]))
    cg.add(var.set_byte_offset(config[CONF_BYTE_OFFSET]))

    if CONF_OPTIONS in config:
        for key, label in config[CONF_OPTIONS].items():
            cg.add(var.add_option(key, label))

    cg.add(hub.register_entity(var))
