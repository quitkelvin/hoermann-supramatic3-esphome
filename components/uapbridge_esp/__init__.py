import logging

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import uart, text_sensor, sensor
from ..uapbridge import to_code_base, CONFIG_SCHEMA_BASE, UAPBridge
from esphome.const import (
    CONF_ID,
    CONF_PLATFORM,
    DEVICE_CLASS_DURATION,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_SECOND,
)

_LOGGER = logging.getLogger(__name__)

AUTO_LOAD = ["uapbridge", "text_sensor", "sensor"]
MULTI_CONF = True

# Extensions of this fork (see CHANGELOG.md). Without any of the options below the component
# behaves like the original inject mode: a real UAP1 stays on the bus (parallel_uap1: true).
CONF_PARALLEL_UAP1 = "parallel_uap1"      # true: real UAP1 stays connected, board answers only when a command is pending
CONF_INJECT_V2 = "inject_v2"              # immediate answer, 3 s fetch timeout, effect check with end positions
CONF_P_SERIES_MODE = "p_series_mode"      # SupraMatic P series 3: drive reports only end position bits
CONF_LAST_COMMAND = "last_command"
CONF_RAW_STATUS = "raw_status"
CONF_DERIVED_STATUS = "derived_status"
# 1.0.1
CONF_MODE = "mode"                        # monitor | inject | emulate (replaces parallel_uap1/inject_v2)
CONF_STOP_PRIORITY = "stop_priority"      # Stop replaces a command that is still waiting to be fetched
CONF_CONNECTION_TIMEOUT = "connection_timeout"   # 0 = off
CONF_INJECT_MAX_FAILURES = "inject_max_failures" # 0 = off
CONF_INJECT_PAUSE = "inject_pause"
CONF_COMMAND_RESULT = "command_result"
CONF_VALID_BROADCASTS = "valid_broadcasts"
CONF_CRC_ERRORS = "crc_errors"
CONF_SECONDS_SINCE_BROADCAST = "seconds_since_broadcast"
CONF_INJECT_FAILURES = "inject_failures"

MODES = ["monitor", "inject", "emulate"]


def _resolve_mode(config):
    """(parallel_uap1, inject_v2, mode or None) - mode wins, otherwise the deprecated options (1.0.0 defaults)."""
    if CONF_MODE in config:
        mode = config[CONF_MODE]
        return mode != "emulate", mode in ("monitor", "inject"), mode
    return config.get(CONF_PARALLEL_UAP1, True), config.get(CONF_INJECT_V2, False), None


def _check_combination(config):
    old = [k for k in (CONF_PARALLEL_UAP1, CONF_INJECT_V2) if k in config]
    if CONF_MODE in config and old:
        raise cv.Invalid(f"'mode' cannot be combined with the deprecated option(s) {', '.join(old)}")
    parallel, v2, mode = _resolve_mode(config)
    if mode is None:
        if old:
            _LOGGER.warning("uapbridge_esp: %s is deprecated, use 'mode: inject' (parallel_uap1 + inject_v2), "
                            "'mode: emulate' (parallel_uap1: false) or 'mode: monitor'", " / ".join(old))
    if config.get(CONF_P_SERIES_MODE) and not v2:
        raise cv.Invalid("p_series_mode requires mode: inject or mode: monitor (or the deprecated inject_v2: true)")
    if v2 and not parallel:
        raise cv.Invalid("inject_v2 requires parallel_uap1: true (it is only tested next to a real UAP1)")
    if mode == "emulate":
        _LOGGER.warning("uapbridge_esp: mode: emulate is EXPERIMENTAL and not verified by the authors of this fork. "
                        "Never connect a real UAP1 at the same time.")
    return config


# Create UAPBridge_esp namespace
uapbridge_esp_ns = cg.esphome_ns.namespace("uapbridge_esp")
UAPBridge_esp = uapbridge_esp_ns.class_("UAPBridge_esp", UAPBridge)

CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA_BASE.extend(
        {
            cv.GenerateID(): cv.declare_id(UAPBridge_esp),
            cv.Optional(CONF_MODE): cv.one_of(*MODES, lower=True),
            cv.Optional(CONF_PARALLEL_UAP1): cv.boolean,   # deprecated -> mode
            cv.Optional(CONF_INJECT_V2): cv.boolean,       # deprecated -> mode
            cv.Optional(CONF_STOP_PRIORITY, default=True): cv.boolean,
            cv.Optional(CONF_CONNECTION_TIMEOUT, default="5s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_INJECT_MAX_FAILURES, default=5): cv.int_range(min=0, max=50),
            cv.Optional(CONF_INJECT_PAUSE, default="60s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_COMMAND_RESULT): text_sensor.text_sensor_schema(
                icon="mdi:check-network-outline", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_VALID_BROADCASTS): sensor.sensor_schema(
                icon="mdi:counter", accuracy_decimals=0, state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_CRC_ERRORS): sensor.sensor_schema(
                icon="mdi:alert-circle-outline", accuracy_decimals=0, state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_SECONDS_SINCE_BROADCAST): sensor.sensor_schema(
                unit_of_measurement=UNIT_SECOND, icon="mdi:timer-sand", accuracy_decimals=0,
                device_class=DEVICE_CLASS_DURATION, state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_INJECT_FAILURES): sensor.sensor_schema(
                icon="mdi:alert-octagon-outline", accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
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

def _warn_p_series_light(config):
    if not config.get(CONF_P_SERIES_MODE):
        return config
    full = fv.full_config.get()
    if any(l.get(CONF_PLATFORM) == "uapbridge" for l in full.get("light", []) or []):
        _LOGGER.warning("uapbridge_esp: p_series_mode is set and a 'light: platform: uapbridge' exists. The P series "
                        "does not report the light state, so the light entity only TOGGLES (on and off both send "
                        "'toggle light'). Use 'button: platform: uapbridge, toggle_light_button:' instead.")
    return config


FINAL_VALIDATE_SCHEMA = cv.All(
    uart.final_validate_device_schema(
        "uapbridge_uart",
        require_tx=True,
        require_rx=True,
        baud_rate=19200
    ),
    _warn_p_series_light,
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await to_code_base(var, config)
    await uart.register_uart_device(var, config)
    parallel, v2, mode = _resolve_mode(config)
    cg.add(var.set_inject_mode(parallel))
    if v2:
        cg.add(var.set_einwurf_v2(True))
    if mode == "monitor":
        cg.add(var.set_monitor_mode(True))
    cg.add(var.set_stop_priority(config[CONF_STOP_PRIORITY]))
    cg.add(var.set_connection_timeout(config[CONF_CONNECTION_TIMEOUT].total_milliseconds))
    cg.add(var.set_inject_max_failures(config[CONF_INJECT_MAX_FAILURES]))
    cg.add(var.set_inject_pause(config[CONF_INJECT_PAUSE].total_milliseconds))
    if CONF_COMMAND_RESULT in config:
        sens = await text_sensor.new_text_sensor(config[CONF_COMMAND_RESULT])
        cg.add(var.set_command_result_sensor(sens))
    for key, setter in (
        (CONF_VALID_BROADCASTS, var.set_valid_broadcasts_sensor),
        (CONF_CRC_ERRORS, var.set_crc_errors_sensor),
        (CONF_SECONDS_SINCE_BROADCAST, var.set_seconds_since_broadcast_sensor),
        (CONF_INJECT_FAILURES, var.set_inject_failures_sensor),
    ):
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(setter(sens))
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
