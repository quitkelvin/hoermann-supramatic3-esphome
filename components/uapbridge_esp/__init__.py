import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, text_sensor
from ..uapbridge import to_code_base, CONFIG_SCHEMA_BASE, UAPBridge
from esphome.const import CONF_ID

AUTO_LOAD = ["uapbridge", "text_sensor"]
MULTI_CONF = True

# Extensions of this fork (see CHANGELOG.md). Without any of the options below the component
# behaves like the original inject mode: a real UAP1 stays on the bus (parallel_uap1: true).
CONF_PARALLEL_UAP1 = "parallel_uap1"      # true: real UAP1 stays connected, board answers only when a command is pending
CONF_INJECT_V2 = "inject_v2"              # immediate answer, 3 s fetch timeout, effect check with end positions
CONF_P_SERIES_MODE = "p_series_mode"      # SupraMatic P series 3: drive reports only end position bits
CONF_LAST_COMMAND = "last_command"
CONF_RAW_STATUS = "raw_status"
CONF_DERIVED_STATUS = "derived_status"


def _check_combination(config):
    if config.get(CONF_P_SERIES_MODE) and not config.get(CONF_INJECT_V2):
        raise cv.Invalid("p_series_mode requires inject_v2: true")
    if config.get(CONF_INJECT_V2) and not config.get(CONF_PARALLEL_UAP1):
        raise cv.Invalid("inject_v2 requires parallel_uap1: true (it is only tested next to a real UAP1)")
    return config


# Create UAPBridge_esp namespace
uapbridge_esp_ns = cg.esphome_ns.namespace("uapbridge_esp")
UAPBridge_esp = uapbridge_esp_ns.class_("UAPBridge_esp", UAPBridge)

CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA_BASE.extend(
        {
            cv.GenerateID(): cv.declare_id(UAPBridge_esp),
            cv.Optional(CONF_PARALLEL_UAP1, default=True): cv.boolean,
            cv.Optional(CONF_INJECT_V2, default=False): cv.boolean,
            cv.Optional(CONF_P_SERIES_MODE, default=False): cv.boolean,
            cv.Optional(CONF_LAST_COMMAND): text_sensor.text_sensor_schema(
                icon="mdi:garage-alert", entity_category="diagnostic"
            ),
            cv.Optional(CONF_RAW_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:code-braces", entity_category="diagnostic"
            ),
            cv.Optional(CONF_DERIVED_STATUS): text_sensor.text_sensor_schema(icon="mdi:garage"),
        }
    ).add_extra(cv.only_on_esp32),
    _check_combination,
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "uapbridge_uart",
    require_tx=True,
    require_rx=True,
    baud_rate=19200
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await to_code_base(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_inject_mode(config[CONF_PARALLEL_UAP1]))
    if config[CONF_INJECT_V2]:
        cg.add(var.set_einwurf_v2(True))
    if CONF_LAST_COMMAND in config:
        sens = await text_sensor.new_text_sensor(config[CONF_LAST_COMMAND])
        cg.add(var.set_letzter_befehl_sensor(sens))
    if config[CONF_P_SERIES_MODE]:
        cg.add(var.set_p_modus(True))
    if CONF_DERIVED_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_DERIVED_STATUS])
        cg.add(var.set_status_abgeleitet_sensor(sens))
    if CONF_RAW_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_RAW_STATUS])
        cg.add(var.set_rohstatus_sensor(sens))
