import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, climate
from esphome.const import CONF_ID

from .const import CONF_RAW_PACKET_LOGGING, CONF_ZONE_2

DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor", "binary_sensor", "button", "select"]

everfrost_ns = cg.esphome_ns.namespace("everfrost")
EverFrostClimate = everfrost_ns.class_(
    "EverFrostClimate",
    climate.Climate,
    cg.PollingComponent,
    ble_client.BLEClientNode,
)
EverFrostZoneClimate = everfrost_ns.class_(
    "EverFrostZoneClimate",
    climate.Climate,
)

CONFIG_SCHEMA = (
    climate.climate_schema(EverFrostClimate)
    .extend(
        {
            cv.Optional(CONF_RAW_PACKET_LOGGING, default=False): cv.boolean,
            cv.Optional(CONF_ZONE_2): climate.climate_schema(EverFrostZoneClimate),
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

    if CONF_ZONE_2 in config:
        zone2_config = config[CONF_ZONE_2]
        zone2 = cg.new_Pvariable(zone2_config[CONF_ID])
        await climate.register_climate(zone2, zone2_config)
        cg.add(zone2.set_parent(var))
        cg.add(var.set_zone2_climate(zone2))
