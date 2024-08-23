import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["cover"]
MULTI_CONF = True
CONF_NICEBUST4_ID = "nice_bust4_id"

nice_bust4_ns = cg.esphome_ns.namespace("nice_bust4")

NiceBusT4 = nice_bust4_ns.class_(
    "NiceBusT4", cg.Component, uart.UARTDevice
#    "NiceBusT4", cg.Component, uart.IDFUARTComponent, cover.Cover
)

NICEBUST4_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NICEBUST4_ID): cv.use_id(NiceBusT4),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.GenerateID(): cv.declare_id(NiceBusT4)})
#    .extend(cv.polling_component_schema("1000ms"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)



#CONFIG_SCHEMA = (
#    cv.Schema({cv.GenerateID(): cv.declare_id(NiceBusT4)})
#    .extend(cv.COMPONENT_SCHEMA)
##    .extend(uart.UART_DEVICE_SCHEMA)
#)


#async def to_code(config):
#    var = cg.new_Pvariable(config[CONF_ID])
#    await cg.register_component(var, config)
#    #await uart.register_uart_device(var, config)
