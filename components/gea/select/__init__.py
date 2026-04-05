import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID, CONF_OPTIONS
from .. import (
    gea_ns,
    GEAComponent,
    CONF_GEA_ID,
    CONF_ERD,
    CONF_DECODE,
    CONF_BYTE_OFFSET,
    DECODE_TYPES,
)

DEPENDENCIES = ["gea"]

GEASelect = gea_ns.class_("GEASelect", select.Select, cg.Component)


def validate_options(value):
    """Accept {int_or_hex_str: label_str} and normalise keys to int."""
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
        result[key] = str(v)
    return result


CONFIG_SCHEMA = (
    select.select_schema(GEASelect)
    .extend(
        {
            cv.GenerateID(CONF_GEA_ID): cv.use_id(GEAComponent),
            cv.Required(CONF_ERD): cv.hex_uint16_t,
            cv.Required(CONF_OPTIONS): validate_options,
            cv.Optional(CONF_DECODE, default="uint8"): cv.enum(
                DECODE_TYPES, lower=True
            ),
            cv.Optional(CONF_BYTE_OFFSET, default=0): cv.uint8_t,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    options_map = config[CONF_OPTIONS]
    option_labels = list(options_map.values())

    var = await select.new_select(config, options=option_labels)
    await cg.register_component(var, config)

    hub = await cg.get_variable(config[CONF_GEA_ID])
    cg.add(var.set_erd(config[CONF_ERD]))
    cg.add(var.set_decode(config[CONF_DECODE]))
    cg.add(var.set_byte_offset(config[CONF_BYTE_OFFSET]))

    # Pass options map as pairs so C++ can build std::map<uint32_t, std::string>
    for key, label in options_map.items():
        cg.add(var.add_option(key, label))

    cg.add(hub.register_entity(var))
