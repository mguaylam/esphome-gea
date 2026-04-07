import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID
from .. import (
    gea_ns,
    GEAComponent,
    CONF_GEA_ID,
    CONF_ERD,
)

DEPENDENCIES = ["gea"]

CONF_PAYLOAD = "payload"

GEAButton = gea_ns.class_("GEAButton", button.Button, cg.Component)

CONFIG_SCHEMA = (
    button.button_schema(GEAButton)
    .extend(
        {
            cv.GenerateID(CONF_GEA_ID): cv.use_id(GEAComponent),
            cv.Required(CONF_ERD): cv.hex_uint16_t,
            # Payload bytes to write when the button is pressed.
            # Accepts a list of integers, e.g. [0x01] or [0x00, 0x02].
            cv.Optional(CONF_PAYLOAD, default=[0x01]): cv.All(
                cv.ensure_list(cv.hex_uint8_t), cv.Length(min=1)
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await button.register_button(var, config)

    hub = await cg.get_variable(config[CONF_GEA_ID])
    cg.add(var.set_erd(config[CONF_ERD]))
    for byte_val in config[CONF_PAYLOAD]:
        cg.add(var.add_payload_byte(byte_val))
    cg.add(var.set_parent(hub))
