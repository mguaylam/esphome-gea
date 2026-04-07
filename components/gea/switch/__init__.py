import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID
from .. import (
    gea_ns,
    GEAComponent,
    CONF_GEA_ID,
    CONF_ERD,
    CONF_BYTE_OFFSET,
    CONF_WRITE_ERD,
)

DEPENDENCIES = ["gea"]

CONF_PAYLOAD_ON = "payload_on"
CONF_PAYLOAD_OFF = "payload_off"

GEASwitch = gea_ns.class_("GEASwitch", switch.Switch, cg.Component)

CONFIG_SCHEMA = (
    switch.switch_schema(GEASwitch)
    .extend(
        {
            cv.GenerateID(CONF_GEA_ID): cv.use_id(GEAComponent),
            cv.Required(CONF_ERD): cv.hex_uint16_t,
            cv.Optional(CONF_BYTE_OFFSET, default=0): cv.uint8_t,
            cv.Optional(CONF_WRITE_ERD): cv.hex_uint16_t,
            cv.Optional(CONF_PAYLOAD_ON, default=0x01): cv.hex_uint8_t,
            cv.Optional(CONF_PAYLOAD_OFF, default=0x00): cv.hex_uint8_t,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)

    hub = await cg.get_variable(config[CONF_GEA_ID])
    cg.add(var.set_erd(config[CONF_ERD]))
    cg.add(var.set_byte_offset(config[CONF_BYTE_OFFSET]))

    if CONF_WRITE_ERD in config:
        cg.add(var.set_write_erd(config[CONF_WRITE_ERD]))

    cg.add(var.set_on_value(config[CONF_PAYLOAD_ON]))
    cg.add(var.set_off_value(config[CONF_PAYLOAD_OFF]))

    cg.add(hub.register_entity(var))
