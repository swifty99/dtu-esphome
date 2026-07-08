import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import HoymilesDtuComponent, HoymilesDtuInverter
from .const import (
    CONF_DTU_ID,
    CONF_INVERTER_ID,
    CONF_LAST_RADIO_ERROR,
    CONF_LAST_RX_PAYLOAD,
    CONF_LAST_SEEN,
    CONF_SCAN_DETECTED,
    CONF_STATUS,
)

INVERTER_TEXT_SENSOR_TYPES = {
    CONF_STATUS: text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_LAST_SEEN: text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}

DTU_TEXT_SENSOR_TYPES = {
    CONF_LAST_RX_PAYLOAD: text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_LAST_RADIO_ERROR: text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_SCAN_DETECTED: text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}


def validate_text_sensor_config(config):
    if any(key in config for key in INVERTER_TEXT_SENSOR_TYPES) and (
        CONF_INVERTER_ID not in config
    ):
        raise cv.Invalid("inverter text sensors require inverter_id")
    if (
        any(key in config for key in DTU_TEXT_SENSOR_TYPES)
        and CONF_DTU_ID not in config
    ):
        raise cv.Invalid("radio diagnostic text sensors require dtu_id")
    if CONF_INVERTER_ID not in config and CONF_DTU_ID not in config:
        raise cv.Invalid("either inverter_id or dtu_id is required")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(CONF_INVERTER_ID): cv.use_id(HoymilesDtuInverter),
            cv.Optional(CONF_DTU_ID): cv.use_id(HoymilesDtuComponent),
            **{
                cv.Optional(key): schema
                for key, schema in INVERTER_TEXT_SENSOR_TYPES.items()
            },
            **{
                cv.Optional(key): schema
                for key, schema in DTU_TEXT_SENSOR_TYPES.items()
            },
        }
    ),
    validate_text_sensor_config,
)


async def to_code(config):
    if CONF_INVERTER_ID in config:
        inverter = await cg.get_variable(config[CONF_INVERTER_ID])
        for key in INVERTER_TEXT_SENSOR_TYPES:
            if key not in config:
                continue
            sens = await text_sensor.new_text_sensor(config[key])
            setter = getattr(inverter, f"set_{key}_text_sensor")
            cg.add(setter(sens))

    if CONF_DTU_ID in config:
        dtu = await cg.get_variable(config[CONF_DTU_ID])
        for key in DTU_TEXT_SENSOR_TYPES:
            if key not in config:
                continue
            sens = await text_sensor.new_text_sensor(config[key])
            setter = getattr(dtu, f"set_{key}_text_sensor")
            cg.add(setter(sens))
