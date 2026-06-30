import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import spi
from esphome.const import CONF_ID

from .const import (
    CONF_ADDRESS,
    CONF_CE_PIN,
    CONF_CHANNEL,
    CONF_DWELL_MS,
    CONF_INVERTERS,
    CONF_IRQ_PIN,
    CONF_MODEL,
    CONF_PA_LEVEL,
    CONF_PAYLOAD,
    CONF_POLL_INTERVAL,
    CONF_RX_DWELL_MS,
    CONF_RX_OFFSET,
    CONF_RX_WINDOW_MS,
    CONF_SERIAL,
    CONF_SERIAL_FORMAT,
    CONF_TIMESTAMP,
    CONF_TX_CHANNEL,
    CONF_WINDOW_MS,
    SERIAL_FORMAT_BCD,
    SERIAL_FORMAT_DECIMAL,
    SERIAL_FORMAT_RAW,
)
from .protocol import parse_serial_bcd

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
RadioDumpAction = hoymiles_dtu_ns.class_("RadioDumpAction", automation.Action)
RadioSendRequestAction = hoymiles_dtu_ns.class_(
    "RadioSendRequestAction", automation.Action
)
RadioSendRawAction = hoymiles_dtu_ns.class_("RadioSendRawAction", automation.Action)
RadioListenAction = hoymiles_dtu_ns.class_("RadioListenAction", automation.Action)

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

PA_LEVEL_VALUE_OPTIONS = {
    "min": 0,
    "low": 1,
    "high": 2,
    "max": 3,
}

SERIAL_FORMAT_OPTIONS = {
    SERIAL_FORMAT_DECIMAL: 0,
    SERIAL_FORMAT_BCD: 1,
    SERIAL_FORMAT_RAW: 2,
}


def validate_serial(value):
    # Hoymiles serials are packed BCD: the printed digits map directly to the
    # radio-address bytes (exactly like Ahoy, which parses them as base-16).
    return parse_serial_bcd(value)


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


RADIO_DUMP_ACTION_SCHEMA = automation.maybe_simple_id(
    {
        cv.GenerateID(): cv.use_id(HoymilesDtuComponent),
    }
)


@automation.register_action(
    "hoymiles_dtu.radio_dump",
    RadioDumpAction,
    RADIO_DUMP_ACTION_SCHEMA,
    synchronous=True,
)
async def radio_dump_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


RADIO_SEND_REQUEST_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(HoymilesDtuComponent),
        cv.Required(CONF_SERIAL): cv.templatable(cv.string_strict),
        cv.Optional(CONF_SERIAL_FORMAT, default=SERIAL_FORMAT_BCD): cv.enum(
            SERIAL_FORMAT_OPTIONS, lower=True
        ),
        cv.Optional(CONF_TX_CHANNEL, default=3): cv.templatable(
            cv.int_range(min=0, max=125)
        ),
        cv.Optional(CONF_RX_OFFSET, default=3): cv.templatable(
            cv.int_range(min=-5, max=5)
        ),
        cv.Optional(CONF_RX_WINDOW_MS, default=520): cv.templatable(
            cv.int_range(min=0, max=5000)
        ),
        cv.Optional(CONF_RX_DWELL_MS, default=8): cv.templatable(
            cv.int_range(min=0, max=1000)
        ),
        cv.Optional(CONF_PA_LEVEL, default="low"): cv.templatable(
            cv.enum(PA_LEVEL_VALUE_OPTIONS, lower=True)
        ),
        cv.Optional(CONF_TIMESTAMP, default=0): cv.templatable(
            cv.int_range(min=0, max=0xFFFFFFFF)
        ),
    }
)


