import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import ENTITY_CATEGORY_CONFIG

from .climate import EverFrostClimate
from .const import (
    CONF_EVERFROST_ID,
    CONF_SCREEN_BRIGHTNESS,
    CONF_VOLTAGE_PROTECTION,
)


everfrost_ns = cg.esphome_ns.namespace("everfrost")
EverFrostVoltageProtectionSelect = everfrost_ns.class_(
    "EverFrostVoltageProtectionSelect",
    select.Select,
)
EverFrostScreenBrightnessSelect = everfrost_ns.class_(
    "EverFrostScreenBrightnessSelect",
    select.Select,
)

LEVEL_OPTIONS = ["Low", "Medium", "High"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_EVERFROST_ID): cv.use_id(EverFrostClimate),
        cv.Optional(CONF_VOLTAGE_PROTECTION): select.select_schema(
            EverFrostVoltageProtectionSelect,
            icon="mdi:car-battery",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_SCREEN_BRIGHTNESS): select.select_schema(
            EverFrostScreenBrightnessSelect,
            icon="mdi:brightness-6",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_EVERFROST_ID])

    if CONF_VOLTAGE_PROTECTION in config:
        var = await select.new_select(
            config[CONF_VOLTAGE_PROTECTION],
            options=LEVEL_OPTIONS,
        )
        cg.add(var.set_parent(parent))
        cg.add(parent.set_voltage_protection_select(var))

    if CONF_SCREEN_BRIGHTNESS in config:
        var = await select.new_select(
            config[CONF_SCREEN_BRIGHTNESS],
            options=LEVEL_OPTIONS,
        )
        cg.add(var.set_parent(parent))
        cg.add(parent.set_screen_brightness_select(var))
