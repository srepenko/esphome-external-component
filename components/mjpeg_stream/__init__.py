"""mjpeg_stream: живий MJPEG-потік на ESP32-P4 з апаратним JPEG-декодером, як image для LVGL."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import display, image
from esphome.components.esp32 import include_builtin_idf_component, get_esp32_variant, VARIANT_ESP32P4
from esphome.const import CONF_ID, CONF_URL, CONF_USERNAME, CONF_PASSWORD, CONF_WIDTH, CONF_HEIGHT

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["image"]
MULTI_CONF = True

CONF_AUTOSTART = "autostart"
CONF_RGB_ORDER = "rgb_order"
CONF_MAX_JPEG_SIZE = "max_jpeg_size"
CONF_ON_FRAME = "on_frame"
CONF_ON_CONNECTED = "on_connected"
CONF_ON_DISCONNECTED = "on_disconnected"

ns = cg.esphome_ns.namespace("mjpeg_stream")
MjpegStream = ns.class_("MjpegStream", cg.Component, image.Image_)
StartAction = ns.class_("StartAction", automation.Action)
StopAction = ns.class_("StopAction", automation.Action)


def _multiple_of_16(value):
    value = cv.int_range(min=16, max=1280)(value)
    if value % 16:
        raise cv.Invalid("має бути кратним 16 (вимога апаратного JPEG-декодера)")
    return value


def _only_p4(config):
    if get_esp32_variant() != VARIANT_ESP32P4:
        raise cv.Invalid("mjpeg_stream потребує ESP32-P4 (апаратний JPEG-декодер)")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(MjpegStream),
        cv.Required(CONF_URL): cv.url,
        cv.Optional(CONF_USERNAME): cv.string,
        cv.Optional(CONF_PASSWORD): cv.string,
        cv.Required(CONF_WIDTH): _multiple_of_16,
        cv.Required(CONF_HEIGHT): _multiple_of_16,
        cv.Optional(CONF_AUTOSTART, default=True): cv.boolean,
        cv.Optional(CONF_RGB_ORDER, default="BGR"): cv.one_of("RGB", "BGR", upper=True),
        cv.Optional(CONF_MAX_JPEG_SIZE, default=98304): cv.int_range(min=8192, max=1048576),
        cv.Optional("direct_draw"): cv.Schema({
            cv.Required("display"): cv.use_id(display.Display),
            cv.Required("x"): cv.int_,
            cv.Required("y"): cv.int_,
            cv.Optional("radius", default=0): cv.int_range(min=0, max=64),
            cv.Optional("background", default=0x000000): cv.hex_uint32_t,
            cv.Optional("exclude", default=[]): cv.ensure_list(cv.Schema({
                cv.Required("x"): cv.int_, cv.Required("y"): cv.int_,
                cv.Required("width"): cv.int_, cv.Required("height"): cv.int_})),
        }),
        cv.Optional(CONF_ON_FRAME): automation.validate_automation(single=True),
        cv.Optional(CONF_ON_CONNECTED): automation.validate_automation(single=True),
        cv.Optional(CONF_ON_DISCONNECTED): automation.validate_automation(single=True),
    }).extend(cv.COMPONENT_SCHEMA),
    _only_p4,
)


async def to_code(config):
    # LVGL під час генерації коду шукає тип і розмір картинки
    image.add_metadata(config[CONF_ID], config[CONF_WIDTH], config[CONF_HEIGHT], "RGB565", "opaque")
    include_builtin_idf_component("esp_http_client")
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_WIDTH], config[CONF_HEIGHT])
    await cg.register_component(var, config)
    cg.add(var.set_url(config[CONF_URL]))
    if CONF_USERNAME in config:
        cg.add(var.set_username(config[CONF_USERNAME]))
    if CONF_PASSWORD in config:
        cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_autostart(config[CONF_AUTOSTART]))
    cg.add(var.set_rgb_order_bgr(config[CONF_RGB_ORDER] == "BGR"))
    cg.add(var.set_max_jpeg_size(config[CONF_MAX_JPEG_SIZE]))
    if "direct_draw" in config:
        dd = config["direct_draw"]
        disp = await cg.get_variable(dd["display"])
        bg = dd["background"]
        r, g, b = (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF
        bg565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        cg.add(var.set_direct(disp, dd["x"], dd["y"], dd["radius"], bg565))
        for ex in dd["exclude"]:
            cg.add(var.add_exclude(ex["x"], ex["y"], ex["width"], ex["height"]))
    for key, getter in ((CONF_ON_FRAME, var.get_frame_trigger()), (CONF_ON_CONNECTED, var.get_connected_trigger()),
                        (CONF_ON_DISCONNECTED, var.get_disconnected_trigger())):
        if key in config:
            await automation.build_automation(getter, [], config[key])


ACTION_SCHEMA = automation.maybe_simple_id({cv.GenerateID(): cv.use_id(MjpegStream)})


@automation.register_action("mjpeg_stream.start", StartAction, ACTION_SCHEMA, synchronous=True)
@automation.register_action("mjpeg_stream.stop", StopAction, ACTION_SCHEMA, synchronous=True)
async def action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