@automation.register_action(
    "hoymiles_dtu.radio_send_request",
    RadioSendRequestAction,
    RADIO_SEND_REQUEST_ACTION_SCHEMA,
    synchronous=True,
)
async def radio_send_request_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    serial = await cg.templatable(config[CONF_SERIAL], args, cg.std_string)
    serial_format = await cg.templatable(config[CONF_SERIAL_FORMAT], args, cg.uint8)
    tx_channel = await cg.templatable(config[CONF_TX_CHANNEL], args, cg.uint8)
    rx_offset = await cg.templatable(config[CONF_RX_OFFSET], args, cg.int8)
    rx_window_ms = await cg.templatable(config[CONF_RX_WINDOW_MS], args, cg.uint16)
    rx_dwell_ms = await cg.templatable(config[CONF_RX_DWELL_MS], args, cg.uint16)
    pa_level = await cg.templatable(config[CONF_PA_LEVEL], args, cg.uint8)
    timestamp = await cg.templatable(config[CONF_TIMESTAMP], args, cg.uint32)
    cg.add(var.set_serial(serial))
    cg.add(var.set_serial_format(serial_format))
    cg.add(var.set_tx_channel(tx_channel))
    cg.add(var.set_rx_offset(rx_offset))
    cg.add(var.set_rx_window_ms(rx_window_ms))
    cg.add(var.set_rx_dwell_ms(rx_dwell_ms))
    cg.add(var.set_pa_level(pa_level))
    cg.add(var.set_timestamp(timestamp))
    return var


RADIO_SEND_RAW_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(HoymilesDtuComponent),
        cv.Required(CONF_ADDRESS): cv.templatable(cv.string_strict),
        cv.Required(CONF_PAYLOAD): cv.templatable(cv.string_strict),
        cv.Optional(CONF_TX_CHANNEL, default=3): cv.templatable(
            cv.int_range(min=0, max=125)
        ),
        cv.Optional(CONF_RX_OFFSET, default=3): cv.templatable(
            cv.int_range(min=-5, max=5)
        ),
        cv.Optional(CONF_RX_WINDOW_MS, default=520): cv.templatable(
            cv.int_range(min=0, max=5000)
        ),
        cv.Optional(CONF_RX_DWELL_MS, default=8): cv.templatable(
            cv.int_range(min=0, max=1000)
        ),
        cv.Optional(CONF_PA_LEVEL, default="low"): cv.templatable(
            cv.enum(PA_LEVEL_VALUE_OPTIONS, lower=True)
        ),
    }
)


@automation.register_action(
    "hoymiles_dtu.radio_send_raw",
    RadioSendRawAction,
    RADIO_SEND_RAW_ACTION_SCHEMA,
    synchronous=True,
)
async def radio_send_raw_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    address = await cg.templatable(config[CONF_ADDRESS], args, cg.std_string)
    payload = await cg.templatable(config[CONF_PAYLOAD], args, cg.std_string)
    tx_channel = await cg.templatable(config[CONF_TX_CHANNEL], args, cg.uint8)
    rx_offset = await cg.templatable(config[CONF_RX_OFFSET], args, cg.int8)
    rx_window_ms = await cg.templatable(config[CONF_RX_WINDOW_MS], args, cg.uint16)
    rx_dwell_ms = await cg.templatable(config[CONF_RX_DWELL_MS], args, cg.uint16)
    pa_level = await cg.templatable(config[CONF_PA_LEVEL], args, cg.uint8)
    cg.add(var.set_address(address))
    cg.add(var.set_payload(payload))
    cg.add(var.set_tx_channel(tx_channel))
    cg.add(var.set_rx_offset(rx_offset))
    cg.add(var.set_rx_window_ms(rx_window_ms))
    cg.add(var.set_rx_dwell_ms(rx_dwell_ms))
    cg.add(var.set_pa_level(pa_level))
    return var


RADIO_LISTEN_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(HoymilesDtuComponent),
        cv.Required(CONF_CHANNEL): cv.templatable(cv.int_range(min=0, max=125)),
        cv.Optional(CONF_WINDOW_MS, default=520): cv.templatable(
            cv.int_range(min=1, max=5000)
        ),
        cv.Optional(CONF_DWELL_MS, default=8): cv.templatable(
            cv.int_range(min=0, max=1000)
        ),
    }
)


@automation.register_action(
    "hoymiles_dtu.radio_listen",
    RadioListenAction,
    RADIO_LISTEN_ACTION_SCHEMA,
    synchronous=True,
)
async def radio_listen_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    channel = await cg.templatable(config[CONF_CHANNEL], args, cg.uint8)
    window_ms = await cg.templatable(config[CONF_WINDOW_MS], args, cg.uint16)
    dwell_ms = await cg.templatable(config[CONF_DWELL_MS], args, cg.uint16)
    cg.add(var.set_channel(channel))
    cg.add(var.set_window_ms(window_ms))
    cg.add(var.set_dwell_ms(dwell_ms))
    return var
