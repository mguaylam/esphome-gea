import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_UNIT_OF_MEASUREMENT,
    CONF_ACCURACY_DECIMALS,
    CONF_DEVICE_CLASS,
    CONF_STATE_CLASS,
    CONF_ICON,
)
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

GEASensor = gea_ns.class_("GEASensor", sensor.Sensor, cg.Component)

CONFIG_SCHEMA = (
    sensor.sensor_schema(GEASensor)
    .extend(
        {
            cv.GenerateID(CONF_GEA_ID): cv.use_id(GEAComponent),
            cv.Required(CONF_ERD): cv.hex_uint16_t,
            cv.Optional(CONF_DECODE, default="uint16_be"): cv.enum(
                DECODE_TYPES, lower=True
            ),
            cv.Optional(CONF_BYTE_OFFSET, default=0): cv.uint8_t,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)

    hub = await cg.get_variable(config[CONF_GEA_ID])
    cg.add(var.set_erd(config[CONF_ERD]))
    cg.add(var.set_decode(config[CONF_DECODE]))
    cg.add(var.set_byte_offset(config[CONF_BYTE_OFFSET]))
    cg.add(hub.register_entity(var))
