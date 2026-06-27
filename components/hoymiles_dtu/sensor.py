import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_NUMBER,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_KILOWATT_HOURS,
    UNIT_VOLT,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)

from . import HoymilesDtuInverter
from .const import (
    CONF_AC_CURRENT,
    CONF_AC_FREQUENCY,
    CONF_AC_POWER,
    CONF_AC_VOLTAGE,
    CONF_CHANNELS,
    CONF_DC_CURRENT,
    CONF_DC_POWER,
    CONF_DC_VOLTAGE,
    CONF_INVERTER_ID,
    CONF_TEMPERATURE,
    CONF_YIELD_TODAY,
    CONF_YIELD_TOTAL,
)

SENSOR_TYPES = {
    CONF_AC_POWER: sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    CONF_AC_VOLTAGE: sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    CONF_AC_CURRENT: sensor.sensor_schema(
        unit_of_measurement=UNIT_AMPERE,
        accuracy_decimals=2,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    CONF_AC_FREQUENCY: sensor.sensor_schema(
        unit_of_measurement=UNIT_HERTZ,
        accuracy_decimals=2,
        device_class=DEVICE_CLASS_FREQUENCY,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    CONF_TEMPERATURE: sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    CONF_YIELD_TODAY: sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT_HOURS,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_ENERGY,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
    CONF_YIELD_TOTAL: sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        accuracy_decimals=3,
        device_class=DEVICE_CLASS_ENERGY,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
}

CHANNEL_SENSOR_TYPES = {
    CONF_DC_VOLTAGE: sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    CONF_DC_CURRENT: sensor.sensor_schema(
        unit_of_measurement=UNIT_AMPERE,
        accuracy_decimals=2,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    CONF_DC_POWER: sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    CONF_YIELD_TODAY: sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT_HOURS,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_ENERGY,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
}

CHANNEL_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NUMBER): cv.int_range(min=1, max=4),
        **{cv.Optional(key): schema for key, schema in CHANNEL_SENSOR_TYPES.items()},
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_INVERTER_ID): cv.use_id(HoymilesDtuInverter),
        **{cv.Optional(key): schema for key, schema in SENSOR_TYPES.items()},
        cv.Optional(CONF_CHANNELS): cv.ensure_list(CHANNEL_SCHEMA),
    }
)


async def to_code(config):
    inverter = await cg.get_variable(config[CONF_INVERTER_ID])

    for key in SENSOR_TYPES:
        if key not in config:
            continue
        sens = await sensor.new_sensor(config[key])
        setter = getattr(inverter, f"set_{key}_sensor")
        cg.add(setter(sens))

    for channel_config in config.get(CONF_CHANNELS, []):
        number = channel_config[CONF_NUMBER]
        for key in CHANNEL_SENSOR_TYPES:
            if key not in channel_config:
                continue
            sens = await sensor.new_sensor(channel_config[key])
            setter_name = f"set_channel_{key}_sensor"
            cg.add(getattr(inverter, setter_name)(number, sens))
