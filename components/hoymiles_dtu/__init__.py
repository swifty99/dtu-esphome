import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = []
CODEOWNERS = ["@swifty99"]

hoymiles_dtu_ns = cg.esphome_ns.namespace("hoymiles_dtu")
HoymilesDtuComponent = hoymiles_dtu_ns.class_("HoymilesDtuComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HoymilesDtuComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
