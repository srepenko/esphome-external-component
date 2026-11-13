"""Рух і рівень світла з вбудованої камери панелі (OV02C10) — рахується на самій панелі, кадри нікуди не йдуть.

Порт jc8012_camera з Heronimonimo/tessera-jc8012-camera без залежності від Tessera.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, logger, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_ILLUMINANCE,
    DEVICE_CLASS_MOTION,
    STATE_CLASS_MEASUREMENT,
    UNIT_LUX,
)

DEPENDENCIES = ["camera", "logger"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor"]

CONF_CAMERA = "camera"
CONF_STATUS = "status"
CONF_ACTIVITY = "activity"
CONF_ILLUMINANCE = "illuminance"
CONF_MOTION = "motion"

ns = cg.esphome_ns.namespace("panel_motion")
Camera = cg.esphome_ns.namespace("camera").class_("Camera", cg.EntityBase, cg.Component)
PanelMotion = ns.class_("PanelMotion", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PanelMotion),
        cv.Required(CONF_CAMERA): cv.use_id(Camera),
        cv.Optional(CONF_STATUS): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_ACTIVITY): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_ILLUMINANCE): sensor.sensor_schema(
            unit_of_measurement=UNIT_LUX, accuracy_decimals=1,
            device_class=DEVICE_CLASS_ILLUMINANCE, state_class=STATE_CLASS_MEASUREMENT),
        cv.Optional(CONF_MOTION): binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_MOTION),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_camera(await cg.get_variable(config[CONF_CAMERA])))
    if CONF_STATUS in config:
        cg.add(var.set_status(await cg.get_variable(config[CONF_STATUS])))
        logger.request_log_listener()
        cg.add(var.listen_to_log())
    if CONF_ACTIVITY in config:
        cg.add(var.set_activity(await cg.get_variable(config[CONF_ACTIVITY])))
    if CONF_ILLUMINANCE in config:
        cg.add(var.set_illuminance(await sensor.new_sensor(config[CONF_ILLUMINANCE])))
    if CONF_MOTION in config:
        cg.add(var.set_motion(await binary_sensor.new_binary_sensor(config[CONF_MOTION])))
