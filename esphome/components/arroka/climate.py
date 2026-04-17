import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, uart
from esphome.const import CONF_ID

DEPENDENCIES = ['uart']
AUTO_LOAD = ['climate']

arroka_ns = cg.esphome_ns.namespace('arroka')
ArrokaClimate = arroka_ns.class_(
    'ArrokaClimate', climate.Climate, cg.Component, uart.UARTDevice
)

CONF_DE_RE_PIN = 'de_re_pin'

CONFIG_SCHEMA = cv.All(
    climate.climate_schema(ArrokaClimate).extend({
        cv.Optional(CONF_DE_RE_PIN, default=4): cv.int_,
    }).extend(uart.UART_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_de_re_pin(config[CONF_DE_RE_PIN]))
