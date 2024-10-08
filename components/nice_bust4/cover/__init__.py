import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_UPDATE_INTERVAL, CONF_USE_ADDRESS

DEPENDENCIES = ["nice_bust4"]

from .. import CONF_NICEBUST4_ID, NICEBUST4_SCHEMA, nice_bust4_ns

NiceBusT4Cover = nice_bust4_ns.class_('NiceBusT4Cover', cover.Cover, cg.Component)


CONFIG_SCHEMA = NICEBUST4_SCHEMA.extend(
    cover.COVER_SCHEMA.extend({
        cv.GenerateID(): cv.declare_id(NiceBusT4Cover),
        cv.Optional(CONF_ADDRESS): cv.hex_uint16_t,
        cv.Optional(CONF_USE_ADDRESS): cv.hex_uint16_t,
        cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
    }).extend(cv.COMPONENT_SCHEMA)
)
        
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
    
    cg.add(paren.register_cover(var))