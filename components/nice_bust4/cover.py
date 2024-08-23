import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_UPDATE_INTERVAL, CONF_USE_ADDRESS

CONFIG_SCHEMA = cover.COVER_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(Nice),
    cv.Optional(CONF_ADDRESS): cv.hex_uint16_t,
    cv.Optional(CONF_USE_ADDRESS): cv.hex_uint16_t,
#    cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_RX_PIN): cv.uint8_t,
    cv.Optional(CONF_TX_PIN): cv.uint8_t,
}).extend(cv.COMPONENT_SCHEMA)#.extend(uart.UART_DEVICE_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    #yield uart.register_uart_device(var, config)

    yield cover.register_cover(var, config)

    if CONF_ADDRESS in config:
        address = config[CONF_ADDRESS]
        cg.add(var.set_to_address(address))

    if CONF_USE_ADDRESS in config:
        use_address = config[CONF_USE_ADDRESS]
        cg.add(var.set_from_address(use_address))
    
    if CONF_RX_PIN in config:
        rx_pin = config[CONF_RX_PIN]
        cg.add(var.set_rx_pin(rx_pin))
    
    if CONF_TX_PIN in config:
        tx_pin = config[CONF_TX_PIN]
        cg.add(var.set_tx_pin(tx_pin))
        
