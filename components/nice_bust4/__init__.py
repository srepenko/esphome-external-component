import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["cover"]
MULTI_CONF = True

CONF_NICEBUST4_ID = "nice_bust4_id"

nice_bust4_ns = cg.esphome_ns.namespace("nice_bust4")
NiceBusT4 = nice_bust4_ns.class_("NiceBusT4", cg.Component, uart.IDFUARTComponent)

NICEBUST4_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NICEBUST4_ID): cv.use_id(NiceBusT4),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.GenerateID(): cv.declare_id(NiceBusT4)})
    #.extend(cv.polling_component_schema("1000ms"))
    #.extend(uart.UART_DEVICE_SCHEMA)
)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    #yield uart.register_uart_device(var, config)
