import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_ID

from .const import (
    CONF_CE_PIN,
    CONF_INVERTERS,
    CONF_IRQ_PIN,
    CONF_MODEL,
    CONF_PA_LEVEL,
    CONF_POLL_INTERVAL,
    CONF_SERIAL,
)
from .protocol import parse_serial

CODEOWNERS = ["@swifty99"]
DEPENDENCIES = ["spi"]
AUTO_LOAD = ["sensor", "text_sensor"]

hoymiles_dtu_ns = cg.esphome_ns.namespace("hoymiles_dtu")
HoymilesDtuComponent = hoymiles_dtu_ns.class_(
    "HoymilesDtuComponent", cg.Component, spi.SPIDevice
)
HoymilesDtuInverter = hoymiles_dtu_ns.class_("HoymilesDtuInverter")
HmModel = hoymiles_dtu_ns.enum("HmModel")
HmPaLevel = hoymiles_dtu_ns.enum("HmPaLevel")

MODEL_OPTIONS = {
    "hm_1200": HmModel.HM_1200,
    "hm_1500": HmModel.HM_1500,
}

PA_LEVEL_OPTIONS = {
    "min": HmPaLevel.PA_MIN,
    "low": HmPaLevel.PA_LOW,
    "high": HmPaLevel.PA_HIGH,
    "max": HmPaLevel.PA_MAX,
}


def validate_serial(value):
    return parse_serial(value)


INVERTER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HoymilesDtuInverter),
        cv.Required(CONF_SERIAL): validate_serial,
        cv.Required(CONF_MODEL): cv.enum(MODEL_OPTIONS, lower=True),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HoymilesDtuComponent),
            cv.Required(CONF_CE_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_IRQ_PIN): pins.gpio_input_pin_schema,
            cv.Optional(
                CONF_POLL_INTERVAL, default="15s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PA_LEVEL, default="low"): cv.enum(
                PA_LEVEL_OPTIONS, lower=True
            ),
            cv.Required(CONF_INVERTERS): cv.All(
                cv.ensure_list(INVERTER_SCHEMA), cv.Length(min=1)
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(
        spi.spi_device_schema(
            cs_pin_required=True, default_data_rate="1MHz", default_mode="MODE0"
        )
    )
)

FINAL_VALIDATE_SCHEMA = spi.final_validate_device_schema(
    "hoymiles_dtu", require_miso=True, require_mosi=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    ce_pin = await cg.gpio_pin_expression(config[CONF_CE_PIN])
    cg.add(var.set_ce_pin(ce_pin))
    if irq_pin_config := config.get(CONF_IRQ_PIN):
        irq_pin = await cg.gpio_pin_expression(irq_pin_config)
        cg.add(var.set_irq_pin(irq_pin))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_pa_level(config[CONF_PA_LEVEL]))

    for inverter_config in config[CONF_INVERTERS]:
        inverter = cg.new_Pvariable(inverter_config[CONF_ID])
        cg.add(inverter.set_serial(inverter_config[CONF_SERIAL]))
        cg.add(inverter.set_model(inverter_config[CONF_MODEL]))
        cg.add(var.add_inverter(inverter))
