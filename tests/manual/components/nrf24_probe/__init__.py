import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@swifty99"]

CONF_CE_PIN = "ce_pin"
CONF_CS_PIN = "cs_pin"
CONF_SCK_PIN = "sck_pin"
CONF_MOSI_PIN = "mosi_pin"
CONF_MISO_PIN = "miso_pin"
CONF_IRQ_PIN = "irq_pin"

nrf24_probe_ns = cg.esphome_ns.namespace("nrf24_probe")
Nrf24Probe = nrf24_probe_ns.class_("Nrf24Probe", cg.Component)

GPIO_NUM = cv.int_range(min=0, max=48)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Nrf24Probe),
        cv.Required(CONF_CE_PIN): GPIO_NUM,
        cv.Required(CONF_CS_PIN): GPIO_NUM,
        cv.Required(CONF_SCK_PIN): GPIO_NUM,
        cv.Required(CONF_MOSI_PIN): GPIO_NUM,
        cv.Required(CONF_MISO_PIN): GPIO_NUM,
        cv.Optional(CONF_IRQ_PIN): GPIO_NUM,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(
        var.set_pins(
            config[CONF_CE_PIN],
            config[CONF_CS_PIN],
            config[CONF_SCK_PIN],
            config[CONF_MOSI_PIN],
            config[CONF_MISO_PIN],
            config.get(CONF_IRQ_PIN, -1),
        )
    )
