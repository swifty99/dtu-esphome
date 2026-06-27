import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import HoymilesDtuInverter
from .const import CONF_INVERTER_ID, CONF_LAST_SEEN, CONF_STATUS

TEXT_SENSOR_TYPES = {
    CONF_STATUS: text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_LAST_SEEN: text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_INVERTER_ID): cv.use_id(HoymilesDtuInverter),
        **{cv.Optional(key): schema for key, schema in TEXT_SENSOR_TYPES.items()},
    }
)


async def to_code(config):
    inverter = await cg.get_variable(config[CONF_INVERTER_ID])
    for key in TEXT_SENSOR_TYPES:
        if key not in config:
            continue
        sens = await text_sensor.new_text_sensor(config[key])
        setter = getattr(inverter, f"set_{key}_text_sensor")
        cg.add(setter(sens))
