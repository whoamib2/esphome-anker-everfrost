import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, climate
from esphome.const import CONF_ID

from .const import CONF_RAW_PACKET_LOGGING

DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor", "binary_sensor", "button"]

everfrost_ns = cg.esphome_ns.namespace("everfrost")
EverFrostClimate = everfrost_ns.class_(
    "EverFrostClimate",
    climate.Climate,
    cg.PollingComponent,
    ble_client.BLEClientNode,
)

CONFIG_SCHEMA = (
    climate.climate_schema(EverFrostClimate)
    .extend(
        {
            cv.Optional(CONF_RAW_PACKET_LOGGING, default=False): cv.boolean,
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_raw_packet_logging(config[CONF_RAW_PACKET_LOGGING]))
