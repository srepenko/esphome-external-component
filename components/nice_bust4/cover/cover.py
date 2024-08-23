import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_UPDATE_INTERVAL, CONF_USE_ADDRESS

from .. import CONF_NICEBUST4_ID, NICEBUST4_COMPONENT_SCHEMA, nice_bust4_ns

Nice = nice_bust4_ns.class_('NiceBusT4', cover.Cover, cg.Component)

NICE_COVER_SCHEMA = cover.COVER_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(Nice),
    cv.Optional(CONF_ADDRESS): cv.hex_uint16_t,
    cv.Optional(CONF_USE_ADDRESS): cv.hex_uint16_t,
    cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = NICEBUST4_COMPONENT_SCHEMA.extend(cv.NICE_COVER_SCHEMA)
        
async def to_code(config):
    paren = await cg.get_variable(config[CONF_NICEBUST4_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cover.register_cover(var, config)
    cg.add(var.set_parent(paren))

    if CONF_ADDRESS in config:
        address = config[CONF_ADDRESS]
        cg.add(var.set_to_address(address))

    if CONF_USE_ADDRESS in config:
        use_address = config[CONF_USE_ADDRESS]
        cg.add(var.set_from_address(use_address))
    