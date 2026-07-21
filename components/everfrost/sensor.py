import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_CURRENT_TEMPERATURE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

from .climate import EverFrostClimate
from .const import CONF_EVERFROST_ID

CONF_BATTERY = "battery"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_EVERFROST_ID): cv.use_id(EverFrostClimate),
        cv.Optional(CONF_CURRENT_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_BATTERY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_EVERFROST_ID])

    if CONF_CURRENT_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT_TEMPERATURE])
        cg.add(parent.set_current_temperature_sensor(sens))

    if CONF_BATTERY in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY])
        cg.add(parent.set_battery_sensor(sens))
