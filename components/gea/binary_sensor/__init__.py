import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID
from .. import (
    gea_ns,
    GEAComponent,
    CONF_GEA_ID,
    CONF_ERD,
    CONF_BITMASK,
    CONF_BYTE_OFFSET,
)

DEPENDENCIES = ["gea"]

CONF_INVERTED = "inverted"

GEABinarySensor = gea_ns.class_("GEABinarySensor", binary_sensor.BinarySensor, cg.Component)

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(GEABinarySensor)
    .extend(
        {
            cv.GenerateID(CONF_GEA_ID): cv.use_id(GEAComponent),
            cv.Required(CONF_ERD): cv.hex_uint16_t,
            cv.Optional(CONF_BITMASK, default=0xFF): cv.hex_uint8_t,
            cv.Optional(CONF_BYTE_OFFSET, default=0): cv.uint8_t,
            cv.Optional(CONF_INVERTED, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await binary_sensor.register_binary_sensor(var, config)

    hub = await cg.get_variable(config[CONF_GEA_ID])
    cg.add(var.set_erd(config[CONF_ERD]))
    cg.add(var.set_bitmask(config[CONF_BITMASK]))
    cg.add(var.set_byte_offset(config[CONF_BYTE_OFFSET]))
    if config[CONF_INVERTED]:
        cg.add(var.set_inverted(True))
    cg.add(hub.register_entity(var))
