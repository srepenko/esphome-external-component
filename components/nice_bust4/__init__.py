import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]

nice_bust4_ns = cg.esphome_ns.namespace("nice_bust4")
NiceBusT4 = nice_bust4_ns.class_(
#    "NiceBusT4", cg.Component, uart.UARTDevice
    "NiceBusT4", uart.IDFUARTComponent
)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(NiceBusT4)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_IDFUARTCOMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
