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
)

DEPENDENCIES = ["gea"]

GEASwitch = gea_ns.class_("GEASwitch", switch.Switch, cg.Component)

CONFIG_SCHEMA = (
    switch.switch_schema(GEASwitch)
    .extend(
        {
            cv.GenerateID(CONF_GEA_ID): cv.use_id(GEAComponent),
            cv.Required(CONF_ERD): cv.hex_uint16_t,
            cv.Optional(CONF_BYTE_OFFSET, default=0): cv.uint8_t,
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
    cg.add(hub.register_entity(var))
