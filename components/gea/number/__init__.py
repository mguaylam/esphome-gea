import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_ID,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_STEP,
)
from .. import (
    gea_ns,
    GEAComponent,
    CONF_GEA_ID,
    CONF_ERD,
    CONF_DECODE,
    CONF_BYTE_OFFSET,
    CONF_WRITE_ERD,
    CONF_MULTIPLIER,
    CONF_OFFSET,
    DECODE_TYPES,
    validate_nonzero_multiplier,
)

DEPENDENCIES = ["gea"]

GEANumber = gea_ns.class_("GEANumber", number.Number, cg.Component)

CONFIG_SCHEMA = (
    number.number_schema(GEANumber)
    .extend(
        {
            cv.GenerateID(CONF_GEA_ID): cv.use_id(GEAComponent),
            cv.Required(CONF_ERD): cv.hex_uint16_t,
            cv.Optional(CONF_DECODE, default="uint8"): cv.enum(
                DECODE_TYPES, lower=True
            ),
            cv.Optional(CONF_BYTE_OFFSET, default=0): cv.uint8_t,
            cv.Optional(CONF_MIN_VALUE, default=0): cv.float_,
            cv.Optional(CONF_MAX_VALUE, default=255): cv.float_,
            cv.Optional(CONF_STEP, default=1): cv.float_,
            cv.Optional(CONF_WRITE_ERD): cv.hex_uint16_t,
            cv.Optional(CONF_MULTIPLIER, default=1.0): validate_nonzero_multiplier,
            cv.Optional(CONF_OFFSET, default=0.0): cv.float_,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await number.new_number(
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    await cg.register_component(var, config)

    hub = await cg.get_variable(config[CONF_GEA_ID])
    cg.add(var.set_erd(config[CONF_ERD]))
    cg.add(var.set_decode(config[CONF_DECODE]))
    cg.add(var.set_byte_offset(config[CONF_BYTE_OFFSET]))
    cg.add(var.set_multiplier(config[CONF_MULTIPLIER]))
    cg.add(var.set_offset(config[CONF_OFFSET]))

    if CONF_WRITE_ERD in config:
        cg.add(var.set_write_erd(config[CONF_WRITE_ERD]))

    cg.add(hub.register_entity(var))
